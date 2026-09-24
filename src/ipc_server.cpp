#include "ipc_server.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
// A client that hasn't drained this many queued bytes is treated as stuck
// (suspended process, dead reader that never noticed) and dropped rather
// than left to grow without bound.
constexpr size_t kMaxOutboxBytes = 1 << 20; // 1 MiB
} // namespace

IpcServer::IpcServer(std::string socketPath) : socketPath_(std::move(socketPath)) {}

IpcServer::~IpcServer() { stop(); }

void IpcServer::setSnapshotProvider(SnapshotFn fn) { snapshotFn_ = std::move(fn); }

bool IpcServer::start() {
  if (running_.exchange(true))
    return true;

  std::error_code ec;
  fs::create_directories(fs::path(socketPath_).parent_path(), ec);
  fs::remove(socketPath_, ec); // clear a stale socket file from a previous run

  listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (listen_fd_ < 0) {
    std::cerr << "[IpcServer] socket() failed: " << std::strerror(errno) << "\n";
    running_ = false;
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (socketPath_.size() >= sizeof(addr.sun_path)) {
    std::cerr << "[IpcServer] socket path too long: " << socketPath_ << "\n";
    close(listen_fd_);
    running_ = false;
    return false;
  }
  std::strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    std::cerr << "[IpcServer] bind(" << socketPath_ << ") failed: " << std::strerror(errno)
              << "\n";
    close(listen_fd_);
    running_ = false;
    return false;
  }

  if (listen(listen_fd_, 16) < 0) {
    std::cerr << "[IpcServer] listen() failed: " << std::strerror(errno) << "\n";
    close(listen_fd_);
    running_ = false;
    return false;
  }

  wake_fd_ = eventfd(0, EFD_NONBLOCK);
  epfd_ = epoll_create1(0);

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = listen_fd_;
  epoll_ctl(epfd_, EPOLL_CTL_ADD, listen_fd_, &ev);
  ev.data.fd = wake_fd_;
  epoll_ctl(epfd_, EPOLL_CTL_ADD, wake_fd_, &ev);

  thread_ = std::thread(&IpcServer::run, this);
  return true;
}

void IpcServer::stop() {
  if (!running_.exchange(false))
    return;
  if (wake_fd_ >= 0) {
    uint64_t one = 1;
    ssize_t written = write(wake_fd_, &one, sizeof(one));
    (void)written;
  }
  if (thread_.joinable())
    thread_.join();

  for (auto &c : clients_)
    close(c.fd);
  clients_.clear();

  if (listen_fd_ >= 0)
    close(listen_fd_);
  if (wake_fd_ >= 0)
    close(wake_fd_);
  if (epfd_ >= 0)
    close(epfd_);
  listen_fd_ = wake_fd_ = epfd_ = -1;

  std::error_code ec;
  fs::remove(socketPath_, ec);
}

void IpcServer::broadcast(const std::string &line) {
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingBroadcasts_.push_back(line);
  }
  if (wake_fd_ >= 0) {
    uint64_t one = 1;
    ssize_t written = write(wake_fd_, &one, sizeof(one));
    (void)written;
  }
}

void IpcServer::run() {
  epoll_event events[16];
  while (running_.load()) {
    int n = epoll_wait(epfd_, events, 16, -1);
    if (!running_.load())
      break;

    for (int i = 0; i < n; i++) {
      int fd = events[i].data.fd;
      if (fd == wake_fd_) {
        uint64_t val;
        ssize_t r = read(wake_fd_, &val, sizeof(val));
        (void)r;
        drainPendingBroadcasts();
      } else if (fd == listen_fd_) {
        acceptClients();
      } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
        closeClient(fd);
      } else if (events[i].events & EPOLLOUT) {
        auto it = std::find_if(clients_.begin(), clients_.end(),
                                [fd](const Client &c) { return c.fd == fd; });
        if (it != clients_.end())
          flushClient(*it);
      } else if (events[i].events & EPOLLIN) {
        handleClientReadable(fd);
      }
    }
  }
}

void IpcServer::acceptClients() {
  for (;;) {
    int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
    if (fd < 0)
      return; // EAGAIN: no more pending connections

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);

    clients_.push_back(Client{fd, ""});
    if (snapshotFn_) {
      clients_.back().outbox = snapshotFn_();
      flushClient(clients_.back());
    }
  }
}

void IpcServer::handleClientReadable(int fd) {
  // Broadcast-only protocol: clients aren't expected to send anything
  // meaningful. Still need to read so a closed connection (recv() == 0)
  // and a dead/reset peer are both detected instead of spinning on
  // EPOLLIN forever.
  char buf[256];
  for (;;) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0)
      continue; // discard - not part of the protocol
    if (n == 0) {
      closeClient(fd);
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;
    closeClient(fd); // real error (e.g. ECONNRESET)
    return;
  }
}

void IpcServer::flushClient(Client &c) {
  while (!c.outbox.empty()) {
    ssize_t n = send(c.fd, c.outbox.data(), c.outbox.size(), MSG_NOSIGNAL);
    if (n > 0) {
      c.outbox.erase(0, static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      break;
    // Real error (broken pipe, connection reset): drop this client.
    closeClient(c.fd);
    return;
  }

  if (c.outbox.size() > kMaxOutboxBytes) {
    std::cerr << "[IpcServer] client fd=" << c.fd << " exceeded outbox limit, dropping\n";
    closeClient(c.fd);
    return;
  }

  epoll_event ev{};
  ev.events = EPOLLIN | (c.outbox.empty() ? 0 : EPOLLOUT);
  ev.data.fd = c.fd;
  epoll_ctl(epfd_, EPOLL_CTL_MOD, c.fd, &ev);
}

void IpcServer::closeClient(int fd) {
  epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
  clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                 [fd](const Client &c) { return c.fd == fd; }),
                 clients_.end());
}

void IpcServer::drainPendingBroadcasts() {
  std::vector<std::string> batch;
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    batch.swap(pendingBroadcasts_);
  }
  if (batch.empty() || clients_.empty())
    return;

  // Snapshot the fd list first: flushClient() -> closeClient() mutates
  // clients_ on error, which would invalidate an iterator over it directly.
  std::vector<int> fds;
  fds.reserve(clients_.size());
  for (const auto &c : clients_)
    fds.push_back(c.fd);

  for (const auto &line : batch)
    for (auto &c : clients_)
      c.outbox += line + "\n";

  for (int fd : fds) {
    auto it = std::find_if(clients_.begin(), clients_.end(),
                            [fd](const Client &c) { return c.fd == fd; });
    if (it != clients_.end())
      flushClient(*it);
  }
}

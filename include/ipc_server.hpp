#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Unix-domain-socket server broadcasting JSON-lines messages to every
// connected client. One dedicated thread multiplexes the listening socket,
// all client sockets, and a wakeup eventfd via epoll.
//
// broadcast() can be called from any thread (in practice, the
// DeviceMonitor thread): it only queues the message and pokes the wakeup
// eventfd, so client socket state is ever only touched from the epoll
// thread itself - no locking needed around the sockets.
class IpcServer {
public:
  explicit IpcServer(std::string socketPath);
  ~IpcServer();

  IpcServer(const IpcServer &) = delete;
  IpcServer &operator=(const IpcServer &) = delete;

  // Builds the message sent to a client immediately after it connects
  // (e.g. a "hello" + full device snapshot), before any broadcast()
  // traffic. Invoked from the epoll thread - keep it fast, and safe to
  // call concurrently with whatever else reads the underlying state.
  using SnapshotFn = std::function<std::string()>;
  void setSnapshotProvider(SnapshotFn fn);

  bool start();
  void stop();

  // Appends one line (a trailing newline is added automatically) to every
  // connected client's outbound queue. Safe to call from any thread.
  void broadcast(const std::string &line);

private:
  struct Client {
    int fd = -1;
    std::string outbox; // bytes queued but not yet written
  };

  void run();
  void acceptClients();
  void handleClientReadable(int fd);
  void flushClient(Client &c);
  void closeClient(int fd);
  void drainPendingBroadcasts();

  std::string socketPath_;
  SnapshotFn snapshotFn_;

  int listen_fd_ = -1;
  int epfd_ = -1;
  int wake_fd_ = -1; // eventfd: wakes epoll_wait() for both stop() and broadcast()

  std::thread thread_;
  std::atomic<bool> running_{false};

  std::mutex pendingMutex_;
  std::vector<std::string> pendingBroadcasts_; // queued by broadcast(), drained on the epoll thread

  std::vector<Client> clients_; // only ever touched from the epoll thread
};

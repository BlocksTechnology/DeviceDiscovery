#pragma once
#include <utility>

// CRTP base: gives every scanner a uniform, statically-dispatched scan()
// entry point with zero virtual-call overhead. Derived classes implement
// scanImpl() (any signature - e.g. CANScanner::scanImpl(interface)) and
// keep it private, granting this template friendship; this lets generic
// code (the background scan loop in the daemon) call .scan(...) the same
// way on any scanner type without needing a common vtable.
template <typename Derived> class Scanner {
public:
  template <typename... Args> decltype(auto) scan(Args &&...args) {
    return static_cast<Derived *>(this)->scanImpl(std::forward<Args>(args)...);
  }

protected:
  Scanner() = default;
  ~Scanner() = default;
};

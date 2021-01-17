/*
 * Generally, log-like workload use group commit to imporve its throughput.
 * here, I want to know how much throughput do it gets?
 */

#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <random>
#include <string>

struct spinlock {
  // from https://rigtorp.se/spinlock/
  std::atomic<bool> lock_ = {0};

  void lock() noexcept {
    for (;;) {
      // Optimistically assume the lock is free on the first try
      if (!lock_.exchange(true, std::memory_order_acquire)) {
        return;
      }
      // Wait for lock to be released without generating cache misses
      while (lock_.load(std::memory_order_relaxed)) {
        // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
        // hyper-threads
        __builtin_ia32_pause();
      }
    }
  }

  bool try_lock() noexcept {
    // First do a relaxed load to check if lock is free in order to prevent
    // unnecessary cache misses if someone does while(!try_lock())
    return !lock_.load(std::memory_order_relaxed) &&
           !lock_.exchange(true, std::memory_order_acquire);
  }

  void unlock() noexcept { lock_.store(false, std::memory_order_release); }
};
class Writer {
 private:
  std::string outdata_ = "abcdefghijklmnopqrstuvwxyz";
  int log_fd_;

  std::mt19937 g;
  std::random_device rd;

 public:
  Writer(int logfd) : log_fd_(logfd), g(rd()) {}
  ~Writer() {}

  void DoWrite(uint32_t seqno) {
    std::shuffle(outdata_.begin(), outdata_.end(), g);

    ::write(log_fd_, outdata_.c_str(), outdata_.size());
  }
};
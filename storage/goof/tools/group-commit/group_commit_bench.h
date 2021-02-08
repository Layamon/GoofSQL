/*
 * Generally, log-like workload use group commit to imporve its throughput.
 * here, I want to know how much throughput do it gets?
 */

#include <fcntl.h>
#include <gflags/gflags.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <random>
#include <string>

DEFINE_bool(enable_group_commit, false, "enable group commit of log writting");
DEFINE_bool(enable_sync, true, "Sync Log File?");
DEFINE_bool(enable_direct, false, "Open Log File in O_DIRECT?");
DEFINE_uint32(writers, 8, "parallel writer which write into the same log.");
DEFINE_uint32(records, 100000, "how much records to be write into log.");

std::atomic<int32_t> record_cnt;
std::atomic<int32_t> actual_cnt;
std::atomic<uint64_t> seq;

uint32_t NewSequence() {
  return seq.fetch_add(1, std::memory_order_relaxed);
}
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
 protected:
  std::string outdata_ = "0123456789";
  int log_fd_;

  std::mt19937 g;
  std::random_device rd;

 public:
  Writer(int logfd) : log_fd_(logfd), g(rd()) {}
  ~Writer() {}
  const std::string &outdata() { return outdata_; }

  void DoWrite(struct spinlock *log_latch) {
    char outbuf[4 + 10];  // seqno(uint32_t) + 10 bytes data
    // prepare log data
    std::shuffle(outdata_.begin(), outdata_.end(), g);
    static_assert(sizeof(uint32_t) == 4);
    ::memmove(&outbuf[0] + 4, outdata_.c_str(), 10);

    // get seqno and write into log sequentially.
    log_latch->lock();
    auto seqno = NewSequence();
    ::memmove(&outbuf[0], &seqno, sizeof(uint32_t));
    ::write(log_fd_, outbuf, 14);
    if (FLAGS_enable_sync) {
      ::fsync(log_fd_);
    }
    record_cnt.fetch_add(1, std::memory_order_relaxed);
    actual_cnt.fetch_add(1, std::memory_order_relaxed);
    log_latch->unlock();
  }
};
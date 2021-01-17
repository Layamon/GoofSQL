#include "group_commit_bench.h"
#include <gflags/gflags.h>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

DEFINE_bool(enable_group_commit, false, "enable group commit of log writting");
DEFINE_uint32(writers, 8, "parallel writer which write into the same log.");
DEFINE_uint32(records, 100000, "how much records to be write into log.");

std::atomic<int32_t> record_cnt;

namespace single_commit {
struct spinlock log_lock;
void Write(int fd) {
  Writer writer(fd);

  while (record_cnt.load(std::memory_order_relaxed) > 0) {
    uint32_t seq =
        FLAGS_records - record_cnt.fetch_sub(1, std::memory_order_relaxed);

    log_lock.lock();
    writer.DoWrite(seq);
    log_lock.unlock();
  }
}
}  // namespace single_commit

namespace group_commit {
enum class GroupRole { INIT, LEADER, FOLLOWER };

class GroupWriter : public Writer {
 public:
  GroupWriter(int logfd) : Writer(logfd) {}
  void DoWrite(uint32_t seqno);

  GroupWriter *older_;
  GroupWriter *newer_;
  GroupRole role_;
};

// return true if w join group and become leader
bool JoinGroup(GroupWriter *w) {
  while (true) {
    auto cur_list_head = writer_list_head.load(std::memory_order_relaxed);
    w->older_ = cur_list_head;
    if (writer_list_head.compare_exchange_weak(cur_list_head, w)) {
      return cur_list_head == nullptr;
    }
  }
}

std::atomic<group_commit::GroupWriter *> writer_list_head{nullptr};
void GroupWriter::DoWrite(uint32_t seqno) {
  std::shuffle(outdata_.begin(), outdata_.end(), g);

  bool is_leader = JoinGroup(this);

}

void Write(int fd) {
  GroupWriter writer(fd);

  while (record_cnt.load(std::memory_order_relaxed)) {
    uint32_t seq =
        FLAGS_records - record_cnt.fetch_sub(1, std::memory_order_relaxed);
    writer.DoWrite(seq);
  }
}
}  // namespace group_commit

int main(int argc, char const *argv[]) {
  using namespace std::chrono;
  auto start = system_clock::now();
  int log_file_fd = ::open("my.log", O_RDWR | O_CREAT | O_APPEND);
  record_cnt.store(FLAGS_records);
  std::vector<std::thread> workers;

  if (FLAGS_enable_group_commit) {
    for (int i = 0; i < FLAGS_writers; i++) {
      workers.emplace_back(group_commit::Write, log_file_fd);
    }
  } else {
    for (int i = 0; i < FLAGS_writers; i++) {
      workers.emplace_back(single_commit::Write, log_file_fd);
    }
  }

  for (int i = 0; i < FLAGS_writers; i++) {
    workers[i].join();
  }
  auto end = system_clock::now();
  auto duration = duration_cast<microseconds>(end - start);
  std::cout << "cost: "
            << double(duration.count()) * microseconds::period::num /
                   microseconds::period::den
            << "seconds." << std::endl;

  return 0;
}
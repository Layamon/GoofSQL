#include "group_commit_bench.h"
#include <sys/stat.h>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace single_commit {
struct spinlock log_latch;
void Write(int fd) {
  Writer writer(fd);

  while (record_cnt.load(std::memory_order_relaxed) > 0) {
    writer.DoWrite(&log_latch);
  }
}
}  // namespace single_commit

namespace group_commit {
enum class GroupRole { LEADER, FOLLOWER, COMPLETE };
class GroupWriter : public Writer {
 public:
  GroupWriter(int logfd, int id) : Writer(logfd), id_(id) {
    // prepare log data
    std::shuffle(outdata_.begin(), outdata_.end(), g);
  }
  ~GroupWriter() {
    older_ = nullptr;
    newer_ = nullptr;
  }
  void DoWrite();
  void WaitState();
  void NotifyState(GroupRole r);
  int logfd() { return log_fd_; }

  GroupWriter *older_{nullptr};
  GroupWriter *newer_{nullptr};
  std::atomic<GroupRole> role_{GroupRole::FOLLOWER};
  int id_;

  std::mutex mu_;
  std::condition_variable cv_;

  // if role_ == LEADER
  volatile uint32_t group_size_{0};
};

std::atomic<group_commit::GroupWriter *> writer_list_head{nullptr};
std::atomic<bool> has_leader{false};

static inline void CheckCycle(GroupWriter *w) {
  GroupWriter *cur = w;
  size_t i = 0;
  do {
    cur = cur->older_;
    ++i;
    if (cur == w) {
      std::cout << "Cycle: " << i << std::endl;
    }
  } while (cur != nullptr);
  assert(cur == nullptr);
}

// return true if w join group and become leader
bool JoinGroup(GroupWriter *w) {
  assert(w->older_ == nullptr);
  size_t i = 0;
  while (!writer_list_head.compare_exchange_weak(w->older_, w)) {
    //CheckCycle(w);
    // assert(w->older_ != nullptr);
    ++i;
  }
  return w->older_ == nullptr;
}

void GroupWriter::WaitState() {
  std::unique_lock<std::mutex> unique_lock(mu_);
  cv_.wait(unique_lock, [this]() {
    //std::cout << "Aweak: " << id_ << std::endl;
    return (role_.load() == GroupRole::COMPLETE ||
            role_.load() == GroupRole::LEADER);
  });
}

void GroupWriter::NotifyState(GroupRole r) {
  {
    std::lock_guard<std::mutex> guard(mu_);
    role_.store(r);
  }
  cv_.notify_one();
}

// Leader collect data and write
void BatchWrite(GroupWriter *tail, GroupWriter *head) {
  assert(head != nullptr && tail != nullptr);
  // fix newer_ pointer and count group size
  GroupWriter* prev = nullptr;
  auto cur = tail;
  while (cur != head) {
    cur->newer_ = prev;
    head->group_size_++;

    prev = cur;
    cur = cur->older_;
  }
  head->newer_ = prev;
  head->group_size_++;

  // batch all data in group
  uint32_t group_data_size = 4 + 10 * head->group_size_;
  char outbuf[group_data_size];
  {
    auto mem_offset = &outbuf[0] + 4;
    GroupWriter* cur = head;
    do {
      ::memmove(mem_offset, cur->outdata().c_str(), 10);
      mem_offset += 10;
      cur = cur->newer_;
    } while (cur != nullptr);
    assert(cur == nullptr);
    assert(mem_offset == &outbuf[0] + 4 + head->group_size_ * 10);
    uint32_t seqno = NewSequence();
    ::memmove(&outbuf[0], &seqno, 4);
  }

  // write into file
  ::write(head->logfd(), outbuf, group_data_size);
  if (FLAGS_enable_sync) {
    ::fsync(head->logfd());
  }
  std::cout << "BatchWrite GroupSize: " << head->group_size_
            << ", id: " << head->id_ << std::endl;
}

bool CheckGroup(GroupWriter *tail, GroupWriter *head) {
  uint32_t group_size = 1;
  while (tail != head) {
    assert(tail->older_->newer_ == tail);
    tail = tail->older_;
    group_size++;
  }
  assert(group_size == head->group_size_);
  //std::cout << "Check GroupSize: " << group_size << std::endl;
  return false;
}

void LeaderExit(GroupWriter *last_writer, GroupWriter *leader) {
  assert(last_writer != nullptr && leader != nullptr);

  GroupWriter *new_leader = last_writer;
  bool has_pending_writer =
      !(writer_list_head.compare_exchange_weak(new_leader, nullptr));
  if (has_pending_writer) {
    // there are new writers and aweak new leader
    assert(new_leader != nullptr && new_leader != last_writer);

    while (new_leader->older_ != last_writer) {
      new_leader = new_leader->older_;
      assert(new_leader != nullptr);
    }

    assert(new_leader->older_ == last_writer && last_writer->newer_ == nullptr);
    new_leader->older_ = nullptr;
    new_leader->NotifyState(GroupRole::LEADER);
  }

  // aweak follower
  CheckGroup(last_writer, leader);
  GroupWriter *follower = leader->newer_;
  uint32_t aweak_follower_cnt = 0;
  while (follower != nullptr) {
    // keep next before Notify current follower, since when follower being
    // notified, it will be dcor
    GroupWriter *next = follower->newer_;
    follower->NotifyState(GroupRole::COMPLETE);
    follower = next;
    aweak_follower_cnt++;
  }
  assert(aweak_follower_cnt == leader->group_size_ - 1);
}

void DoWrite(int fd, int id) {
  GroupWriter writer(fd, id);

  if (JoinGroup(&writer)) {
    writer.role_.store(GroupRole::LEADER);
  } else if (writer.role_ == GroupRole::FOLLOWER) {
    // Follower wait until its state changed
    writer.WaitState();
  }

  if (writer.role_.load() == GroupRole::COMPLETE) {
    return;
  }
  assert(writer.role_.load() == GroupRole::LEADER);
  auto last_writer = writer_list_head.load();
  BatchWrite(last_writer, &writer);

  LeaderExit(last_writer, &writer);
}

void Write(int fd, int id) {
  while (record_cnt.load(std::memory_order_relaxed) > 0) {
    DoWrite(fd, id);
  }
}
}  // namespace group_commit

bool CheckResult(const std::string &file_name) {
  // TODO
  auto file_size = [file_name]() {
    struct stat sbuf;
    if (::stat(file_name.c_str(), &sbuf) != 0) {
      std::cerr << "while stat a file for size, name: " << file_name
                << std::endl;
      return decltype(sbuf.st_size)(-1);
    } else {
      return sbuf.st_size;
    }
  };

  std::cout << "FileSize: " << file_size() << std::endl;
}

int main(int argc, char **argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  using namespace std::chrono;
  auto start = system_clock::now();

  const std::string log_file_name = "my.log";
  int log_file_fd;
  if (FLAGS_enable_direct) {
    log_file_fd =
        ::open(log_file_name.c_str(), O_RDWR | O_CREAT | O_APPEND | O_DIRECT);
  } else {
    log_file_fd = ::open(log_file_name.c_str(), O_RDWR | O_CREAT | O_APPEND);
  }
  record_cnt.store(FLAGS_records);
  std::vector<std::thread> workers;

  if (FLAGS_enable_group_commit) {
    for (int i = 0; i < FLAGS_writers; i++) {
      workers.emplace_back(group_commit::Write, log_file_fd, i);
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

  CheckResult(log_file_name);

  return 0;
}
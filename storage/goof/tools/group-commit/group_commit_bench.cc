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
enum class GroupRole { INIT, LEADER, FOLLOWER, COMPLETE };
class GroupWriter : public Writer {
 public:
  GroupWriter(int logfd, int id)
      : Writer(logfd), role_(GroupRole::INIT), id_(id) {
    // prepare log data
    std::shuffle(outdata_.begin(), outdata_.end(), g);
  }
  void DoWrite();
  void WaitState();
  void NotifyState(GroupRole r);
  int logfd() { return log_fd_; }

  GroupWriter *older_{nullptr};
  GroupWriter *newer_{nullptr};
  volatile GroupRole role_;
  int id_;

  std::mutex mu_;
  std::condition_variable cv_;

  // if role_ == LEADER
  volatile uint32_t group_size_{1};
};

std::atomic<group_commit::GroupWriter *> writer_list_head{nullptr};

// return true if w join group and become leader
bool JoinGroup(GroupWriter *w) {
  assert(w->older_ == nullptr);
  while (true) {
    if (writer_list_head.compare_exchange_strong(w->older_, w)) {
      return w->older_ == nullptr;
    }
  }
}

void GroupWriter::WaitState() {
  assert(role_ == GroupRole::FOLLOWER);
  //std::cout << "Await: " << id_ << "; " << std::endl;
  std::unique_lock<std::mutex> unique_lock(mu_);
  cv_.wait(unique_lock, [this]() {
    bool not_spurious =
        (role_ == GroupRole::COMPLETE || role_ == GroupRole::LEADER);
    //std::cout << "Aweak: " << id_ << (not_spurious ? " true; " : " false; ")
    //          << std::endl;
    return not_spurious;
  });
}

void GroupWriter::NotifyState(GroupRole r) {
  assert(role_ == GroupRole::FOLLOWER);
  //if (r == GroupRole::LEADER) {
  //  std::cout << "Notify Leader: " << id_ << std::endl;
  //} else {
  //  std::cout << "Notify Complete: " << id_ << std::endl;
  //}
  {
    std::lock_guard<std::mutex> g(mu_);
    role_ = r;
  }
  cv_.notify_one();
}

// Leader collect data and write
void BatchWrite(GroupWriter *tail, GroupWriter *head) {
  assert(head != nullptr && tail != nullptr);
  // fix newer_ pointer and count group size
  if (tail != head) {
    auto prev = tail;
    auto cur = tail->older_;
    while (cur != nullptr && cur != head) {
      cur->newer_ = prev;
      head->group_size_++;

      prev = cur;
      cur = cur->older_;
    }
    assert(cur == nullptr || cur == head);
    if (cur == nullptr) {
      cur = head;
    } else {
      head->newer_ = prev;
      head->group_size_++;
    }
  }

  // batch all data in group
  uint32_t group_data_size = 4 + 10 * head->group_size_;
  char outbuf[group_data_size];
  {
    auto mem_offset = &outbuf[0] + 4;
    auto cur = head;
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
  std::cout << "BatchWrite GroupSize: " << head->group_size_ << std::endl;
}

void LeaderExit(GroupWriter *last_writer, GroupWriter *leader) {
  assert(last_writer != nullptr && leader != nullptr);
  GroupWriter *new_leader = last_writer;
  bool no_pending_writer =
      writer_list_head.compare_exchange_strong(new_leader, nullptr);
  if (!no_pending_writer) {
    // there are new writers and aweak new leader
    assert(new_leader != nullptr && new_leader != last_writer);
    while (new_leader->older_ != last_writer && new_leader->older_ != nullptr) {
      new_leader = new_leader->older_;
    }
    assert(new_leader->older_ == last_writer || new_leader->older_ == nullptr);
    new_leader->NotifyState(GroupRole::LEADER);
  }

  // aweak follower
  uint32_t aweak_follower_cnt = 0;
  while (last_writer != leader) {
    last_writer->NotifyState(GroupRole::COMPLETE);
    aweak_follower_cnt++;
    last_writer = last_writer->older_;
  }
  assert(aweak_follower_cnt == leader->group_size_ - 1);
}

void DoWrite(int fd, int id) {
  GroupWriter writer(fd, id);

  if (JoinGroup(&writer)) {
    writer.role_ = GroupRole::LEADER;
  } else {
    writer.role_ = GroupRole::FOLLOWER;
    // Follower wait until leader done
    writer.WaitState();
  }

  if (writer.role_ == GroupRole::COMPLETE) {
    return;
  }
  assert(writer.role_ == GroupRole::LEADER);
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
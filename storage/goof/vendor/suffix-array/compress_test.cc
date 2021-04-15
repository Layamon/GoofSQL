#include <fcntl.h>
#include <gtest/gtest.h>
#include <openssl/md5.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <random>

namespace ctest {
using namespace testing;
class RandomFileGenerator {
 public:
  std::string operator()() {
    std::random_device rand;
    std::mt19937_64 gen(rand());
    std::uniform_int_distribution<> record_count_dist(900, 1100);
    std::uniform_int_distribution<> val_length_dist(6, 18);
    // use 0 as end-of-word
    std::uniform_int_distribution<char> byte_dist(1, 255);

    std::string file_name = "input-" + std::to_string(byte_dist(gen));
    int output_fd =
        ::open(file_name.c_str(), O_RDWR | O_CREAT | O_APPEND | O_DIRECT);

    size_t record_count = record_count_dist(gen);
    for (size_t i = 0; i < record_count; i++) {
      int val_len = val_length_dist(gen);
      std::string bytes;
      for (size_t j = 0; j < val_len; j++) {
        bytes.push_back(byte_dist(gen));
      }
      bytes.push_back(0);
      ::write(output_fd, (void *)bytes.c_str(), bytes.size());
    }
    ::close(output_fd);
    return file_name;
  }
};

TEST(HelloWordTest, InputOutputTest) {
  RandomFileGenerator rand_file;
  std::string text = rand_file();
  auto get_md5 = [](const std::string &file_name, unsigned char *result) {
    int fd = ::open(file_name.c_str(), O_RDWR | O_DIRECT);
    struct stat statbuf;
    if (fstat(fd, &statbuf) < 0) exit(-1);

    auto file_size = statbuf.st_size;

    auto file_buffer = mmap(0, file_size, PROT_READ, MAP_SHARED, fd, 0);
    ::MD5((unsigned char *)file_buffer, file_size, result);
    munmap(file_buffer, file_size);
  };

  unsigned char filemd5[MD5_DIGEST_LENGTH];
  get_md5(text, filemd5);

  // compress

  // decompress

  unsigned char filemd5_[MD5_DIGEST_LENGTH];
  get_md5(text, filemd5_);
  ASSERT_EQ(filemd5, filemd5_);
}
}  // namespace ctest

int main(int argc, char *argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
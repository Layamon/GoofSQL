#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <memory>

#include "sa_compress.h"
namespace sa {
SACompress::SACompress(/* args */) {}

SACompress::~SACompress() {}

void SACompress::BuildDict(std::string filename) {
  size_t file_size = 0;
  auto get_filebuffer = [&file_size](const std::string &file_name) {
    int fd = ::open(file_name.c_str(), O_RDWR | O_DIRECT);
    struct stat statbuf;
    if (fstat(fd, &statbuf) < 0) exit(-1);

    file_size = statbuf.st_size;

    auto file_buffer = mmap(0, file_size, PROT_READ, MAP_SHARED, fd, 0);
    auto my_deleter = [file_size_ = file_size](char *filebuf) {
      munmap((void *)filebuf, file_size_);
    };
    return std::unique_ptr<char, decltype(my_deleter)>((char *)file_buffer,
                                                       my_deleter);
  };
  auto filebuf = get_filebuffer(filename);

  size_t i = 0;
  while (i < file_size) {
    auto seek_value_end= [&i, &filebuf]() {
      while (filebuf.get()[i] != 0) {
        ++i;
      }
    };
    
    
  }
}
}  // namespace sa
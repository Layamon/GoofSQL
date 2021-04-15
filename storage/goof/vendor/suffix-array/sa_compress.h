#pragma once
#include <string>

namespace sa {

class SACompress {
 private:
  std::string sa_dict_file;

 public:
  SACompress();
  ~SACompress();

  void BuildDict(std::string file_name);
};
}  // namespace sa
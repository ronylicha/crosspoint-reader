#pragma once

// In-memory card for the cover pack tests: files are byte vectors, handles
// share them, and a few switches inject the failures the pack must survive.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using oflag_t = int;
#ifndef O_RDONLY
#define O_RDONLY 0
#endif
#ifndef O_RDWR
#define O_RDWR 2
#endif

namespace fake {
using Bytes = std::shared_ptr<std::vector<uint8_t>>;
inline std::map<std::string, Bytes> files;
inline bool failAlloc = false;
inline bool failReplace = false;
inline unsigned opens = 0;

inline void reset() {
  files.clear();
  failAlloc = false;
  failReplace = false;
  opens = 0;
}
}  // namespace fake

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(fake::Bytes bytes) : data(std::move(bytes)) {}

  explicit operator bool() const { return data != nullptr; }
  size_t fileSize() const { return data ? data->size() : 0; }
  bool seekSet(const size_t offset) {
    if (!data || offset > data->size()) return false;
    pos = offset;
    return true;
  }
  int read(void* dst, const size_t count) {
    if (!data) return -1;
    const size_t n = std::min(count, data->size() - pos);
    memcpy(dst, data->data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }
  size_t write(const void* src, const size_t count) {
    if (!data) return 0;
    if (pos + count > data->size()) data->resize(pos + count);
    memcpy(data->data() + pos, src, count);
    pos += count;
    return count;
  }
  bool close() {
    data.reset();
    return true;
  }

 private:
  fake::Bytes data;
  size_t pos = 0;
};

class HalStorage {
 public:
  bool exists(const char* path) const { return fake::files.count(path) != 0; }
  bool mkdir(const char*) { return true; }
  bool remove(const char* path) { return fake::files.erase(path) != 0; }
  bool replaceFile(const char* tmp, const char* path) {
    if (fake::failReplace || !exists(tmp)) return false;
    fake::files[path] = fake::files[tmp];
    fake::files.erase(tmp);
    return true;
  }
  HalFile open(const char* path, const oflag_t) {
    const auto it = fake::files.find(path);
    if (it == fake::files.end()) return HalFile();
    ++fake::opens;
    return HalFile(it->second);
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    file = open(path.c_str(), O_RDONLY);
    return static_cast<bool>(file);
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    fake::files[path] = std::make_shared<std::vector<uint8_t>>();
    ++fake::opens;
    file = HalFile(fake::files[path]);
    return true;
  }
};

inline HalStorage Storage;

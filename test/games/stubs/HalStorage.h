#pragma once

#include "HostRuntime.h"

namespace host {
struct FileSystem {
  std::map<std::string, std::vector<uint8_t>> files;
  bool ready = true;
  bool failDirectory = false, failOpenRead = false, failOpenWrite = false;
  bool shortRead = false, shortWrite = false, failClose = false, failRemove = false;
  bool failRename = false, failReplace = false;
  int writes = 0;
};
inline FileSystem fs;
}  // namespace host
class HalFile {
  std::string path;
  bool opened = false;

 public:
  void open(const char* value) {
    path = value;
    opened = true;
  }
  bool isDirectory() const { return false; }
  size_t size() const { return host::fs.files.at(path).size(); }
  int read(void* out, size_t count) {
    const auto& data = host::fs.files.at(path);
    count = std::min(count, data.size());
    if (host::fs.shortRead && count) --count;
    memcpy(out, data.data(), count);
    return static_cast<int>(count);
  }
  size_t write(const void* data, size_t count) {
    ++host::fs.writes;
    if (host::fs.shortWrite && count) --count;
    const auto* bytes = static_cast<const uint8_t*>(data);
    host::fs.files[path].assign(bytes, bytes + count);
    return count;
  }
  void flush() {}
  bool close() {
    opened = false;
    return !host::fs.failClose;
  }
};
class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  bool ready() const { return host::fs.ready; }
  bool exists(const char* path) const { return host::fs.files.count(path) != 0; }
  bool ensureDirectoryExists(const char*) const { return !host::fs.failDirectory; }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (host::fs.failOpenRead || !exists(path)) return false;
    file.open(path);
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    if (host::fs.failOpenWrite) return false;
    host::fs.files[path].clear();
    file.open(path);
    return true;
  }
  bool remove(const char* path) {
    if (host::fs.failRemove) return false;
    return host::fs.files.erase(path) != 0;
  }
  bool rename(const char* from, const char* to) {
    if (host::fs.failRename || !exists(from) || exists(to)) return false;
    auto node = host::fs.files.extract(from);
    node.key() = to;
    host::fs.files.insert(std::move(node));
    return true;
  }
  bool replaceFile(const char* from, const char* to) {
    // HAL removes the old target before rename. Fault injection intentionally
    // leaves source intact but destination absent, as a power loss can do.
    host::fs.files.erase(to);
    if (host::fs.failReplace || !exists(from)) return false;
    auto node = host::fs.files.extract(from);
    node.key() = to;
    host::fs.files.insert(std::move(node));
    return true;
  }
};
#define Storage HalStorage::getInstance()

#pragma once

#include <map>
#include <optional>
#include <string>

#include "HalStorage.h"

struct FakeMetadata {
  std::string title = "Title";
  std::string author = "Author";
  bool success = true;
  std::string series = {};
  std::optional<float> seriesIndex = std::nullopt;
};

inline std::map<std::string, FakeMetadata> bookMetadata;

class Epub {
  std::string path;

 public:
  struct SyncMetadata {
    std::string title;
    std::string author;
    std::string isbn;
    std::string asin;
    std::string series;
    std::optional<float> seriesIndex;
  };

  Epub(const std::string& path, const char*) : path(path) {}

  bool loadMetadata(std::string& title, std::string& author) {
    ++fake::parses;
    const auto& metadata = bookMetadata[path];
    if (!metadata.success) return false;
    title = metadata.title;
    author = metadata.author;
    return true;
  }

  bool loadSyncMetadata(SyncMetadata& metadata) {
    metadata = {};
    ++fake::parses;
    const auto& source = bookMetadata[path];
    if (!source.success) return false;
    metadata.title = source.title;
    metadata.author = source.author;
    metadata.series = source.series;
    metadata.seriesIndex = source.seriesIndex;
    return true;
  }
};

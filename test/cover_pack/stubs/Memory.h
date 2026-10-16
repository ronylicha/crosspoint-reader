#pragma once

#include <memory>
#include <type_traits>

#include "HalStorage.h"

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (fake::failAlloc) return nullptr;
  return std::make_unique<T>(count);
}

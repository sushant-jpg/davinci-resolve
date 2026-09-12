#pragma once
#include "core/Project.h"
#include <atomic>
namespace sentinel {
class MediaProbe {
  public:
    static Media probe(const QString& path, const std::atomic_bool* cancel = nullptr);
    static QString executable(const QString& name);
};
} // namespace sentinel

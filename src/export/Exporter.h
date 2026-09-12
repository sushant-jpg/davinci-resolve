#pragma once
#include "core/Project.h"
#include <atomic>
#include <functional>
namespace sentinel {
class Exporter {
  public:
    // Called on a worker thread. Callback runs on that same thread.
    static void render(const Project& project, const QString& destination, const std::atomic_bool& cancel,
                       const std::function<void(int, const QString&)>& progress = {});
};
} // namespace sentinel

#pragma once
#include "core/Project.h"
#include <QByteArray>
namespace sentinel {
class ProjectIO {
  public:
    static QByteArray encode(const Project& project);
    static Project decode(const QByteArray& bytes);
    static Project load(const QString& path);
    static void save(const Project& project, const QString& path, bool backup = true);
};
} // namespace sentinel

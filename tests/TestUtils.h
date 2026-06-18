#pragma once

#include <QDebug>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <cstdio>
#include <cstdlib>
#include <functional>

inline void require(bool condition, const char* message) {
  if (!condition) {
    qCritical().noquote() << message;
    fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

inline void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    std::exit(1);
  }
}

inline void runTest(const char* name, void (*test)()) {
  qInfo().noquote() << QStringLiteral("RUN %1").arg(QString::fromUtf8(name));
  test();
}

inline void runTest(const char* name, const std::function<void()>& test) {
  qInfo().noquote() << QStringLiteral("RUN %1").arg(QString::fromUtf8(name));
  test();
}

// RAII guard that sets a QSettings key for the duration of a scope and restores (or removes) the
// prior value on destruction. QSettings is process-global, so tests that exercise a preference
// (editor/*, markdown/*, ...) must restore the default afterwards or they poison every later test
// run in the same process.
class SettingsOverride {
public:
  SettingsOverride(const char* key, const QVariant& value) : key_(QString::fromLatin1(key)) {
    QSettings settings;
    hadOld_ = settings.contains(key_);
    oldValue_ = settings.value(key_);
    settings.setValue(key_, value);
  }
  ~SettingsOverride() {
    QSettings settings;
    if (hadOld_) {
      settings.setValue(key_, oldValue_);
    } else {
      settings.remove(key_);
    }
  }
  SettingsOverride(const SettingsOverride&) = delete;
  SettingsOverride& operator=(const SettingsOverride&) = delete;

private:
  QString key_;
  QVariant oldValue_;
  bool hadOld_ = false;
};

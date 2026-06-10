#pragma once

#include <QDebug>
#include <QString>

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

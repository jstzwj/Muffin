#include "unicode/IcuRuntime.h"
#include "IcuDataConfig.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <unicode/uclean.h>
#include <unicode/udata.h>
#include <mutex>

namespace {
struct RuntimeData {
  QFile file;
  QString error;
  bool initialized = false;
  std::once_flag once;
};
RuntimeData& runtimeData() {
  static RuntimeData data;
  return data;
}
} // namespace

QString muffin::icuDataFilePath() {
  const QDir executable(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
  if (executable.dirName() == QStringLiteral("MacOS"))
    return executable.filePath(QStringLiteral("../Resources/icu/") + QStringLiteral(MUFFIN_ICU_DATA_FILE));
#endif
  return executable.filePath(QStringLiteral("resources/icu/") + QStringLiteral(MUFFIN_ICU_DATA_FILE));
}

bool muffin::initializeIcuData(QString* error) {
  auto& data = runtimeData();
  std::call_once(data.once, [&data] {
    data.file.setFileName(icuDataFilePath());
    if (!data.file.open(QIODevice::ReadOnly)) {
      data.error = data.file.fileName() + QStringLiteral(": ") + data.file.errorString();
      return;
    }
    if (data.file.size() != MUFFIN_ICU_DATA_BYTES) {
      data.error = data.file.fileName() + QStringLiteral(": unexpected ICU data size");
      return;
    }
    auto* mapping = data.file.map(0, data.file.size());
    if (!mapping) {
      data.error = data.file.fileName() + QStringLiteral(": ") + data.file.errorString();
      return;
    }
    const auto bytes = QByteArrayView(reinterpret_cast<const char*>(mapping), data.file.size());
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != MUFFIN_ICU_DATA_SHA256) {
      data.error = data.file.fileName() + QStringLiteral(": ICU data checksum mismatch");
      data.file.unmap(mapping);
      return;
    }
    UErrorCode status = U_ZERO_ERROR;
    udata_setCommonData(mapping, &status);
    if (U_SUCCESS(status))
      udata_setFileAccess(UDATA_NO_FILES, &status);
    if (U_SUCCESS(status))
      u_init(&status);
    if (U_FAILURE(status)) {
      // Once registered, ICU can retain pointers even after failed u_init.
      // QFile owns the mapping; keep it alive through shutdown in either case.
      data.error = data.file.fileName() + QStringLiteral(": ") + QString::fromLatin1(u_errorName(status));
      return;
    }
    data.initialized = true;
  });
  if (error)
    *error = data.error;
  return data.initialized;
}

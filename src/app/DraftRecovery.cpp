#include "app/DraftRecovery.h"
#include "diagnostics/ScopedPerfProbe.h"
#include "document/DocumentSession.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include "io/ApplicationPaths.h"
#include <QUuid>

#include <QSet>

#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(draftPerf, "muffin.perf.detail", QtWarningMsg)

// True for names produced by DraftRecovery::keyFor(): "untitled" or a 16-char
// lowercase hex digest. pruneOrphaned() only touches files whose stem matches,
// so stray files a user may have placed in the drafts directory are left alone.
bool isDraftKey(const QString& stem) {
  if (stem == QStringLiteral("untitled")) {
    return true;
  }
  if (stem.size() != 16) {
    return false;
  }
  for (const QChar c : stem) {
    const bool hex = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
    if (!hex) {
      return false;
    }
  }
  return true;
}

const QByteArray snapshotMagic = QByteArrayLiteral("MUFFIN-DRAFT 1\n");

bool readSnapshotHeader(QFile& file, QJsonObject& meta) {
  if (!file.open(QIODevice::ReadOnly) || file.read(snapshotMagic.size()) != snapshotMagic)
    return false;
  const QByteArray header = file.readLine(65537);
  if (!header.endsWith('\n') || header.size() > 65536)
    return false;
  const auto json = QJsonDocument::fromJson(header);
  if (!json.isObject())
    return false;
  meta = json.object();
  const qint64 bytes = meta.value(QStringLiteral("bytes")).toInteger(-1);
  return bytes >= 0 && bytes == file.size() - file.pos();
}

} // namespace

namespace muffin {

DraftRecovery::DraftRecovery(QString directory) : directory_(std::move(directory)) {
  if (directory_.isEmpty()) {
    directory_ = ApplicationPaths::dataDirectory();
    if (!directory_.isEmpty()) {
      directory_ += QLatin1String("/drafts");
    }
  }
}

QString DraftRecovery::createDraftKey() {
  const QByteArray hash =
      QCryptographicHash::hash(QUuid::createUuid().toByteArray(QUuid::WithoutBraces), QCryptographicHash::Sha256);
  return QString::fromLatin1(hash.toHex().left(16));
}

QString DraftRecovery::keyFor(const QString& sourceFilePath, const QString& draftKey) const {
  if (isDraftKey(draftKey) && draftKey != QStringLiteral("untitled")) {
    return draftKey;
  }
  if (sourceFilePath.isEmpty()) {
    return QStringLiteral("untitled");
  }
  const QByteArray hash =
      QCryptographicHash::hash(QFileInfo(sourceFilePath).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256);
  return QString::fromLatin1(hash.toHex().left(16));
}

QString DraftRecovery::draftPath(const QString& key) const {
  return directory_ + QLatin1Char('/') + key + QStringLiteral(".md");
}

QString DraftRecovery::metaPath(const QString& key) const {
  return directory_ + QLatin1Char('/') + key + QStringLiteral(".meta");
}

QString DraftRecovery::snapshotPath(const QString& key) const {
  return directory_ + QLatin1Char('/') + key + QStringLiteral(".draft");
}

bool DraftRecovery::snapshot(const QString& markdownText, const QString& sourceFilePath, const QString& draftKey) {
  muffin::diag::ScopedPerfProbe perf("draft.writeSnapshot", draftPerf());
  if (directory_.isEmpty() || !QDir().mkpath(directory_)) {
    return false;
  }
  const QString key = keyFor(sourceFilePath, draftKey);

  const QByteArray bytes = [&] {
    muffin::diag::ScopedPerfProbe perf("draft.encodeUtf8", draftPerf());
    return markdownText.toUtf8();
  }();
  QJsonObject meta;
  meta[QStringLiteral("sourcePath")] = sourceFilePath;
  meta[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
  meta[QStringLiteral("charCount")] = qint64(markdownText.size());
  meta[QStringLiteral("bytes")] = qint64(bytes.size());
  const QByteArray metadata = QJsonDocument(meta).toJson(QJsonDocument::Compact) + '\n';
  if (metadata.size() > 65536)
    return false;
  QSaveFile snapshot(snapshotPath(key));
  if (!snapshot.open(QIODevice::WriteOnly) || snapshot.write(snapshotMagic) != snapshotMagic.size() ||
      snapshot.write(metadata) != metadata.size() || snapshot.write(bytes) != bytes.size() || !snapshot.commit())
    return false;
  // Migrate only after the replacement is durable. A crash before commit
  // leaves the previous snapshot (or legacy pair) intact.
  QFile::remove(draftPath(key));
  QFile::remove(metaPath(key));
  return true;
}

void DraftRecovery::markClean(const QString& sourceFilePath, const QString& draftKey) {
  const QString key = keyFor(sourceFilePath, draftKey);
  QFile::remove(snapshotPath(key));
  QFile::remove(draftPath(key));
  QFile::remove(metaPath(key));
}

QVector<DraftRecovery::PendingDraft> DraftRecovery::pendingDrafts() const {
  QVector<PendingDraft> drafts;
  QDir dir(directory_);
  if (!dir.exists()) {
    return drafts;
  }
  QSet<QString> loaded;
  for (const QString& name :
       dir.entryList({QStringLiteral("*.draft"), QStringLiteral("*.md")}, QDir::Files, QDir::Name)) {
    const bool atomic = name.endsWith(QStringLiteral(".draft"));
    const QString key = name.left(name.size() - (atomic ? 6 : 3));
    if (!isDraftKey(key) || loaded.contains(key))
      continue;
    QJsonObject meta;
    QFile content(dir.absoluteFilePath(name));
    if (atomic) {
      if (!readSnapshotHeader(content, meta))
        continue;
    } else {
      QFile metadata(metaPath(key));
      if (metadata.open(QIODevice::ReadOnly))
        meta = QJsonDocument::fromJson(metadata.read(65536)).object();
      if (meta.isEmpty()) {
        // Legacy crash between content and metadata writes. Preserve the
        // only surviving user data even though its original path is unknown.
        if (!content.open(QIODevice::ReadOnly))
          continue;
        meta[QStringLiteral("charCount")] = qint64(QString::fromUtf8(content.readAll()).size());
        meta[QStringLiteral("timestamp")] = QFileInfo(content).lastModified().toMSecsSinceEpoch();
      }
    }
    PendingDraft d;
    d.key = key;
    d.atomicSnapshot = atomic;
    d.sourcePath = meta.value(QStringLiteral("sourcePath")).toString();
    d.timestamp = qint64(meta.value(QStringLiteral("timestamp")).toDouble());
    d.charCount = qsizetype(meta.value(QStringLiteral("charCount")).toDouble());
    drafts.append(std::move(d));
    loaded.insert(key);
  }
  // Newest first, so the recovery dialog lists the most recent draft on top.
  std::sort(drafts.begin(), drafts.end(),
            [](const PendingDraft& a, const PendingDraft& b) { return a.timestamp > b.timestamp; });
  return drafts;
}

QString DraftRecovery::loadDraft(const PendingDraft& draft, bool* ok) const {
  if (ok) {
    *ok = false;
  }
  if (!isDraftKey(draft.key))
    return {};
  QFile f(draft.atomicSnapshot ? snapshotPath(draft.key) : draftPath(draft.key));
  QJsonObject meta;
  if (draft.atomicSnapshot ? !readSnapshotHeader(f, meta) : !f.open(QIODevice::ReadOnly)) {
    return QString();
  }
  const QByteArray bytes = f.readAll();
  if (f.error() != QFileDevice::NoError) {
    return {};
  }
  if (ok) {
    *ok = true;
  }
  return QString::fromUtf8(bytes);
}

void DraftRecovery::discard(const PendingDraft& draft) {
  if (!isDraftKey(draft.key))
    return;
  QFile::remove(snapshotPath(draft.key));
  QFile::remove(draftPath(draft.key));
  QFile::remove(metaPath(draft.key));
}

void DraftRecovery::pruneOrphaned() {
  QDir dir(directory_);
  if (!dir.exists()) {
    return;
  }
  // Only a sidecar without content is disposable. Content without a sidecar
  // is offered as an untitled recovery document, never pruned.
  QSet<QString> mdKeys, metaKeys;
  for (const QString& f : dir.entryList({QStringLiteral("*.md")}, QDir::Files)) {
    if (isDraftKey(f.left(f.size() - 3))) {
      mdKeys.insert(f.left(f.size() - 3));
    }
  }
  for (const QString& f : dir.entryList({QStringLiteral("*.meta")}, QDir::Files)) {
    if (isDraftKey(f.left(f.size() - 5))) {
      metaKeys.insert(f.left(f.size() - 5));
    }
  }
  for (const QString& key : metaKeys - mdKeys) {
    QFile::remove(metaPath(key));
  }
}

} // namespace muffin

bool muffin::DraftRecovery::snapshotSession(const DocumentSession& session, const QString& draftKey) {
  muffin::diag::ScopedPerfProbe perf("draft.snapshotSession", draftPerf());
  if (session.isOpenInProgress() || !session.document().isModified()) {
    return false;
  }
  const QString text = [&] {
    muffin::diag::ScopedPerfProbe perf("draft.materialize", draftPerf());
    return session.markdownText().toString();
  }();
  return snapshot(text, session.filePath(), draftKey);
}

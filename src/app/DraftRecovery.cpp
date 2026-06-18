#include "app/DraftRecovery.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <QSet>

#include <algorithm>

namespace {

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

}  // namespace

namespace muffin {

DraftRecovery::DraftRecovery(QString directory) : directory_(std::move(directory)) {
  if (directory_.isEmpty()) {
    directory_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!directory_.isEmpty()) {
      directory_ += QLatin1String("/drafts");
    }
  }
}

QString DraftRecovery::keyFor(const QString& sourceFilePath) const {
  if (sourceFilePath.isEmpty()) {
    return QStringLiteral("untitled");
  }
  const QByteArray hash = QCryptographicHash::hash(
      QFileInfo(sourceFilePath).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256);
  return QString::fromLatin1(hash.toHex().left(16));
}

QString DraftRecovery::draftPath(const QString& key) const {
  return directory_ + QLatin1Char('/') + key + QStringLiteral(".md");
}

QString DraftRecovery::metaPath(const QString& key) const {
  return directory_ + QLatin1Char('/') + key + QStringLiteral(".meta");
}

void DraftRecovery::snapshot(const QString& markdownText, const QString& sourceFilePath) {
  if (markdownText.isEmpty()) {
    return;
  }
  if (directory_.isEmpty() || !QDir().mkpath(directory_)) {
    return;
  }
  const QString key = keyFor(sourceFilePath);

  // Content + sidecar meta are written independently (each via QSaveFile, which is
  // atomic on its own). pendingDrafts() only lists a draft when its .md survives.
  QSaveFile content(draftPath(key));
  if (content.open(QIODevice::WriteOnly)) {
    content.write(markdownText.toUtf8());
    content.commit();
  }

  QJsonObject meta;
  meta[QStringLiteral("sourcePath")] = sourceFilePath;
  meta[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
  meta[QStringLiteral("charCount")] = qint64(markdownText.size());
  QSaveFile metaFile(metaPath(key));
  if (metaFile.open(QIODevice::WriteOnly)) {
    metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    metaFile.commit();
  }
}

void DraftRecovery::markClean(const QString& sourceFilePath) {
  const QString key = keyFor(sourceFilePath);
  QFile::remove(draftPath(key));
  QFile::remove(metaPath(key));
}

QVector<DraftRecovery::PendingDraft> DraftRecovery::pendingDrafts() const {
  QVector<PendingDraft> drafts;
  QDir dir(directory_);
  if (!dir.exists()) {
    return drafts;
  }
  const QStringList metas = dir.entryList({QStringLiteral("*.meta")}, QDir::Files);
  for (const QString& metaFile : metas) {
    QFile f(dir.absoluteFilePath(metaFile));
    if (!f.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QJsonObject meta = QJsonDocument::fromJson(f.readAll()).object();
    PendingDraft d;
    d.key = metaFile.left(metaFile.size() - 5);  // strip ".meta"
    d.sourcePath = meta.value(QStringLiteral("sourcePath")).toString();
    d.timestamp = qint64(meta.value(QStringLiteral("timestamp")).toDouble());
    d.charCount = qsizetype(meta.value(QStringLiteral("charCount")).toDouble());
    if (QFile::exists(draftPath(d.key))) {
      drafts.append(std::move(d));
    }
  }
  // Newest first, so the recovery dialog lists the most recent draft on top.
  std::sort(drafts.begin(), drafts.end(),
            [](const PendingDraft& a, const PendingDraft& b) { return a.timestamp > b.timestamp; });
  return drafts;
}

QString DraftRecovery::loadDraft(const PendingDraft& draft) const {
  QFile f(draftPath(draft.key));
  if (!f.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromUtf8(f.readAll());
}

void DraftRecovery::discard(const PendingDraft& draft) {
  QFile::remove(draftPath(draft.key));
  QFile::remove(metaPath(draft.key));
}

void DraftRecovery::pruneOrphaned() {
  QDir dir(directory_);
  if (!dir.exists()) {
    return;
  }
  // 1. Discard complete drafts whose source file is gone (deleted/moved).
  for (const PendingDraft& d : pendingDrafts()) {
    if (!d.sourcePath.isEmpty() && !QFileInfo(d.sourcePath).isFile()) {
      discard(d);
    }
  }
  // 2. Remove half-written pairs left by a crash mid-snapshot: a .meta whose
  //    .md never landed, or a .md whose .meta never landed. Only stems that look
  //    like real draft keys are touched, to protect unrelated files in the dir.
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
  for (const QString& key : mdKeys - metaKeys) {
    QFile::remove(draftPath(key));
  }
}

}  // namespace muffin

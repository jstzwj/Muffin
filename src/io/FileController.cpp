#include "io/FileController.h"

#include "document/DocumentSession.h"

#include <QFile>
#include <QDir>
#include <QSettings>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QSaveFile>
#include <QStringConverter>
#include <QWidget>

#include <unicode/ucnv.h>
#include <unicode/ucnv_err.h>
#include <unicode/ucsdet.h>

namespace {

bool decodeWithIcu(const QByteArray& raw, const QString& encodingName, QString* out) {
  UErrorCode status = U_ZERO_ERROR;
  UConverter* converter = ucnv_open(encodingName.toUtf8().constData(), &status);
  if (U_FAILURE(status)) { return false; }
  ucnv_setToUCallBack(converter, UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
  if (U_FAILURE(status)) {
    ucnv_close(converter);
    return false;
  }
  const int32_t capacity = ucnv_toUChars(
      converter, nullptr, 0, raw.constData(), raw.size(), &status);
  if (status == U_BUFFER_OVERFLOW_ERROR) { status = U_ZERO_ERROR; }
  if (U_FAILURE(status)) {
    ucnv_close(converter);
    return false;
  }
  QVector<UChar> buffer(capacity + 1);
  const int32_t length = ucnv_toUChars(
      converter, buffer.data(), buffer.size(), raw.constData(), raw.size(), &status);
  ucnv_close(converter);
  if (U_FAILURE(status)) { return false; }
  *out = QString(reinterpret_cast<const QChar*>(buffer.constData()), length);
  return true;
}

bool encodeWithIcu(const QString& text, const QString& encodingName, QByteArray* out) {
  UErrorCode status = U_ZERO_ERROR;
  UConverter* converter = ucnv_open(encodingName.toUtf8().constData(), &status);
  if (U_FAILURE(status)) { return false; }
  ucnv_setFromUCallBack(converter, UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
  if (U_FAILURE(status)) {
    ucnv_close(converter);
    return false;
  }
  const auto* source = reinterpret_cast<const UChar*>(text.utf16());
  const int32_t capacity = ucnv_fromUChars(converter, nullptr, 0, source, text.size(), &status);
  if (status == U_BUFFER_OVERFLOW_ERROR) { status = U_ZERO_ERROR; }
  if (U_FAILURE(status)) {
    ucnv_close(converter);
    return false;
  }
  QByteArray encoded(capacity, Qt::Uninitialized);
  const int32_t length = ucnv_fromUChars(
      converter, encoded.data(), encoded.size(), source, text.size(), &status);
  ucnv_close(converter);
  if (U_FAILURE(status)) { return false; }
  encoded.resize(length);
  *out = std::move(encoded);
  return true;
}

muffin::TextLineEnding detectLineEnding(QStringView text) {
  qsizetype crlf = 0;
  qsizetype lf = 0;
  qsizetype cr = 0;
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text.at(i) == QLatin1Char('\r')) {
      if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
        ++crlf;
        ++i;
      } else {
        ++cr;
      }
    } else if (text.at(i) == QLatin1Char('\n')) {
      ++lf;
    }
  }
  if (crlf >= lf && crlf >= cr && crlf > 0) { return muffin::TextLineEnding::Crlf; }
  if (cr > lf && cr > 0) { return muffin::TextLineEnding::Cr; }
  return muffin::TextLineEnding::Lf;
}

void normalizeLineEndings(QString* text) {
  text->replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text->replace(QLatin1Char('\r'), QLatin1Char('\n'));
}

bool looksLikeWesternSingleByteText(const QByteArray& bytes) {
  if (bytes.isEmpty()) return false;
  qsizetype printableAscii = 0;
  qsizetype highBytes = 0;
  for (const unsigned char byte : bytes) {
    if (byte == 0) return false;
    if (byte >= 0x80) {
      ++highBytes;
    } else if (byte == '\t' || byte == '\n' || byte == '\r' ||
               (byte >= 0x20 && byte <= 0x7e)) {
      ++printableAscii;
    }
  }
  // Short Western documents are fundamentally ambiguous to statistical detectors. A mostly
  // ASCII byte stream with sparse high bytes is the common Windows-1252 shape; East Asian
  // multibyte encodings have a materially higher high-byte density and continue through ICU.
  return highBytes > 0 && printableAscii * 4 >= bytes.size() * 3 &&
      highBytes * 4 <= bytes.size();
}

bool decodeWindows1252(const QByteArray& bytes, QString* text) {
  static constexpr char16_t kC1Mapping[32] = {
      0x20ac, 0x0000, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
      0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017d, 0x0000,
      0x0000, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
      0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x0000, 0x017e, 0x0178};

  QString decoded;
  decoded.reserve(bytes.size());
  for (const unsigned char byte : bytes) {
    if (byte >= 0x80 && byte <= 0x9f) {
      const char16_t mapped = kC1Mapping[byte - 0x80];
      if (mapped == 0) return false;  // Undefined Windows-1252 byte: preserve lossless detection.
      decoded.append(QChar(mapped));
    } else {
      decoded.append(QChar(byte));
    }
  }
  *text = std::move(decoded);
  return true;
}

bool encodeWindows1252(QStringView text, QByteArray* bytes) {
  static const QHash<char16_t, unsigned char> kSpecialBytes = {
      {0x20ac, 0x80}, {0x201a, 0x82}, {0x0192, 0x83}, {0x201e, 0x84},
      {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87}, {0x02c6, 0x88},
      {0x2030, 0x89}, {0x0160, 0x8a}, {0x2039, 0x8b}, {0x0152, 0x8c},
      {0x017d, 0x8e}, {0x2018, 0x91}, {0x2019, 0x92}, {0x201c, 0x93},
      {0x201d, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97},
      {0x02dc, 0x98}, {0x2122, 0x99}, {0x0161, 0x9a}, {0x203a, 0x9b},
      {0x0153, 0x9c}, {0x017e, 0x9e}, {0x0178, 0x9f}};

  QByteArray encoded;
  encoded.reserve(text.size());
  for (QChar ch : text) {
    const char16_t value = ch.unicode();
    if (value <= 0x7f || (value >= 0x00a0 && value <= 0x00ff)) {
      encoded.append(static_cast<char>(value));
      continue;
    }
    const auto special = kSpecialBytes.constFind(value);
    if (special == kSpecialBytes.cend()) return false;
    encoded.append(static_cast<char>(special.value()));
  }
  *bytes = std::move(encoded);
  return true;
}

bool decodeLegacyText(
    const QByteArray& bytes, const QString& encodingName, QString* text) {
  if (encodingName.compare(QStringLiteral("windows-1252"), Qt::CaseInsensitive) == 0) {
    return decodeWindows1252(bytes, text);
  }
  return decodeWithIcu(bytes, encodingName, text);
}

bool encodeLegacyText(const QString& text, const QString& encodingName, QByteArray* bytes) {
  if (encodingName.compare(QStringLiteral("windows-1252"), Qt::CaseInsensitive) == 0) {
    return encodeWindows1252(text, bytes);
  }
  return encodeWithIcu(text, encodingName, bytes);
}

bool decodeText(const QByteArray& raw, const QString& requestedEncoding,
                QString* text, muffin::TextFileFormat* format) {
  QByteArray payload = raw;
  QString encoding = requestedEncoding;
  bool bom = false;

  struct BomEntry {
    QByteArray bytes;
    QString name;
    QStringConverter::Encoding qtEncoding;
  };
  const BomEntry bomEntries[] = {
      {QByteArray::fromHex("0000feff"), QStringLiteral("UTF-32BE"), QStringConverter::Utf32BE},
      {QByteArray::fromHex("fffe0000"), QStringLiteral("UTF-32LE"), QStringConverter::Utf32LE},
      {QByteArray::fromHex("efbbbf"), QStringLiteral("UTF-8"), QStringConverter::Utf8},
      {QByteArray::fromHex("feff"), QStringLiteral("UTF-16BE"), QStringConverter::Utf16BE},
      {QByteArray::fromHex("fffe"), QStringLiteral("UTF-16LE"), QStringConverter::Utf16LE},
  };
  for (const BomEntry& entry : bomEntries) {
    if (payload.startsWith(entry.bytes)) {
      bom = true;
      encoding = entry.name;
      payload.remove(0, entry.bytes.size());
      QStringDecoder decoder(entry.qtEncoding);
      *text = decoder(payload);
      if (decoder.hasError()) { return false; }
      break;
    }
  }

  if (text->isNull()) {
    if (encoding.isEmpty()) {
      QStringDecoder utf8(QStringConverter::Utf8);
      const QString decoded = utf8(payload);
      if (!utf8.hasError()) {
        encoding = QStringLiteral("UTF-8");
        *text = decoded;
      } else if (looksLikeWesternSingleByteText(payload) && decodeWindows1252(payload, text)) {
        encoding = QStringLiteral("windows-1252");
      } else {
        UErrorCode status = U_ZERO_ERROR;
        UCharsetDetector* detector = ucsdet_open(&status);
        if (U_FAILURE(status)) { return false; }
        ucsdet_setText(detector, payload.constData(), payload.size(), &status);
        const UCharsetMatch* match = U_SUCCESS(status) ? ucsdet_detect(detector, &status) : nullptr;
        const char* detected = match && U_SUCCESS(status) ? ucsdet_getName(match, &status) : nullptr;
        if (detected && U_SUCCESS(status)) { encoding = QString::fromLatin1(detected); }
        ucsdet_close(detector);
        if (encoding.isEmpty() || !decodeWithIcu(payload, encoding, text)) { return false; }
      }
    } else if (!decodeLegacyText(payload, encoding, text)) {
      return false;
    }
  }

  format->encodingName = encoding;
  format->writeBom = bom;
  format->lineEnding = detectLineEnding(*text);
  format->ensureTrailingNewline = text->endsWith(QLatin1Char('\n')) || text->endsWith(QLatin1Char('\r'));
  format->existingFile = true;
  normalizeLineEndings(text);
  return true;
}

QByteArray bomForEncoding(const QString& encoding) {
  const QString upper = encoding.toUpper();
  if (upper == QLatin1String("UTF-8")) { return QByteArray::fromHex("efbbbf"); }
  if (upper == QLatin1String("UTF-16LE")) { return QByteArray::fromHex("fffe"); }
  if (upper == QLatin1String("UTF-16BE")) { return QByteArray::fromHex("feff"); }
  if (upper == QLatin1String("UTF-32LE")) { return QByteArray::fromHex("fffe0000"); }
  if (upper == QLatin1String("UTF-32BE")) { return QByteArray::fromHex("0000feff"); }
  return {};
}

}  // namespace

muffin::FileController::FileController(QObject* parent) : QObject(parent) {}

bool muffin::FileController::newFile(DocumentSession& session, QWidget* parent) {
  autoSaveOnSwitchIfEnabled(session, parent);
  if (!confirmDiscardIfModified(session, parent)) {
    return false;
  }
  session.newDocument();
  return true;
}

bool muffin::FileController::open(DocumentSession& session, QWidget* parent, QString path) {
  if (path.isEmpty()) {
    path = QFileDialog::getOpenFileName(
        parent,
        tr("Open"),
        QString(),
        tr("Markdown and text files (*.md *.markdown *.mdown *.txt);;All files (*.*)"));
  }
  if (path.isEmpty()) {
    return false;
  }

  QString text;
  TextFileFormat format;
  if (!readTextFile(path, &text, &format, parent)) {
    return false;
  }

  // Do not resolve the current document until the target is known to be
  // readable. Canceling the picker or failing to read must leave its dirty
  // state and recovery snapshot untouched.
  autoSaveOnSwitchIfEnabled(session, parent);
  if (!confirmDiscardIfModified(session, parent)) {
    return false;
  }

  session.setFilePath(path);
  session.setFileFormat(format);
  session.recordFileBaseline();  // baseline the file as it was at open (external-change detection)
  session.openDocumentAsync(text);  // async parse keeps the UI responsive on huge files
  return true;
}

muffin::SaveOutcome muffin::FileController::save(DocumentSession& session, QWidget* parent, const QString& defaultDir) {
  // Refuse to persist while an async open parse is in flight: document_ still holds the pre-open
  // text, but filePath_ already points at the new file — writing would clobber it with stale content.
  if (session.isAsyncParseInProgress()) {
    return SaveOutcome::SkippedBusy;
  }
  if (session.filePath().isEmpty()) {
    return saveAs(session, parent, defaultDir);
  }
  if (!confirmOverwriteIfChanged(session, parent)) {
    return SaveOutcome::Failed;  // user declined to overwrite the externally-modified file
  }
  DocumentSession::SelfWriteGuard guard(session);  // absorb our own commit's fileChanged signal
  QString normalizedText;
  if (!writeTextFile(session.filePath(), session.markdownText().toString(),
                     session.fileFormat(), &normalizedText, parent)) {
    return SaveOutcome::Failed;
  }
  if (normalizedText != session.markdownText().toString()) {
    session.setMarkdownText(normalizedText, false);
  } else {
    session.document().setModified(false);
  }
  session.recordFileBaseline();  // re-baseline after our write (2nd line of self-trigger defense)
  emit documentBecameClean(session.filePath());
  return SaveOutcome::Saved;
}

QString muffin::FileController::defaultUntitledName() const {
  // files/defaultExtension: 0 = .md (default), 1 = .markdown, 2 = .txt.
  QSettings settings;
  switch (settings.value(QStringLiteral("files/defaultExtension"), 0).toInt()) {
    case 1:
      return QStringLiteral("Untitled.markdown");
    case 2:
      return QStringLiteral("Untitled.txt");
    default:
      return QStringLiteral("Untitled.md");
  }
}

void muffin::FileController::autoSaveOnSwitchIfEnabled(DocumentSession& session, QWidget* parent) {
  // The internal Save paths below intentionally keep defaultDir = {} — only the
  // explicit Save / Save As commands (which know the sidebar folder) seed it.
  // Silently persist a pathed, modified document before switching away, so the
  // confirm-discard prompt below is skipped. No-op unless files/autoSaveOnSwitch is on.
  QSettings settings;
  if (!settings.value(QStringLiteral("files/autoSaveOnSwitch"), false).toBool()) {
    return;
  }
  if (session.filePath().isEmpty() || !session.document().isModified()) {
    return;
  }
  save(session, parent);
}

muffin::SaveOutcome muffin::FileController::saveAs(DocumentSession& session, QWidget* parent, const QString& defaultDir) {
  if (session.isAsyncParseInProgress()) {
    return SaveOutcome::SkippedBusy;
  }
  // For an untitled document, anchor the dialog in the requested directory
  // (typically the sidebar's open folder) rather than the working directory.
  QString startingPath;
  if (!session.filePath().isEmpty()) {
    startingPath = session.filePath();
  } else {
    const QString name = defaultUntitledName();
    startingPath = defaultDir.isEmpty() ? name : QDir(defaultDir).filePath(name);
  }
  QString path = QFileDialog::getSaveFileName(
      parent,
      tr("Save As"),
      startingPath,
      tr("Markdown files (*.md *.markdown);;Text files (*.txt);;All files (*.*)"));
  if (path.isEmpty()) {
    return SaveOutcome::Failed;
  }
  TextFileFormat format = session.fileFormat();
  if (!format.existingFile) {
    format = formatForNewFile();
  }
  DocumentSession::SelfWriteGuard guard(session);
  QString normalizedText;
  if (!writeTextFile(path, session.markdownText().toString(), format, &normalizedText, parent)) {
    return SaveOutcome::Failed;
  }
  // The previous path's draft (if any) is now obsolete: the content lives at `path`.
  emit documentBecameClean(session.filePath());
  session.setFilePath(path);  // also re-points the QFileSystemWatcher to the new path
  format.existingFile = true;
  session.setFileFormat(format);
  if (normalizedText != session.markdownText().toString()) {
    session.setMarkdownText(normalizedText, false);
  } else {
    session.document().setModified(false);
  }
  session.recordFileBaseline();  // baseline the new path
  return SaveOutcome::Saved;
}

bool muffin::FileController::confirmDiscardIfModified(DocumentSession& session, QWidget* parent) {
  if (!session.document().isModified()) {
    return true;
  }

  const QMessageBox::StandardButton choice = QMessageBox::warning(
      parent,
      tr("Muffin"),
      tr("The current document has unsaved changes."),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  if (choice == QMessageBox::Cancel) {
    return false;
  }
  if (choice == QMessageBox::Save) {
    return save(session, parent) == SaveOutcome::Saved;  // save() emits documentBecameClean on success.
  }
  emit documentBecameClean(session.filePath());
  return true;
}

bool muffin::FileController::confirmOverwriteIfChanged(DocumentSession& session, QWidget* parent) {
  if (!session.hasFileBaseline()) {
    return true;  // no baseline (first save / freshly opened before baseline recorded) → just write
  }
  const QFileInfo info(session.filePath());
  if (!info.exists()) {
    QMessageBox::warning(parent, tr("File Missing"),
                         tr("The file \"%1\" no longer exists on disk. Use Save As to write it to a new location.")
                             .arg(session.filePath()));
    return false;
  }
  if (info.lastModified() == session.fileBaselineMtime() && info.size() == session.fileBaselineSize()) {
    return true;  // unchanged since the open/last-save baseline
  }
  const QMessageBox::StandardButton choice = QMessageBox::warning(
      parent, tr("File Changed on Disk"),
      tr("The file \"%1\" has been changed outside Muffin. Overwrite it?").arg(info.fileName()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  return choice == QMessageBox::Yes;
}

bool muffin::FileController::readTextFile(
    const QString& path, QString* out, TextFileFormat* format, QWidget* parent) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::critical(parent, tr("Open Failed"), file.errorString());
    return false;
  }

  // Decode the complete byte stream once so charset detection and BOM handling see the whole file.
  const QByteArray raw = file.readAll();
  QString text;
  if (!decodeText(raw, {}, &text, format)) {
    QMessageBox::critical(parent, tr("Encoding Error"),
                          tr("Could not detect a lossless text encoding for this file."));
    return false;
  }
  *out = std::move(text);
  return true;
}

bool muffin::FileController::writeTextFile(
    const QString& path, const QString& text, const TextFileFormat& format,
    QString* normalizedText, QWidget* parent) const {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    QMessageBox::critical(parent, tr("Save Failed"), file.errorString());
    return false;
  }

  QString content = text;

  if (format.ensureTrailingNewline) {
    if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n'))) {
      content += QLatin1Char('\n');
    }
  }

  // Apply line endings (internal text is always LF)
  if (normalizedText) {
    *normalizedText = content;
  }

  QString diskText = content;
  if (format.lineEnding == TextLineEnding::Crlf) {
    diskText.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
  } else if (format.lineEnding == TextLineEnding::Cr) {
    diskText.replace(QLatin1Char('\n'), QLatin1Char('\r'));
  }

  QByteArray encoded;
  if (format.encodingName.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) == 0) {
    encoded = diskText.toUtf8();
  } else if (!encodeLegacyText(diskText, format.encodingName, &encoded)) {
    QMessageBox::critical(parent, tr("Encoding Error"),
                          tr("The document cannot be encoded as %1.").arg(format.encodingName));
    file.cancelWriting();
    return false;
  }
  if (format.writeBom) {
    encoded.prepend(bomForEncoding(format.encodingName));
  }
  if (file.write(encoded) != encoded.size()) {
    QMessageBox::critical(parent, tr("Save Failed"), file.errorString());
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    QMessageBox::critical(parent, tr("Save Failed"), file.errorString());
    return false;
  }
  return true;
}

muffin::TextFileFormat muffin::FileController::formatForNewFile() const {
  QSettings settings;
  TextFileFormat format;
  format.encodingName = QStringLiteral("UTF-8");
  format.lineEnding = settings.value(QStringLiteral("editor/defaultLineBreak"), 1).toInt() == 1
      ? TextLineEnding::Crlf : TextLineEnding::Lf;
  format.ensureTrailingNewline =
      settings.value(QStringLiteral("editor/trailingNewline"), true).toBool();
  return format;
}

bool muffin::FileController::readTextFileWithEncoding(
    const QString& path, QString* out, TextFileFormat* format,
    QWidget* parent, const QString& encodingName) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::critical(parent, tr("Open Failed"), file.errorString());
    return false;
  }

  QString text;
  if (!decodeText(file.readAll(), encodingName, &text, format)) {
    QMessageBox::critical(parent, tr("Encoding Error"),
                          tr("Failed to decode file with encoding: %1").arg(encodingName));
    return false;
  }
  *out = std::move(text);
  return true;
}

bool muffin::FileController::reopenWithEncoding(
    DocumentSession& session, QWidget* parent,
    const QString& encodingName) {
  if (session.filePath().isEmpty()) {
    return false;
  }

  if (session.document().isModified()) {
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        parent, tr("Muffin"),
        tr("The document has unsaved changes. Save before reopening with a new encoding?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
      return false;
    }
    if (choice == QMessageBox::Save) {
      if (save(session, parent) != SaveOutcome::Saved) {
        return false;
      }
    } else {
      emit documentBecameClean(session.filePath());
    }
  }

  QString text;
  TextFileFormat format;
  if (!readTextFileWithEncoding(session.filePath(), &text, &format, parent, encodingName)) {
    return false;
  }

  session.setFileFormat(format);
  session.setMarkdownText(text, false);
  return true;
}

bool muffin::FileController::moveTo(DocumentSession& session, QWidget* parent) {
  if (session.filePath().isEmpty()) {
    return false;
  }

  if (session.document().isModified()) {
    if (save(session, parent) != SaveOutcome::Saved) {
      return false;
    }
  }

  const QString newPath = QFileDialog::getSaveFileName(
      parent, tr("Move To"),
      session.filePath(),
      tr("Markdown files (*.md);;Text files (*.txt);;All files (*.*)"));
  if (newPath.isEmpty() || newPath == session.filePath()) {
    return false;
  }

  if (!QFile::rename(session.filePath(), newPath)) {
    QMessageBox::critical(parent, tr("Move Failed"),
                          tr("Could not move file to:\n%1").arg(newPath));
    return false;
  }

  session.setFilePath(newPath);
  session.document().setModified(false);
  session.recordFileBaseline();  // baseline the moved file
  return true;
}

bool muffin::FileController::reload(DocumentSession& session, QWidget* parent) {
  if (session.filePath().isEmpty()) {
    return false;
  }
  QString text;
  TextFileFormat format;
  if (!readTextFile(session.filePath(), &text, &format, parent)) {
    return false;
  }
  // Discards unsaved edits (caller already confirmed). Does NOT route through open(), which would
  // trigger autoSaveOnSwitch and overwrite the very external change being reloaded. Re-baseline so
  // the watcher treats the reloaded content as the new reference.
  session.setFileFormat(format);
  session.setMarkdownText(text, false);
  session.recordFileBaseline();
  return true;
}

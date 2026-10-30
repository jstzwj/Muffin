#include "app/WorkspaceSearch.h"
#include "document/PieceTable.h"
#include "io/FileController.h"
#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringConverter>
#include <QThread>
#include <algorithm>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {
using namespace muffin;
void hashText(QCryptographicHash& hash, QStringView text) {
  hash.addData(QByteArrayView(reinterpret_cast<const char*>(text.utf16()), text.size() * 2));
}
QString errorText(int code) {
  PCRE2_UCHAR buffer[256];
  const int length = pcre2_get_error_message(code, buffer, 256);
  return length < 0 ? QString::number(code) : QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer), length);
}
QString snippet(QStringView text, qsizetype offset, qsizetype length, int* highlight) {
  auto start = qMax<qsizetype>(0, offset - 70);
  if (start > 0 && text.at(start).isLowSurrogate())
    --start;
  const auto newline = text.mid(start, offset - start).lastIndexOf(u'\n');
  if (newline >= 0)
    start += newline + 1;
  const bool leading = start > 0 && text.at(start - 1) != u'\n';
  auto stop = qMin(text.size(), qMax(offset + qMin<qsizetype>(length, 80), start + 160));
  const auto ending = text.mid(offset, stop - offset).indexOf(u'\n');
  if (ending >= 0)
    stop = offset + ending;
  if (stop < text.size() && text.at(stop).isLowSurrogate())
    ++stop;
  *highlight = int(offset - start) + (leading ? 1 : 0);
  return (leading ? QString(QChar(0x2026)) : QString()) + text.mid(start, stop - start).toString() +
         (stop < text.size() && text.at(stop) != u'\n' ? QString(QChar(0x2026)) : QString());
}

// Compiled code, match data and callout timers belong to a single worker.
class Matcher {
public:
  Matcher(const WorkspaceQuery& query, SearchCancellation cancel) : cancel_(std::move(cancel)) {
    const auto pattern = query.options.regularExpression ? query.pattern : QRegularExpression::escape(query.pattern);
    int error = 0;
    PCRE2_SIZE offset = 0;
    uint32_t flags = PCRE2_UTF;
    if (!query.options.caseSensitive)
      flags |= PCRE2_CASELESS;
    if (query.options.regularExpression)
      flags |= PCRE2_AUTO_CALLOUT;
    code_ =
        pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.utf16()), pattern.size(), flags, &error, &offset, nullptr);
    if (!code_) {
      error_ = errorText(error);
      return;
    }
    data_ = pcre2_match_data_create_from_pattern(code_, nullptr);
    context_ = pcre2_match_context_create(nullptr);
    if (!data_ || !context_) {
      error_ = errorText(PCRE2_ERROR_NOMEMORY);
      return;
    }
    pcre2_set_match_limit(context_, 50000);
    pcre2_set_depth_limit(context_, 500);
    pcre2_set_heap_limit(context_, 16 * 1024);
    pcre2_set_callout(
        context_,
        [](pcre2_callout_block*, void* opaque) {
          auto* self = static_cast<Matcher*>(opaque);
          return (self->cancel_ && self->cancel_->load()) || self->timer_.elapsed() > 2000 ? PCRE2_ERROR_CALLOUT : 0;
        },
        this);
  }
  ~Matcher() {
    pcre2_match_context_free(context_);
    pcre2_match_data_free(data_);
    pcre2_code_free(code_);
  }
  const QString& error() const { return error_; }
  void startFile() {
    error_.clear();
    timer_.start();
  }
  // Mirror QRegularExpression's empty-match retry and Unicode advancement.
  bool scan(QStringView text, qsizetype base, qsizetype cutoff, int lineBase, int limit, WorkspaceSearchFile& file,
            qsizetype& next, bool& empty) {
    qsizetype lineOffset = 0;
    int line = lineBase;
    bool utfChecked = false;
    while (next - base <= text.size()) {
      if (cancel_ && cancel_->load())
        return false;
      const auto local = qMax<qsizetype>(0, next - base);
      const int found =
          pcre2_match(code_, reinterpret_cast<PCRE2_SPTR>(text.utf16()), text.size(), local,
                      (empty ? PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED : 0) | (utfChecked ? PCRE2_NO_UTF_CHECK : 0),
                      data_, context_);
      utfChecked = found >= 0 || found == PCRE2_ERROR_NOMATCH;
      if (found == PCRE2_ERROR_NOMATCH) {
        if (empty && local < text.size()) {
          next += text.at(local).isHighSurrogate() && local + 1 < text.size() && text.at(local + 1).isLowSurrogate()
                      ? 2
                      : 1;
          empty = false;
          continue;
        }
        next = qMax(next, base + cutoff);
        return true;
      }
      if (found < 0) {
        error_ = errorText(found);
        return false;
      }
      const auto* offsets = pcre2_get_ovector_pointer(data_);
      const qsizetype start = offsets[0], length = offsets[1] - offsets[0];
      if (start >= cutoff) {
        next = qMax(next, base + cutoff);
        return true;
      }
      if (file.hits.size() >= limit)
        return true;
      while (lineOffset < start)
        if (text.at(lineOffset++) == u'\n')
          ++line;
      WorkspaceHit hit;
      hit.match.start = base + start;
      hit.match.length = length;
      hit.line = line;
      hit.context = snippet(text, start, length, &hit.highlight);
      file.hits.append(std::move(hit));
      next = base + start + length;
      empty = length == 0;
      if (empty && start == text.size()) {
        ++next;
        return true;
      }
    }
    return true;
  }

private:
  pcre2_code* code_ = nullptr;
  pcre2_match_data* data_ = nullptr;
  pcre2_match_context* context_ = nullptr;
  QString error_;
  SearchCancellation cancel_;
  QElapsedTimer timer_;
};
struct FileResult {
  WorkspaceSearchFile file;
  std::optional<WorkspaceSkipReason> skipped;
  QString error;
  bool limited = false, searched = false;
};

FileResult searchFile(const QString& path, const WorkspaceQuery& query, const OpenSearchBuffer* buffer,
                      Matcher& matcher, const SearchCancellation& cancel) {
  FileResult result;
  auto& file = result.file;
  file.path = path;
  file.modified = buffer && buffer->modified;
  const QFileInfo before(path);
  const qint64 textBytes =
      buffer ? (buffer->snapshot ? buffer->snapshot->size() : buffer->text.size()) * 2 : before.size();
  if (buffer && buffer->busy) {
    result.skipped = WorkspaceSkipReason::Busy;
    return result;
  }
  if (textBytes > query.maxFileBytes) {
    result.skipped = WorkspaceSkipReason::TooLarge;
    return result;
  }
  if (query.options.regularExpression && textBytes > query.maxRegexBytes) {
    result.skipped = WorkspaceSkipReason::RegexTooLarge;
    return result;
  }
  file.replaceable =
      !file.modified && !(buffer && buffer->readOnly) && before.isWritable() && before.size() <= query.maxReplaceBytes;
  QCryptographicHash hash(QCryptographicHash::Sha256);
  QString window;
  qsizetype base = 0, next = 0;
  int lineBase = 1;
  bool empty = false;
  matcher.startFile();
  auto consume = [&](const QString& text, bool end) {
    hashText(hash, text);
    if (result.limited)
      return true;
    window += text;
    qsizetype cutoff = end ? window.size() + 1 : qMax<qsizetype>(0, window.size() - query.pattern.size() - 160);
    if (cutoff < window.size() && window.at(cutoff).isLowSurrogate())
      --cutoff;
    if (cutoff > 0 && !matcher.scan(window, base, cutoff, lineBase, qMax(1, query.maxMatches), file, next, empty)) {
      result.skipped = WorkspaceSkipReason::ResourceLimit;
      result.error = matcher.error();
      return false;
    }
    if (file.hits.size() >= qMax(1, query.maxMatches))
      result.limited = true;
    auto removed = qMax<qsizetype>(0, qMin(next - base - 80, window.size()));
    if (removed < window.size() && window.at(removed).isLowSurrogate())
      --removed;
    lineBase += int(QStringView(window).left(removed).count(u'\n'));
    window.remove(0, removed);
    base += removed;
    return true;
  };
  auto consumeText = [&](const auto& source) {
    const auto chunk = qMax(256, query.chunkBytes / 2);
    for (qsizetype offset = 0; offset < source.size();) {
      if (cancel && cancel->load())
        return false;
      auto length = qMin<qsizetype>(chunk, source.size() - offset);
      if (offset + length < source.size() && source.at(offset + length).isLowSurrogate())
        ++length;
      if (!consume(source.mid(offset, length), offset + length == source.size()))
        return false;
      offset += length;
    }
    return !source.isEmpty() || consume({}, true);
  };
  // Regexes retain arbitrary cross-line semantics with a separately bounded
  // contiguous subject. Ordinary matching uses a rolling window instead.
  if (buffer && buffer->snapshot && !query.options.regularExpression) {
    if (!consumeText(*buffer->snapshot))
      return result;
  } else if (buffer || before.size() <= query.maxReplaceBytes || query.options.regularExpression) {
    QString text;
    if (buffer)
      text = buffer->snapshot ? buffer->snapshot->toString() : buffer->text;
    else {
      QFile disk(path);
      TextFileFormat format;
      if (!disk.open(QIODevice::ReadOnly)) {
        result.skipped = WorkspaceSkipReason::Unreadable;
        return result;
      }
      // QFile::read(maxSize) can reserve maxSize up front. Size this from the
      // actual file, rather than allocating the regex ceiling for every note.
      const auto bytes = disk.read(qMin(before.size(), qMax(query.maxReplaceBytes, query.maxRegexBytes)) + 1);
      if (disk.error() != QFileDevice::NoError || !disk.atEnd()) {
        result.skipped = WorkspaceSkipReason::Unreadable;
        return result;
      }
      if (!FileController::decodeTextBytes(bytes, &text, &format) || text.contains(QChar(0))) {
        result.skipped = WorkspaceSkipReason::InvalidText;
        return result;
      }
    }
    if (query.options.regularExpression) {
      if (!consume(text, true))
        return result;
    } else if (!consumeText(text))
      return result;
  } else {
    QFile disk(path);
    if (!disk.open(QIODevice::ReadOnly)) {
      result.skipped = WorkspaceSkipReason::Unreadable;
      return result;
    }
    const auto prefix = disk.peek(4);
    QStringConverter::Encoding encoding = QStringConverter::Utf8;
    int bom = 0;
    if (prefix.startsWith(QByteArray::fromHex("0000feff"))) {
      encoding = QStringConverter::Utf32BE;
      bom = 4;
    } else if (prefix.startsWith(QByteArray::fromHex("fffe0000"))) {
      encoding = QStringConverter::Utf32LE;
      bom = 4;
    } else if (prefix.startsWith(QByteArray::fromHex("feff"))) {
      encoding = QStringConverter::Utf16BE;
      bom = 2;
    } else if (prefix.startsWith(QByteArray::fromHex("fffe"))) {
      encoding = QStringConverter::Utf16LE;
      bom = 2;
    } else if (prefix.startsWith(QByteArray::fromHex("efbbbf")))
      bom = 3;
    disk.seek(bom);
    QStringDecoder decoder(encoding);
    bool cr = false;
    QChar highSurrogate;
    while (!disk.atEnd()) {
      if (cancel && cancel->load())
        return result;
      const auto bytes = disk.read(qMax(256, query.chunkBytes));
      if (disk.error() != QFileDevice::NoError || bytes.isEmpty()) {
        result.skipped = WorkspaceSkipReason::Unreadable;
        return result;
      }
      if (disk.pos() > query.maxFileBytes) {
        result.skipped = WorkspaceSkipReason::TooLarge;
        return result;
      }
      QString text = decoder(bytes);
      if (decoder.hasError() || text.contains(QChar(0))) {
        result.skipped = WorkspaceSkipReason::InvalidText;
        return result;
      }
      // The UTF-16 decoder can return the high surrogate before the next
      // read supplies its low surrogate. PCRE2 validates the whole subject,
      // including the part beyond our matching cutoff.
      if (!highSurrogate.isNull()) {
        text.prepend(highSurrogate);
        highSurrogate = QChar();
      }
      if (!text.isEmpty() && text.back().isHighSurrogate() && !disk.atEnd()) {
        highSurrogate = text.back();
        text.chop(1);
      }
      if (cr) {
        text.prepend(u'\r');
        cr = false;
      }
      if (!text.isEmpty() && text.back() == u'\r' && !disk.atEnd()) {
        text.chop(1);
        cr = true;
      }
      text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
      text.replace(u'\r', u'\n');
      if (!consume(text, disk.atEnd()))
        return result;
    }
    const auto final = decoder.finalize();
    if (decoder.hasError() || final.invalidChars) {
      result.skipped = WorkspaceSkipReason::InvalidText;
      return result;
    }
  }
  const QFileInfo after(path);
  if (!buffer && (before.size() != after.size() || before.lastModified() != after.lastModified())) {
    result.skipped = WorkspaceSkipReason::Changed;
    file.hits.clear();
    return result;
  }
  file.fingerprint = hash.result();
  result.searched = true;
  return result;
}
}

QByteArray muffin::WorkspaceSearch::fingerprint(QStringView text) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hashText(hash, text);
  return hash.result();
}
QByteArray muffin::WorkspaceSearch::fingerprint(const PieceTable& text) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  text.forEachChunk([&](QStringView chunk) { hashText(hash, chunk); });
  return hash.result();
}
bool muffin::WorkspaceSearch::bufferMatches(const OpenSearchBuffer& buffer, QStringView text) {
  return buffer.snapshot ? fingerprint(*buffer.snapshot) == fingerprint(text) : QStringView(buffer.text) == text;
}
muffin::WorkspaceSearchResult
muffin::WorkspaceSearch::run(const WorkspaceQuery& query, const OpenSearchBuffers& buffers,
                             const SearchCancellation& cancel,
                             const std::function<void(const WorkspaceSearchFile&)>& progress) {
  WorkspaceSearchResult result;
  result.query = query;
  if (query.pattern.isEmpty())
    return result;
  Matcher validation(query, cancel);
  if (!validation.error().isEmpty()) {
    result.error = validation.error();
    result.complete = false;
    return result;
  }
  if (!QFileInfo(query.root).isDir()) {
    result.error = QCoreApplication::translate("muffin::WorkspaceSearch", "Folder is unavailable");
    result.complete = false;
    return result;
  }
  const auto index = WorkspaceFileIndex::scan(query.root, query.extensions, cancel, query.scanOptions);
  result.complete = !index.truncated && index.unreadableIgnoreFiles.isEmpty();
  for (const auto& path : index.unreadableDirectories) {
    result.skipped.append(path);
    result.details.append({path, WorkspaceSkipReason::Unreadable});
  }
  for (const auto& path : index.ignoredDirectories)
    result.details.append({path, WorkspaceSkipReason::IgnoredDirectory});
  for (const auto& path : index.ignoredFiles)
    result.details.append({path, WorkspaceSkipReason::IgnoredFile});
  for (const auto& path : index.unreadableIgnoreFiles)
    result.details.append({path, WorkspaceSkipReason::UnreadableIgnoreFile});
  std::atomic<qsizetype> nextFile{0};
  std::atomic_bool stop{false};
  std::mutex publish;
  qint64 retained = 0;
  const int workers = qMax(1, std::min({query.workerCount, QThread::idealThreadCount(), 4, int(index.paths.size())}));
  auto work = [&] {
    Matcher matcher(query, cancel);
    if (!matcher.error().isEmpty()) {
      std::lock_guard lock(publish);
      result.error = matcher.error();
      result.complete = false;
      stop.store(true);
      return;
    }
    while (!(cancel && cancel->load()) && !stop.load()) {
      const auto i = nextFile.fetch_add(1);
      if (i >= qsizetype(index.paths.size()))
        break;
      const auto& path = index.paths[i];
      const auto relative = QDir(query.root).relativeFilePath(path);
      if (PathMatcher::score(query.includePath, relative) < 0 ||
          (!query.excludePath.trimmed().isEmpty() && PathMatcher::score(query.excludePath, relative) >= 0))
        continue;
      const auto buffer = buffers.constFind(WorkspaceFileIndex::pathKey(path));
      auto item = searchFile(path, query, buffer == buffers.cend() ? nullptr : &buffer.value(), matcher, cancel);
      std::lock_guard lock(publish);
      if (item.searched)
        ++result.searchedFiles;
      if (item.skipped) {
        result.skipped.append(path);
        result.details.append({path, *item.skipped});
        if (*item.skipped == WorkspaceSkipReason::ResourceLimit || *item.skipped == WorkspaceSkipReason::Changed)
          result.complete = false;
      }
      if (!item.error.isEmpty()) {
        result.error = item.error;
        result.complete = false;
      }
      if (item.limited) {
        result.complete = false;
        result.resultLimitReached = true;
      }
      if (item.file.fingerprint.isEmpty())
        continue;
      const int remaining = qMax(0, query.maxMatches - result.matches);
      if (item.file.hits.size() > remaining)
        item.file.hits.resize(remaining);
      for (qsizetype n = 0; n < item.file.hits.size(); ++n) {
        const qint64 bytes = qint64(sizeof(WorkspaceHit)) + item.file.hits[n].context.size() * 2;
        if (retained + bytes > query.maxResultBytes) {
          item.file.hits.resize(n);
          result.resultLimitReached = true;
          result.complete = false;
          stop.store(true);
          break;
        }
        retained += bytes;
      }
      if (!item.file.hits.isEmpty()) {
        result.matches += int(item.file.hits.size());
        if (progress)
          progress(item.file);
        result.files.append(std::move(item.file));
      }
      if (result.matches >= query.maxMatches) {
        result.complete = false;
        result.resultLimitReached = true;
        stop.store(true);
      }
    }
  };
  {
    std::vector<std::thread> threads;
    struct JoinThreads {
      std::vector<std::thread>& threads;
      ~JoinThreads() {
        for (auto& thread : threads)
          if (thread.joinable())
            thread.join();
      }
    } join{threads};
    for (int i = 1; i < workers; ++i)
      threads.emplace_back(work);
    work();
  }
  if (cancel && cancel->load())
    result.complete = false;
  return result;
}

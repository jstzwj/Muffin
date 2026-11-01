#include "editor/DocumentSearch.h"
#include "document/PieceTable.h"

#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>
#include <QElapsedTimer>
#include <QStringView>
#include <limits>
#include <utility>

namespace {
using namespace muffin;
bool canceled(const DocumentSearch::Cancellation& cancel) {
  return cancel && cancel->load(std::memory_order_relaxed);
}
QString regexError(int error) {
  PCRE2_UCHAR buffer[256];
  const int length = pcre2_get_error_message(error, buffer, 256);
  return length < 0 ? QString::number(error) : QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer), length);
}

// Like folder search, use the bounded PCRE2 interpreter. AUTO_CALLOUT lets
// cancellation interrupt even a single expensive regular-expression match.
template <typename Visitor>
SearchResults scanRegex(const QString& text, const QString& pattern, SearchOptions options,
                        const DocumentSearch::Cancellation& cancel, Visitor&& visit) {
  SearchResults result;
  int error = 0;
  PCRE2_SIZE errorOffset = 0;
  using Code = std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)>;
  using Data = std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)>;
  using Context = std::unique_ptr<pcre2_match_context, decltype(&pcre2_match_context_free)>;
  Code code(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.utf16()), pattern.size(),
                          PCRE2_UTF | PCRE2_AUTO_CALLOUT | (options.caseSensitive ? 0 : PCRE2_CASELESS), &error,
                          &errorOffset, nullptr),
            &pcre2_code_free);
  if (!code) {
    result.valid = false;
    result.error = regexError(error);
    return result;
  }
  Data data(pcre2_match_data_create_from_pattern(code.get(), nullptr), &pcre2_match_data_free);
  Context context(pcre2_match_context_create(nullptr), &pcre2_match_context_free);
  if (!data || !context) {
    result.valid = false;
    result.error = regexError(PCRE2_ERROR_NOMEMORY);
    return result;
  }
  struct Budget {
    const DocumentSearch::Cancellation& cancel;
    QElapsedTimer timer;
  } budget{cancel, {}};
  pcre2_set_match_limit(context.get(), 50000);
  pcre2_set_depth_limit(context.get(), 500);
  pcre2_set_heap_limit(context.get(), 16 * 1024);
  pcre2_set_callout(
      context.get(),
      [](pcre2_callout_block*, void* opaque) {
        const auto* budget = static_cast<Budget*>(opaque);
        return canceled(budget->cancel) || budget->timer.elapsed() > 2000 ? PCRE2_ERROR_CALLOUT : 0;
      },
      &budget);
  qsizetype next = 0;
  bool empty = false, utfChecked = false;
  budget.timer.start();
  while (next <= text.size() && !canceled(cancel)) {
    const int found =
        pcre2_match(code.get(), reinterpret_cast<PCRE2_SPTR>(text.utf16()), text.size(), next,
                    (empty ? PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED : 0) | (utfChecked ? PCRE2_NO_UTF_CHECK : 0),
                    data.get(), context.get());
    utfChecked = found >= 0 || found == PCRE2_ERROR_NOMATCH;
    if (found == PCRE2_ERROR_NOMATCH) {
      if (!empty || next == text.size())
        break;
      next += text.at(next).isHighSurrogate() && next + 1 < text.size() && text.at(next + 1).isLowSurrogate() ? 2 : 1;
      empty = false;
      continue;
    }
    if (found < 0) {
      result.valid = false;
      result.error = regexError(found);
      break;
    }
    const auto* offsets = pcre2_get_ovector_pointer(data.get());
    SearchMatch match{qsizetype(offsets[0]), qsizetype(offsets[1] - offsets[0]), {}};
    for (int i = 0; i < found; ++i) {
      match.captures.append(offsets[i * 2] == PCRE2_UNSET
                                ? QString()
                                : text.mid(qsizetype(offsets[i * 2]), qsizetype(offsets[i * 2 + 1] - offsets[i * 2])));
    }
    visit(match, result.totalMatches++);
    next = match.start + match.length;
    empty = match.length == 0;
  }
  result.canceled = canceled(cancel);
  return result;
}

template <typename Visitor>
SearchResults scan(const PieceTable& text, const QString& pattern, SearchOptions options,
                   const DocumentSearch::Cancellation& cancel, Visitor&& visit) {
  SearchResults result;
  if (pattern.isEmpty() || canceled(cancel)) {
    result.canceled = canceled(cancel);
    return result;
  }
  if (options.regularExpression)
    return scanRegex(text.toString(), pattern, options, cancel, std::forward<Visitor>(visit));
  const auto normalize = [options](QChar ch) { return options.caseSensitive ? ch : ch.toCaseFolded(); };
  QVector<QChar> needle;
  for (QChar ch : pattern)
    needle.append(normalize(ch));
  QVector<qsizetype> fallback(needle.size(), 0);
  for (qsizetype i = 1, matched = 0; i < needle.size(); ++i) {
    while (matched > 0 && needle[i] != needle[matched])
      matched = fallback[matched - 1];
    if (needle[i] == needle[matched])
      ++matched;
    fallback[i] = matched;
  }
  qsizetype logical = 0, matched = 0;
  text.forEachChunk([&](QStringView chunk) {
    if (result.canceled)
      return;
    for (QChar raw : chunk) {
      if ((logical & 4095) == 0 && canceled(cancel)) {
        result.canceled = true;
        return;
      }
      const QChar ch = normalize(raw);
      while (matched > 0 && ch != needle[matched])
        matched = fallback[matched - 1];
      if (ch == needle[matched])
        ++matched;
      if (matched == needle.size()) {
        visit(SearchMatch{logical - needle.size() + 1, needle.size(), {}}, result.totalMatches++);
        matched = 0;
      }
      ++logical;
    }
  });
  result.canceled = result.canceled || canceled(cancel);
  return result;
}

struct Navigation {
  qsizetype offset;
  bool forward;
  qsizetype exclude;
  SearchMatch first{-1, 0, {}}, last{-1, 0, {}}, selected{-1, 0, {}};
  qsizetype index = -1;
  void accept(const SearchMatch& match, qsizetype position) {
    if (first.start < 0)
      first = match;
    last = match;
    if (match.start != exclude &&
        (forward ? match.start >= offset && index < 0 : match.start + match.length <= offset)) {
      selected = match;
      index = position;
    }
  }
  void finish(SearchResults& result) {
    if (index < 0 && result.totalMatches > 0) {
      selected = forward ? first : last;
      index = forward ? 0 : result.totalMatches - 1;
    }
    result.selectedMatch = std::move(selected);
    result.selectedIndex = index;
  }
};
} // namespace

namespace muffin {
SearchResults DocumentSearch::findAll(const QString& text, const QString& pattern, SearchOptions options,
                                      Cancellation cancel) {
  return findAll(PieceTable(text), pattern, options, std::move(cancel));
}

SearchResults DocumentSearch::findAll(const PieceTable& text, const QString& pattern, SearchOptions options,
                                      Cancellation cancel, qsizetype offset, bool forward, qsizetype excludeStart) {
  QVector<SearchMatch> cache;
  Navigation navigation{offset, forward, excludeStart};
  auto result = scan(text, pattern, options, cancel, [&](const SearchMatch& match, qsizetype index) {
    navigation.accept(match, index);
    if (cache.size() < kCachedMatches)
      cache.append({match.start, match.length, {}});
  });
  result.matches = std::move(cache);
  navigation.finish(result);
  return result;
}

void DocumentSearch::selectMatch(SearchResults& results, qsizetype offset, bool forward, qsizetype excludeStart) {
  Navigation navigation{offset, forward, excludeStart};
  for (qsizetype i = 0; i < results.matches.size(); ++i)
    navigation.accept(results.matches[i], i);
  navigation.finish(results);
}

QString DocumentSearch::expandReplacement(const QString& replacement, const SearchMatch& match, bool regex) {
  if (!regex)
    return replacement;
  QString expanded;
  for (qsizetype i = 0; i < replacement.size(); ++i) {
    const auto ch = replacement[i];
    if ((ch == u'$' || ch == u'\\') && i + 1 < replacement.size() && replacement[i + 1].isDigit()) {
      qsizetype capture = 0, j = i + 1;
      while (j < replacement.size() && replacement[j].isDigit()) {
        const int digit = replacement[j++].digitValue();
        capture = capture > (std::numeric_limits<qsizetype>::max() - digit) / 10 ? std::numeric_limits<qsizetype>::max()
                                                                                 : capture * 10 + digit;
      }
      if (capture < match.captures.size())
        expanded += match.captures[capture];
      i = j - 1;
    } else {
      expanded += ch;
    }
  }
  return expanded;
}

SearchResults DocumentSearch::replaceAll(const QString& text, const QString& pattern, const QString& replacement,
                                         SearchOptions options, QString* output, Cancellation cancel,
                                         QVector<qsizetype>* sourcePositions) {
  QString replaced;
  replaced.reserve(text.size());
  qsizetype copied = 0;
  QVector<SearchMatch> cache;
  const auto originalPositions = sourcePositions ? *sourcePositions : QVector<qsizetype>();
  auto mappedPositions = originalPositions;
  bool changed = false;
  auto result = scan(PieceTable(text), pattern, options, cancel, [&](const SearchMatch& match, qsizetype) {
    replaced += QStringView(text).mid(copied, match.start - copied);
    const auto inserted = expandReplacement(replacement, match, options.regularExpression);
    changed = changed || QStringView(text).mid(match.start, match.length) != inserted;
    for (qsizetype i = 0; i < originalPositions.size(); ++i) {
      const auto offset = originalPositions[i];
      if (offset >= match.start && offset < match.start + match.length)
        mappedPositions[i] = replaced.size() + qMin(offset - match.start, inserted.size());
      else if (offset >= match.start + match.length)
        mappedPositions[i] += inserted.size() - match.length;
    }
    replaced += inserted;
    copied = match.start + match.length;
    if (cache.size() < kCachedMatches)
      cache.append({match.start, match.length, {}});
  });
  result.matches = std::move(cache);
  result.changed = changed;
  if (result.valid && !result.canceled && output) {
    replaced += QStringView(text).mid(copied);
    *output = std::move(replaced);
    if (sourcePositions)
      *sourcePositions = std::move(mappedPositions);
  }
  return result;
}
} // namespace muffin

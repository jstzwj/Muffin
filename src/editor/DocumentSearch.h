#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

namespace muffin {

class PieceTable;

struct SearchOptions {
  bool regularExpression = false;
  bool caseSensitive = false;
};

struct SearchMatch {
  qsizetype start = 0;
  qsizetype length = 0;
  QStringList captures;
};

struct SearchResults {
  bool valid = true;
  QString error;
  QVector<SearchMatch> matches;
  qsizetype totalMatches = 0;
  bool canceled = false;
  qsizetype selectedIndex = -1;
  SearchMatch selectedMatch{-1, 0, {}};
};

class DocumentSearch final {
public:
  using Cancellation = std::shared_ptr<std::atomic_bool>;
  static constexpr qsizetype kCachedMatches = 10000;
  // Count all matches, retaining a bounded cache. Targets beyond that cache
  // are selected during the same streaming scan.
  static SearchResults findAll(const QString& text, const QString& pattern, SearchOptions options,
                               Cancellation cancel = {});
  static SearchResults findAll(const PieceTable& text, const QString& pattern, SearchOptions options,
                               Cancellation cancel = {}, qsizetype offset = 0, bool forward = true,
                               qsizetype excludeStart = -1);
  static void selectMatch(SearchResults& results, qsizetype offset, bool forward, qsizetype excludeStart = -1);
  static QString expandReplacement(const QString& replacement, const SearchMatch& match, bool regularExpression);
  static SearchResults replaceAll(const QString& text, const QString& pattern, const QString& replacement,
                                  SearchOptions options, QString* output, Cancellation cancel = {});

private:
  DocumentSearch() = delete;
};

} // namespace muffin

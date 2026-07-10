#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

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
};

class DocumentSearch final {
public:
  static SearchResults findAll(const QString& text, const QString& pattern,
                               SearchOptions options);
  static SearchResults findAll(const PieceTable& text, const QString& pattern,
                               SearchOptions options);
  static QString expandReplacement(const QString& replacement,
                                   const SearchMatch& match,
                                   bool regularExpression);
  static SearchResults replaceAll(const QString& text, const QString& pattern,
                                  const QString& replacement, SearchOptions options,
                                  QString* output);

private:
  DocumentSearch() = delete;
};

}  // namespace muffin

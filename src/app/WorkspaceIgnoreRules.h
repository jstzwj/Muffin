#pragma once

#include <QRegularExpression>
#include <QStringView>
#include <QVector>

namespace muffin {

// Compiled gitignore-style rules, relative to the directory containing their
// source file. Directory pruning is performed by WorkspaceFileIndex, so an
// ignored parent cannot be re-included by a rule in an inaccessible child.
class WorkspaceIgnoreRules {
public:
  void append(QStringView text, const QString& base = {});
  bool ignored(const QString& relativePath, bool directory, bool previous = false) const;

private:
  struct Rule {
    QString base;
    QRegularExpression pattern;
    bool directoryOnly = false;
    bool negated = false;
  };
  QVector<Rule> rules_;
};

} // namespace muffin

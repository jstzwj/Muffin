#pragma once

#include <QJsonValue>
#include <QString>
#include <QVector>

#include <optional>
#include <stdexcept>

namespace muffin::mermaid::gitgraph {

enum class GitGraphErrorKind { Lexer, Parser, Runtime };

class GitGraphParseError : public std::runtime_error {
public:
  GitGraphParseError(GitGraphErrorKind kind, int line, int column,
                     QString token, const QString& message);

  GitGraphErrorKind kind;
  int line;
  int column;
  QString token;
};

enum class CommitType { Normal = 0, Reverse = 1, Highlight = 2, Merge = 3, CherryPick = 4 };
enum class Direction { LeftToRight, TopToBottom, BottomToTop };

struct GitGraphParseConfig {
  QString mainBranchName = QStringLiteral("main");
  qreal mainBranchOrder = 0.0;
};

struct GitCommit {
  QString id;
  QString message;
  int seq = 0;
  CommitType type = CommitType::Normal;
  QVector<QString> tags;
  QVector<QString> parents;
  QString branch;
  std::optional<CommitType> customType;
  bool customId = false;
};

struct GitBranch {
  QString name;
  QString head;
  bool hasHead = false;
  std::optional<qreal> order;
};

struct GitGraphData {
  QString title;
  QString accTitle;
  QString accDescr;
  Direction direction = Direction::LeftToRight;
  QString currentBranch;
  QVector<GitCommit> commits;
  QVector<GitBranch> branches;
  QVector<QString> orderedBranches;
};

class GitGraphDiagram {
public:
  static GitGraphData parse(const QString& source,
                            const GitGraphParseConfig& config = {});
};

}  // namespace muffin::mermaid::gitgraph

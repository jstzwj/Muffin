#pragma once
#include "document/CooperativeTask.h"

#include <QHash>
#include <array>
#include <memory>

#include "document/NodeId.h"
#include "theme/CssContent.h"

namespace muffin {

struct DocumentCounterStats {
  qsizetype blocks = 0, semanticShapes = 0, semanticBytes = 0, unsharedSemanticBytes = 0;
  qsizetype memoizedBlocks = 0, sharedStates = 0;
  qsizetype semanticBuilds = 0, candidateScans = 0;
};

// Counter programs contain values and stable semantic keys, never live AST or
// CSS adapter pointers. They survive a local document style invalidation.
struct CounterProgram {
  using Operations = QVector<QPair<QString, int>>;
  std::array<Operations, 3> operations; // reset, increment, set
  QString key;
  QByteArray dependencies;
  NodeId owner;
  bool documentRoot = false;
  std::vector<ContentToken> content;
  std::shared_ptr<const CounterProgram> before, after;
  QVector<std::shared_ptr<const CounterProgram>> children;
};

class DocumentCounterCache {
public:
  using Program = std::shared_ptr<const CounterProgram>;
  struct Block {
    NodeId id;
    Program program;
    QByteArray semantic;
  };
  QVector<Block> blocks;
  Program host;
  bool contextual = false;
  qsizetype semanticBuilds = 0, candidateScans = 0;
  // Shares only exact shape storage/reference counts, never CSS programs or
  // counter values. Caller verifies identical node ids/order and revision.
  void reuseSemanticPool(const DocumentCounterCache& seed);
  QByteArray retainSemantic(QByteArray value);
  void releaseSemantic(const QByteArray& value);
  qsizetype uniqueSemanticCount() const { return semanticReferences_.size(); }
  DocumentCounterStats stats() const;
  void evaluate();
  CooperativeTask evaluateSteps();
  QHash<QString, QHash<QString, QVector<int>>> chains;
  QHash<NodeId, quint64> fingerprints;
  QHash<NodeId, QPair<QString, QString>> text;

private:
  struct Value {
    int depth, value;
    bool operator==(const Value&) const = default;
  };
  using Snapshot = QHash<QString, QVector<Value>>;
  struct Memo {
    Program program;
    std::shared_ptr<const Snapshot> input, output;
    QHash<QString, QHash<QString, QVector<int>>> chains;
    QHash<NodeId, quint64> fingerprints;
    QHash<NodeId, QPair<QString, QString>> text;
  };
  QHash<NodeId, Memo> memo_;
  QHash<QByteArray, qsizetype> semanticReferences_;
  QHash<quint64, std::vector<std::weak_ptr<const Snapshot>>> statePool_;
};
} // namespace muffin

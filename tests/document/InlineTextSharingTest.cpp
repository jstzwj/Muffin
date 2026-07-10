#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "parser/MarkdownSerializer.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

qsizetype countShared(const QVector<InlineNode>& inlines) {
  qsizetype count = 0;
  for (const InlineNode& node : inlines) {
    count += node.usesSharedText() ? 1 : 0;
    count += countShared(node.children());
  }
  return count;
}

qsizetype countShared(const MarkdownNode& node) {
  qsizetype count = countShared(node.inlines());
  for (const auto& child : node.children()) {
    count += countShared(*child);
  }
  return count;
}

MarkdownNode* firstNodeOfType(MarkdownNode& node, BlockType type) {
  if (node.type() == type) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = firstNodeOfType(*child, type)) {
      return found;
    }
  }
  return nullptr;
}

void testLiveTreeSharesSourceText() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# Shared heading\n\nPlain **shared** text."), false);
  require(countShared(session.document().root()) >= 3, "parsed inline text was not source-shared");
}

void testCloneOwnsTextAndSurvivesDocumentReplacement() {
  DocumentSession session;
  const QString tableText = QStringLiteral("| Alpha | Beta |\n| --- | --- |\n| One | Two |");
  session.setMarkdownText(tableText, false);
  MarkdownNode* table = firstNodeOfType(session.document().root(), BlockType::Table);
  require(table != nullptr, "table parse failed");
  require(countShared(*table) > 0, "table inline text was not shared");

  std::unique_ptr<MarkdownNode> snapshot = table->clone(CloneMode::PreserveIds);
  require(countShared(*snapshot) == 0, "undo snapshot retained shared source slices");
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeBlock(*snapshot);

  session.setMarkdownText(QStringLiteral("replacement document"), false);
  require(serializer.serializeBlock(*snapshot) == serialized, "snapshot text changed with live document");
  require(serialized.contains(QStringLiteral("Alpha")) && serialized.contains(QStringLiteral("Two")),
          "snapshot serialization lost table cell text");
}

void testLocalReparseSharesSliceText() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbravo"), false);
  require(session.applyTextDelta(9, 0, QStringLiteral("X"), true), "local edit failed");
  require(session.markdownText().toString() == QStringLiteral("alpha\n\nbrXavo"), "local edit text mismatch");
  require(countShared(session.document().root()) > 0, "local reparse did not bind shared slice text");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testLiveTreeSharesSourceText();
  testCloneOwnsTextAndSurvivesDocumentReplacement();
  testLocalReparseSharesSliceText();
  return 0;
}

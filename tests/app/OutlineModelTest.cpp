#include "app/OutlineModel.h"

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

QVector<OutlineEntry> sampleEntries() {
  return {
      {QStringLiteral("Root"), 1, NodeId::create(), SourceRange{}, -1},
      {QStringLiteral("Child"), 2, NodeId::create(), SourceRange{}, 0},
      {QStringLiteral("Grandchild"), 3, NodeId::create(), SourceRange{}, 1},
      {QStringLiteral("Second root"), 1, NodeId::create(), SourceRange{}, -1},
  };
}

void testFlatModel() {
  OutlineModel model;
  model.setEntries(sampleEntries());
  require(model.rowCount() == 4, "flat outline should expose every heading at root");
  const QModelIndex child = model.index(1, 0);
  require(child.isValid(), "flat child index missing");
  require(!model.parent(child).isValid(), "flat outline entries must not have model parents");
  require(model.data(child).toString().endsWith(QStringLiteral("Child")), "flat title mismatch");
  require(model.data(child).toString() != QStringLiteral("Child"), "flat child should include indentation");
  require(model.entry(child) && model.entry(child)->parentIndex == 0, "entry lookup lost source hierarchy");
}

void testFoldableModel() {
  OutlineModel model;
  model.setFoldable(true);
  model.setEntries(sampleEntries());
  require(model.rowCount() == 2, "foldable outline should expose two roots");
  const QModelIndex root = model.index(0, 0);
  require(model.rowCount(root) == 1, "root should have one child");
  const QModelIndex child = model.index(0, 0, root);
  require(model.data(child).toString() == QStringLiteral("Child"), "foldable title must not be padded");
  require(model.parent(child) == root, "child parent mismatch");
  const QModelIndex grandchild = model.index(0, 0, child);
  require(model.parent(grandchild) == child, "grandchild parent mismatch");
  model.clear();
  require(model.isEmpty() && model.rowCount() == 0, "clear should release all outline rows");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testFlatModel();
  testFoldableModel();
  return 0;
}

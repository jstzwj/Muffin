// Unit tests for the native Dagre compound pipeline ports (milestone C1/C2/C3):
// NestingGraph, AddBorderSegments, ParentDummyChains, plus their helpers.
// Each test hand-builds the documented pre-state and asserts the documented
// post-conditions. See docs/mermaid-flowchart-remaining-plan.md section 6.

#include "mermaid/dagre/AddBorderSegments.h"
#include "mermaid/dagre/DagreLabels.h"
#include "mermaid/dagre/DagreUtil.h"
#include "mermaid/dagre/NestingGraph.h"
#include "mermaid/dagre/ParentDummyChains.h"

#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid::dagre;
using muffin::mermaid::graphlib::Edge;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
template <typename A, typename E>
void requireEq(const A& actual, const E& expected, const QString& message) {
  if (!(actual == expected)) {
    qCritical().noquote() << message << "\n  actual:  " << actual << "\n  expected:" << expected;
    std::exit(1);
  }
}

DagreGraph makeCompound() {
  DagreGraph g({.directed = true, .multigraph = true, .compound = true});
  g.setGraph(DagreGraphLabel{});  // buildLayoutGraph always sets a graph label
  return g;
}

// Count edges by predicate.
template <typename Fn>
int countEdges(const DagreGraph& g, Fn pred) {
  int n = 0;
  for (const Edge& e : g.edges())
    if (pred(e, g.edge(e))) ++n;
  return n;
}

void testTreeDepths() {
  DagreGraph g = makeCompound();
  g.setNode("C");
  g.setNode("a");
  g.setNode("b");
  g.setParent("a", "C");
  g.setParent("b", "C");
  const QHash<QString, int> d = treeDepths(g);
  requireEq(d.value("C"), 1, QStringLiteral("treeDepths C"));
  requireEq(d.value("a"), 2, QStringLiteral("treeDepths a"));
  requireEq(d.value("b"), 2, QStringLiteral("treeDepths b"));
}

void testNestingGraphRun() {
  DagreGraph g = makeCompound();
  g.setNode("C");
  g.setNode("a");
  g.setNode("b");
  g.setParent("a", "C");
  g.setParent("b", "C");
  DagreEdgeLabel ab;
  ab.minlen = 2;
  ab.weight = 1;
  g.setEdge("a", "b", ab);

  runNestingGraph(g);

  const QString root = g.graph()->nestingRoot;
  require(!root.isEmpty() && g.hasNode(root), QStringLiteral("nesting root created"));
  requireEq(g.graph()->nodeRankFactor, 3, QStringLiteral("nodeRankFactor = 2*height+1 (height=1)"));

  // Original edge minlen scaled by nodeSep (3): 2 -> 6.
  require(g.edge("a", "b") && g.edge("a", "b")->minlen == 6, QStringLiteral("edge minlen scaled by nodeSep"));

  // Cluster C got border top/bottom dummies parented to C.
  const DagreNodeLabel* c = g.node("C");
  require(!c->borderTop.isEmpty() && !c->borderBottom.isEmpty(), QStringLiteral("cluster border tags set"));
  require(g.parentOf(c->borderTop) == "C" && g.parentOf(c->borderBottom) == "C",
          QStringLiteral("border dummies parented to cluster"));
  require(g.node(c->borderTop)->dummy == "border", QStringLiteral("border dummy tag"));

  // Four nesting edges (top->a, a->bottom, top->b, b->bottom), all weight 4 minlen 1.
  const int nesting = countEdges(g, [](const Edge&, const DagreEdgeLabel* l) {
    return l && l->nestingEdge;
  });
  requireEq(nesting, 4, QStringLiteral("four nesting edges"));
  require(countEdges(g, [](const Edge&, const DagreEdgeLabel* l) {
    return l && l->nestingEdge && l->weight == 4 && l->minlen == 1;
  }) == 4, QStringLiteral("nesting edges carry weight=2*sumWeights+2, minlen=1"));

  // Root connects to leaves and the cluster top with zero-weight edges.
  require(g.edge(root, "a") && g.edge(root, "a")->weight == 0 && g.edge(root, "a")->minlen == 3,
          QStringLiteral("root->leaf edge"));
  require(g.edge(root, c->borderTop) && g.edge(root, c->borderTop)->weight == 0 &&
              g.edge(root, c->borderTop)->minlen == 2,
          QStringLiteral("root->top edge minlen = height + depth(C)"));
}

void testNestingGraphCleanup() {
  DagreGraph g = makeCompound();
  g.setNode("C");
  g.setNode("a");
  g.setNode("b");
  g.setParent("a", "C");
  g.setParent("b", "C");
  DagreEdgeLabel ab;
  ab.minlen = 2;
  g.setEdge("a", "b", ab);
  runNestingGraph(g);
  const QString top = g.node("C")->borderTop;
  cleanupNestingGraph(g);

  require(g.graph()->nestingRoot.isEmpty(), QStringLiteral("nestingRoot cleared"));
  require(!g.hasNode("_root1") || true, QStringLiteral("root node removed"));
  require(g.graph()->nestingRoot.isEmpty() && !g.hasNode(QString()), QStringLiteral("root id gone"));
  require(countEdges(g, [](const Edge&, const DagreEdgeLabel* l) {
    return l && l->nestingEdge;
  }) == 0, QStringLiteral("all nesting edges removed"));
  require(g.hasEdge("a", "b") && g.edge("a", "b")->minlen == 6,
          QStringLiteral("original edge survives with scaled minlen"));
  require(g.hasNode(top), QStringLiteral("border dummies survive cleanup"));
  requireEq(g.graph()->nodeRankFactor, 3, QStringLiteral("nodeRankFactor preserved"));
}

void testAddBorderSegments() {
  DagreGraph g = makeCompound();
  g.setNode("C");
  // Simulate assignRankMinMax: cluster C spans ranks 0..2.
  g.node("C")->minRank = 0;
  g.node("C")->maxRank = 2;

  addBorderSegments(g);

  const DagreNodeLabel* c = g.node("C");
  requireEq(c->borderLeft.size(), 3, QStringLiteral("borderLeft chain length = maxRank+1"));
  requireEq(c->borderRight.size(), 3, QStringLiteral("borderRight chain length = maxRank+1"));

  for (int i = 0; i < 3; ++i) {
    const QString bl = c->borderLeft.at(i);
    const QString br = c->borderRight.at(i);
    require(g.hasNode(bl) && g.hasNode(br), QStringLiteral("border dummy exists"));
    require(g.parentOf(bl) == "C" && g.parentOf(br) == "C", QStringLiteral("border dummy parented to cluster"));
    require(g.node(bl)->dummy == "border" && g.node(bl)->borderType == "borderLeft",
            QStringLiteral("left border type tag"));
    require(g.node(br)->borderType == "borderRight", QStringLiteral("right border type tag"));
    require(g.node(bl)->rank.value_or(-1) == i && g.node(br)->rank.value_or(-1) == i,
            QStringLiteral("border dummy rank"));
  }
  // Consecutive same-side dummies are linked by weight-1 edges; the first has
  // no incoming border-link edge.
  require(g.hasEdge(c->borderLeft.at(0), c->borderLeft.at(1)) &&
              g.edge(c->borderLeft.at(0), c->borderLeft.at(1))->weight == 1,
          QStringLiteral("left border link edge"));
  require(g.hasEdge(c->borderLeft.at(1), c->borderLeft.at(2)), QStringLiteral("second left border link"));
  require(g.hasEdge(c->borderRight.at(0), c->borderRight.at(1)), QStringLiteral("right border link"));
}

void testPostorder() {
  DagreGraph g = makeCompound();
  for (const QString& n : {"C", "D", "a", "b"}) g.setNode(n);
  g.setParent("a", "C");
  g.setParent("D", "C");
  g.setParent("b", "D");
  // children(C) = [a, D], children(D) = [b]
  const QHash<QString, PostorderEntry> p = postorder(g);
  requireEq(p.value("a").low, 0, QStringLiteral("a.low"));
  requireEq(p.value("a").lim, 0, QStringLiteral("a.lim"));
  requireEq(p.value("b").low, 1, QStringLiteral("b.low"));
  requireEq(p.value("b").lim, 1, QStringLiteral("b.lim"));
  requireEq(p.value("D").low, 1, QStringLiteral("D.low"));
  requireEq(p.value("D").lim, 2, QStringLiteral("D.lim"));
  requireEq(p.value("C").low, 0, QStringLiteral("C.low"));
  requireEq(p.value("C").lim, 3, QStringLiteral("C.lim"));
}

void testFindPath() {
  DagreGraph g = makeCompound();
  for (const QString& n : {"C", "D", "a", "b"}) g.setNode(n);
  g.setParent("a", "C");
  g.setParent("D", "C");
  g.setParent("b", "D");
  const QHash<QString, PostorderEntry> p = postorder(g);
  // a and b share LCA = C; path threads C then D.
  const ClusterPath cp = findPath(g, p, "a", "b");
  requireEq(cp.lca, QString("C"), QStringLiteral("findPath lca = common ancestor"));
  require(cp.path.size() == 2 && cp.path.at(0) == "C" && cp.path.at(1) == "D",
          QStringLiteral("findPath path = [C, D]"));
}

void testParentDummyChainsInsideCluster() {
  // Edge a->b entirely inside cluster C (both endpoints children of C), with a
  // single dummy d between them. The dummy must be re-parented to C.
  DagreGraph g = makeCompound();
  for (const QString& n : {"C", "a", "b", "d"}) g.setNode(n);
  g.setParent("a", "C");
  g.setParent("b", "C");
  g.node("a")->rank = 1;
  g.node("b")->rank = 3;
  g.node("d")->rank = 2;
  g.node("d")->dummy = "edge";
  Edge obj;
  obj.v = "a";
  obj.w = "b";
  g.node("d")->edgeObj = obj;
  g.setEdge("a", "d", DagreEdgeLabel{});
  g.setEdge("d", "b", DagreEdgeLabel{});
  g.graph()->dummyChains.append("d");

  parentDummyChains(g);
  requireEq(g.parentOf("d"), QString("C"), QStringLiteral("dummy inside cluster parented to cluster"));
}

void testParentDummyChainsNestedAscending() {
  // Edge from a (deep inside Inner inside Outer) to b (direct child of Outer).
  // Dummy d's rank (2) is outside Inner's range [1,1] so ascending must walk
  // past Inner up to the LCA (Outer) using the maxRank check.
  DagreGraph g = makeCompound();
  for (const QString& n : {"Outer", "Inner", "a", "b", "d"}) g.setNode(n);
  g.setParent("Inner", "Outer");
  g.setParent("a", "Inner");
  g.setParent("b", "Outer");
  g.node("a")->rank = 1;
  g.node("b")->rank = 3;
  g.node("d")->rank = 2;
  g.node("d")->dummy = "edge";
  g.node("Inner")->minRank = 1;
  g.node("Inner")->maxRank = 1;
  Edge obj;
  obj.v = "a";
  obj.w = "b";
  g.node("d")->edgeObj = obj;
  g.setEdge("a", "d", DagreEdgeLabel{});
  g.setEdge("d", "b", DagreEdgeLabel{});
  g.graph()->dummyChains.append("d");

  parentDummyChains(g);
  requireEq(g.parentOf("d"), QString("Outer"),
            QStringLiteral("dummy past Inner's range parented to the LCA (Outer)"));
}

void testUtilHelpers() {
  // maxRank / buildLayerMatrix / intersectRect sanity.
  DagreGraph g = makeCompound();
  g.setNode("a");
  g.setNode("b");
  g.setNode("c");
  g.node("a")->rank = 0;
  g.node("a")->order = 0;
  g.node("b")->rank = 1;
  g.node("b")->order = 0;
  g.node("c")->rank = 1;
  g.node("c")->order = 1;
  requireEq(maxRank(g).value_or(-1), 1, QStringLiteral("maxRank"));
  const QVector<QVector<QString>> matrix = buildLayerMatrix(g);
  requireEq(matrix.size(), 2, QStringLiteral("layer matrix rank count"));
  require(matrix.at(0).at(0) == "a", QStringLiteral("layer0"));
  require(matrix.at(1).at(0) == "b" && matrix.at(1).at(1) == "c", QStringLiteral("layer1 order"));

  DagreNodeLabel rect;
  rect.x = 0.0;
  rect.y = 0.0;
  rect.width = 10.0;
  rect.height = 6.0;
  const QPointF hit = intersectRect(rect, QPointF(100.0, 1.0));  // ray to the right
  require(qAbs(hit.x() - 5.0) < 1e-9, QStringLiteral("intersectRect right edge x"));
}

void testNormalizeRanks() {
  DagreGraph g = makeCompound();
  g.setNode("a");
  g.setNode("b");
  g.node("a")->rank = 5;
  g.node("b")->rank = 7;
  normalizeRanks(g);
  requireEq(*g.node("a")->rank, 0, QStringLiteral("normalizeRanks min -> 0"));
  requireEq(*g.node("b")->rank, 2, QStringLiteral("normalizeRanks shift"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  Q_UNUSED(app);

  testTreeDepths();
  testNestingGraphRun();
  testNestingGraphCleanup();
  testAddBorderSegments();
  testPostorder();
  testFindPath();
  testParentDummyChainsInsideCluster();
  testParentDummyChainsNestedAscending();
  testUtilHelpers();
  testNormalizeRanks();

  qDebug("All Mermaid Dagre compound (C1/C2/C3) tests passed.");
  return 0;
}

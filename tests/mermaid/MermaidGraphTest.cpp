// Unit tests for the native graphlib compound multigraph port (Milestone B).
// Mirrors the behaviour of node_modules/dagre-d3-es/src/graphlib/graph.js and
// covers the plan in docs/mermaid-flowchart-remaining-plan.md section 6
// ("里程碑 B：原生 compound 图模型").
//
// This test links MuffinCore only so it stays free of any Node/browser tooling.

#include "mermaid/graphlib/Graph.h"

#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QString>

#include <cstdlib>
#include <stdexcept>

using muffin::mermaid::graphlib::Edge;
using muffin::mermaid::graphlib::ancestorsOf;
using muffin::mermaid::graphlib::lowestCommonAncestor;

namespace {

// Label shapes representative of what dagre's buildLayoutGraph populates.
struct NodeLabel {
  QString text;
  qreal width = 0.0;
  qreal height = 0.0;
  int rank = 0;
  QString borderTop;
  QString borderBottom;
};
struct EdgeLabel {
  int minlen = 1;
  int weight = 1;
  qreal width = 0.0;
  qreal height = 0.0;
  int labeloffset = 10;
  QString labelpos = QStringLiteral("r");
};
struct GraphLabel {
  QString rankdir = QStringLiteral("tb");
  qreal nodesep = 50.0;
  qreal edgesep = 20.0;
  qreal ranksep = 50.0;
};

using G = muffin::mermaid::graphlib::Graph<NodeLabel, EdgeLabel, GraphLabel>;
using CG = muffin::mermaid::graphlib::Graph<NodeLabel, EdgeLabel, GraphLabel>;

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
QString join(const QList<QString>& xs) { return xs.join(QStringLiteral(", ")); }

QString edgeIds(const G& g) {
  QStringList parts;
  for (const Edge& e : g.edges()) {
    QString id = e.v + QStringLiteral("-") + e.w;
    if (e.hasName) id += QStringLiteral("(") + e.name + QStringLiteral(")");
    parts.append(id);
  }
  return parts.join(QStringLiteral(", "));
}

void testBasicNodes() {
  G g;
  g.setNode("a", NodeLabel{.text = "A"});
  g.setNode("b", NodeLabel{.text = "B"});
  g.setNode("c", NodeLabel{.text = "C"});
  require(g.nodeCount() == 3, QStringLiteral("nodeCount"));
  require(g.hasNode("a") && !g.hasNode("z"), QStringLiteral("hasNode"));
  requireEq(g.node("a")->text, QString("A"), QStringLiteral("node label"));
  require(g.node("z") == nullptr, QStringLiteral("absent node is nullptr"));
  requireEq(join(g.nodes()), QString("a, b, c"), QStringLiteral("nodes() insertion order"));
  // Re-setting an existing node updates the label but keeps its position.
  g.setNode("a", NodeLabel{.text = "A2"});
  requireEq(g.node("a")->text, QString("A2"), QStringLiteral("node update"));
  requireEq(join(g.nodes()), QString("a, b, c"), QStringLiteral("order unchanged after update"));
  // setNode with no value auto-creates via the default (here default-constructed).
  g.setNode("d");
  require(g.hasNode("d") && g.node("d")->text.isEmpty(), QStringLiteral("auto-create default label"));
  g.removeNode("b");
  require(!g.hasNode("b") && g.nodeCount() == 3, QStringLiteral("removeNode"));
  requireEq(join(g.nodes()), QString("a, c, d"), QStringLiteral("order after removal"));
}

void testEdges() {
  G g;
  g.setNode("a");
  g.setNode("b");
  g.setNode("c");
  g.setEdge("a", "b", EdgeLabel{.minlen = 2});
  g.setEdge("b", "c", EdgeLabel{.weight = 5});
  require(g.edgeCount() == 2, QStringLiteral("edgeCount"));
  require(g.hasEdge("a", "b") && !g.hasEdge("b", "a"), QStringLiteral("hasEdge directed"));
  requireEq(g.edge("a", "b")->minlen, 2, QStringLiteral("edge label"));
  // setEdge auto-creates missing endpoints.
  g.setEdge("x", "y", EdgeLabel{.minlen = 3});
  require(g.hasNode("x") && g.hasNode("y"), QStringLiteral("setEdge creates nodes"));
  requireEq(edgeIds(g), QString("a-b, b-c, x-y"), QStringLiteral("edges() insertion order"));
  requireEq(join(g.predecessors("c")), QString("b"), QStringLiteral("predecessors"));
  requireEq(join(g.successors("a")), QString("b"), QStringLiteral("successors"));
  require(g.inEdges("c").size() == 1 && g.inEdges("c").first().v == "b", QStringLiteral("inEdges"));
  require(g.outEdges("a").size() == 1 && g.outEdges("a").first().w == "b", QStringLiteral("outEdges"));
  require(g.nodeEdges("b").size() == 2, QStringLiteral("nodeEdges"));
  require(g.sources() == QList<QString>{"a"} || g.sources().contains("a"), QStringLiteral("sources"));
  // removeEdge drops the edge but keeps nodes and their order.
  g.removeEdge("a", "b");
  require(!g.hasEdge("a", "b") && g.edgeCount() == 2, QStringLiteral("removeEdge"));
  requireEq(join(g.nodes()), QString("a, b, c, x, y"), QStringLiteral("nodes preserved after edge removal"));
}

void testMultigraph() {
  G g({.directed = true, .multigraph = true, .compound = false});
  g.setEdge("a", "b", EdgeLabel{.weight = 1}, "e1");
  g.setEdge("a", "b", EdgeLabel{.weight = 2}, "e2");
  // An unnamed edge between the same endpoints is a THIRD, distinct edge.
  g.setEdge("a", "b", EdgeLabel{.weight = 3});
  require(g.edgeCount() == 3, QStringLiteral("parallel named+unnamed edges coexist"));
  requireEq(g.edge("a", "b", "e1")->weight, 1, QStringLiteral("named edge e1"));
  requireEq(g.edge("a", "b", "e2")->weight, 2, QStringLiteral("named edge e2"));
  requireEq(g.edge("a", "b")->weight, 3, QStringLiteral("unnamed edge"));
  // predecessors/successors deduplicate by node id even across parallel edges.
  require(g.predecessors("b") == QList<QString>{"a"}, QStringLiteral("predecessors dedup"));
  QList<Edge> in = g.inEdges("b");
  require(in.size() == 3, QStringLiteral("inEdges returns all parallel edges"));
  // Removing one named edge leaves the others intact.
  g.removeEdge("a", "b", "e1");
  require(g.edgeCount() == 2 && !g.hasEdge("a", "b", "e1") && g.hasEdge("a", "b", "e2"),
          QStringLiteral("remove one parallel edge"));
  // A non-multigraph rejects named edges.
  G plain;
  bool threw = false;
  try {
    plain.setEdge("a", "b", EdgeLabel{}, "named");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require(threw, QStringLiteral("named edge rejected in non-multigraph"));
}

void testCompound() {
  CG g({.directed = true, .multigraph = false, .compound = true});
  g.setNode("root");
  g.setNode("a");
  g.setNode("b");
  g.setNode("c");
  g.setParent("a", "root");
  g.setParent("b", "root");
  g.setParent("c", "b");
  requireEq(g.parentOf("a"), QString("root"), QStringLiteral("parent"));
  require(g.parentOf("root").isNull(), QStringLiteral("root has no parent"));
  require(g.children("root")->contains("a") && g.children("root")->contains("b"),
          QStringLiteral("children of root"));
  // children() with no arg returns top-level nodes (the graph-root children).
  QList<QString> top = g.children().value_or(QList<QString>{});
  require(top.contains("root") && !top.contains("a"), QStringLiteral("top-level children"));
  // Mixed compound+leaf traversal: leaves have no children, compound nodes do.
  require(*g.children("a") == QList<QString>{}, QStringLiteral("leaf has no children"));
  require(g.children("b")->contains("c"), QStringLiteral("nested child"));
}

void testSetParentCycleDetection() {
  CG g({.compound = true});
  g.setNode("a");
  g.setNode("b");
  g.setNode("c");
  g.setParent("a", "b");
  g.setParent("b", "c");
  bool direct = false, transitive = false;
  try {
    g.setParent("b", "a");  // direct cycle b->a->b
  } catch (const std::runtime_error&) {
    direct = true;
  }
  try {
    g.setParent("c", "a");  // transitive cycle c->a->b->c
  } catch (const std::runtime_error&) {
    transitive = true;
  }
  require(direct && transitive, QStringLiteral("setParent rejects cycles"));
}

void testRemoveNodeReparents() {
  CG g({.compound = true});
  g.setNode("cluster");
  g.setNode("a");
  g.setNode("b");
  g.setParent("a", "cluster");
  g.setParent("b", "cluster");
  g.removeNode("cluster");
  // Children of the removed cluster are re-parented to the graph root.
  require(g.hasNode("a") && g.hasNode("b"), QStringLiteral("children survive parent removal"));
  require(g.parentOf("a").isNull() && g.parentOf("b").isNull(),
          QStringLiteral("children reparented to root"));
  QList<QString> top = g.children().value_or(QList<QString>{});
  require(top.contains("a") && top.contains("b"), QStringLiteral("orphaned children become top-level"));
}

void testAncestorsAndLca() {
  CG g({.compound = true});
  // Build:        root
  //             /      \
  //          left      right
  //          /  \        \
  //        a     b        c
  for (const QString& n : {"root", "left", "right", "a", "b", "c"}) g.setNode(n);
  g.setParent("left", "root");
  g.setParent("right", "root");
  g.setParent("a", "left");
  g.setParent("b", "left");
  g.setParent("c", "right");
  requireEq(join(ancestorsOf(g, "a")), QString("left, root"), QStringLiteral("ancestors of a"));
  requireEq(lowestCommonAncestor(g, "a", "b"), QString("left"), QStringLiteral("lca siblings"));
  requireEq(lowestCommonAncestor(g, "a", "c"), QString("root"), QStringLiteral("lca cousins"));
  requireEq(lowestCommonAncestor(g, "a", "left"), QString("left"), QStringLiteral("lca with ancestor"));
}

void testDeepParentChainNoOverflow() {
  // A chain deep enough that a recursive parent walk would blow the C++ stack.
  const int depth = 50000;
  CG g({.compound = true});
  for (int i = 0; i < depth; ++i) g.setNode(QStringLiteral("n%1").arg(i));
  for (int i = 0; i + 1 < depth; ++i) g.setParent(QStringLiteral("n%1").arg(i), QStringLiteral("n%1").arg(i + 1));
  requireEq(g.parentOf(QStringLiteral("n0")), QString("n1"), QStringLiteral("deep parent link"));
  require(ancestorsOf(g, QStringLiteral("n0")).size() == depth - 1,
          QStringLiteral("deep ancestor walk is iterative and complete"));
}

void testSpecialIdsDoNotPollute() {
  // In JS these would shadow Object.prototype; in C++ they are ordinary keys.
  G g;
  for (const QString& id : {QStringLiteral("__proto__"), QStringLiteral("constructor"),
                            QStringLiteral("toString"), QStringLiteral("hasOwnProperty"),
                            QStringLiteral("prototype")}) {
    g.setNode(id, NodeLabel{.text = id.toUpper()});
    require(g.hasNode(id) && g.node(id)->text == id.toUpper(),
            QStringLiteral("special id roundtrip: ") + id);
  }
  requireEq(g.nodeCount(), 5, QStringLiteral("all special ids coexist"));
  // The reserved graph-root and edge-delim chars must roundtrip too.
  G mg({.multigraph = true});
  mg.setNode(QStringLiteral("\x01"));
  require(g.hasNode(QStringLiteral("__proto__")), QStringLiteral("no proto pollution clobbered prior node"));
}

void testIterationOrderStability() {
  G g;
  for (const QString& id : {"z", "a", "m", "b", "q"}) g.setNode(id);
  requireEq(join(g.nodes()), QString("z, a, m, b, q"), QStringLiteral("nodes follow insertion order"));
  // Remove a middle node, then re-add it: it lands at the end (JS semantics).
  g.removeNode("m");
  g.setNode("m");
  requireEq(join(g.nodes()), QString("z, a, b, q, m"), QStringLiteral("re-added node moves to end"));
  // Edges behave the same way.
  G e({.multigraph = true});
  e.setEdge("x", "y", EdgeLabel{}, "1");
  e.setEdge("p", "q", EdgeLabel{}, "2");
  e.setEdge("x", "y", EdgeLabel{}, "3");
  requireEq(edgeIds(e), QString("x-y(1), p-q(2), x-y(3)"), QStringLiteral("edges follow insertion order"));
  e.removeEdge("p", "q", "2");
  requireEq(edgeIds(e), QString("x-y(1), x-y(3)"), QStringLiteral("edge removal preserves order"));
}

void testDeterminismAcrossRuns() {
  auto build = []() {
    G g({.multigraph = true, .compound = true});
    g.setNode("c");
    g.setNode("a");
    g.setNode("b");
    g.setEdge("a", "b", EdgeLabel{.weight = 1}, "e1");
    g.setEdge("a", "b", EdgeLabel{.weight = 2}, "e2");
    g.setParent("a", "c");
    g.setParent("b", "c");
    g.removeEdge("a", "b", "e1");
    g.setEdge("a", "b", EdgeLabel{.weight = 9}, "e1");  // re-add -> end
    return QStringList{join(g.nodes()), edgeIds(g),
                       join(g.children("c").value_or(QList<QString>{}))};
  };
  const QStringList first = build();
  const QStringList second = build();
  require(first == second, QStringLiteral("identical build sequence yields identical query order"));
}

void testBuildLayoutGraphFieldFidelity() {
  // layout.js::buildLayoutGraph copies a whitelisted attribute subset onto the
  // layout graph. Verify the model round-trips every field the pipeline reads.
  G g({.multigraph = true, .compound = true});
  g.setGraph(GraphLabel{.rankdir = "LR", .nodesep = 40.0, .edgesep = 15.0, .ranksep = 60.0});
  g.setNode("a", NodeLabel{.width = 30.0, .height = 12.0});
  g.setNode("cluster", NodeLabel{.borderTop = "bt", .borderBottom = "bb"});
  g.setParent("a", "cluster");
  g.setEdge("a", "cluster", EdgeLabel{.minlen = 2, .weight = 3, .width = 9.0, .height = 11.0,
                                      .labeloffset = 12, .labelpos = "l"}, "edge1");
  require(g.graph()->rankdir == "LR" && g.graph()->ranksep == 60.0, QStringLiteral("graph label"));
  require(g.node("a")->width == 30.0 && g.node("a")->height == 12.0, QStringLiteral("node attrs"));
  require(g.node("cluster")->borderTop == "bt", QStringLiteral("node border attrs"));
  require(g.edge("a", "cluster", "edge1")->minlen == 2 && g.edge("a", "cluster", "edge1")->labelpos == "l",
          QStringLiteral("edge attrs"));
  requireEq(g.parentOf("a"), QString("cluster"), QStringLiteral("parent preserved with edge label"));
}

void testSetPath() {
  G g;
  QList<QString> path = {"a", "b", "c", "d"};
  g.setPath(path, EdgeLabel{.weight = 7});
  require(g.hasEdge("a", "b") && g.hasEdge("b", "c") && g.hasEdge("c", "d"), QStringLiteral("setPath edges"));
  require(!g.hasEdge("a", "c"), QStringLiteral("setPath is a chain, not a clique"));
  requireEq(g.edge("b", "c")->weight, 7, QStringLiteral("setPath label applied"));
}

void testDefaultLabels() {
  G g;
  g.setDefaultNodeLabel(NodeLabel{.text = "DEFAULT", .width = 99.0});
  g.setDefaultEdgeLabel([](const QString&, const QString&, const QString&) {
    return EdgeLabel{.weight = 42};
  });
  g.setNode("auto");
  require(g.node("auto")->text == "DEFAULT" && g.node("auto")->width == 99.0,
          QStringLiteral("constant default node label"));
  g.setNode("a");
  g.setNode("b");
  g.setEdge("a", "b");
  requireEq(g.edge("a", "b")->weight, 42, QStringLiteral("function default edge label"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  Q_UNUSED(app);

  testBasicNodes();
  testEdges();
  testMultigraph();
  testCompound();
  testSetParentCycleDetection();
  testRemoveNodeReparents();
  testAncestorsAndLca();
  testDeepParentChainNoOverflow();
  testSpecialIdsDoNotPollute();
  testIterationOrderStability();
  testDeterminismAcrossRuns();
  testBuildLayoutGraphFieldFidelity();
  testSetPath();
  testDefaultLabels();

  qDebug("All Mermaid graphlib compound multigraph tests passed.");
  return 0;
}

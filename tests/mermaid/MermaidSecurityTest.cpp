// Milestone H2 security test. Asserts the parser FLAGS unsafe click-href URLs
// (FlowVertex::linkUnsafe) while PRESERVING the raw link for AST fidelity, and
// that MermaidSecurityPolicy enforces it at render time (Strict drops, Loose
// passes through; callbacks never execute).

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/security/MermaidSecurityPolicy.h"

#include <QDebug>

#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

// Find the first vertex with the given id (parser preserves source order).
const flowchart::FlowVertex* vertexById(const flowchart::FlowchartData& data, const QString& id) {
  for (const flowchart::FlowVertex& v : data.vertices)
    if (v.id == id) return &v;
  return nullptr;
}

// Parse a single-statement flowchart and return the named vertex.
const flowchart::FlowVertex* parseVertex(const QString& body, const QString& id) {
  const flowchart::Flowchart chart = flowchart::Flowchart::parse(QStringLiteral("flowchart TB\nA[Alpha]\n") + body);
  return vertexById(chart.data(), id);
}
}  // namespace

int main() {
  // Unsafe schemes are flagged but the raw link is preserved.
  const flowchart::FlowVertex* js = parseVertex(QStringLiteral("click A href \"javascript:alert(1)\""), QStringLiteral("A"));
  require(js != nullptr, QStringLiteral("A missing (javascript)"));
  require(js->linkUnsafe, QStringLiteral("javascript: URL should be flagged unsafe (link=%1)").arg(js->link));
  require(!js->link.isEmpty(), QStringLiteral("unsafe link must be PRESERVED for AST fidelity, not dropped"));

  const flowchart::FlowVertex* data = parseVertex(QStringLiteral("click A href \"data:text/html,<b>\""), QStringLiteral("A"));
  require(data != nullptr, QStringLiteral("A missing (data)"));
  require(data->linkUnsafe, QStringLiteral("data: URL should be flagged unsafe"));

  // A safe https URL is not flagged.
  const flowchart::FlowVertex* safe = parseVertex(QStringLiteral("click A href \"https://example.com\""), QStringLiteral("A"));
  require(safe != nullptr, QStringLiteral("A missing (https)"));
  require(!safe->linkUnsafe, QStringLiteral("https URL should NOT be flagged (link=%1)").arg(safe->link));

  // MermaidSecurityPolicy enforcement.
  MermaidSecurityPolicy strict;
  require(strict.allowCallbackExecution() == false, QStringLiteral("callbacks must never execute"));
  require(strict.sanitizeHtmlLabels() == true, QStringLiteral("HTML labels must be sanitized"));
  require(strict.enforceLink(QStringLiteral("https://x"), false) == QStringLiteral("https://x"), QStringLiteral("Strict keeps a safe link"));
  require(strict.enforceLink(QStringLiteral("javascript:alert(1)"), true).isEmpty(), QStringLiteral("Strict drops an unsafe link"));

  MermaidSecurityPolicy loose;
  loose.level = MermaidSecurityLevel::Loose;
  require(loose.enforceLink(QStringLiteral("javascript:alert(1)"), true) == QStringLiteral("javascript:alert(1)"),
          QStringLiteral("Loose passes the link through (editor applies its own sanitization)"));

  qDebug().noquote() << "MermaidSecurityTest: unsafe links flagged + preserved; policy enforces Strict drop / Loose passthrough; callbacks never execute";
  return 0;
}

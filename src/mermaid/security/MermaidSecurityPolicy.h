#pragma once

// Mermaid security policy (milestone H2). This is the RENDER/EXPORT-time
// enforcement layer; the parser only FLAGs unsafe data (FlowVertex::linkUnsafe)
// so the AST stays faithful to upstream. Consumers (the editor + HTML/PDF/image
// export in milestone I) consult this policy to decide what to emit.
//
// Mirrors mermaid's securityLevel (strict/loose/sandbox/antiscript). Muffin
// defaults to Strict and NEVER executes callback JavaScript — there is no JS
// runtime in-process (milestone I forbids Node/Chromium/JS-engine deps), so
// allowCallbackExecution() is a constant false regardless of level.

#include <QString>

namespace muffin::mermaid {

enum class MermaidSecurityLevel { Strict, Loose, Sandbox, Antiscript };

struct MermaidSecurityPolicy {
  MermaidSecurityLevel level = MermaidSecurityLevel::Strict;

  // The link to actually emit: the raw link when it is safe (or under Loose),
  // empty when it is unsafe and the policy drops it. Strict drops; Loose passes
  // through (the editor/export may then apply its own sanitization).
  QString enforceLink(const QString& raw, bool unsafe) const {
    if (level == MermaidSecurityLevel::Loose) return raw;
    return unsafe ? QString() : raw;
  }

  // HTML labels are sanitized before render regardless of level (defence in
  // depth; the editor's HTML sanitizer is applied at the render boundary).
  bool sanitizeHtmlLabels() const { return true; }

  // Muffin never executes mermaid callback JavaScript — no JS runtime exists.
  // Callback NAMES are kept as data (a Qt signal / data attribute) only.
  bool allowCallbackExecution() const { return false; }
};

}  // namespace muffin::mermaid

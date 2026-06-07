#pragma once

#include "document/MarkdownTypes.h"
#include "editor/EditorContext.h"

#include <QString>

namespace muffin {

class LiteralBlockController;

QString sanitizedHtmlPreview(const LiteralBlockController& ctrl);
bool insertFrontMatter(const EditorContext& ctx, LiteralBlockController& ctrl, FrontMatterFormat format);

}  // namespace muffin

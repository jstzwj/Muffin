#include "math/MathRenderer.h"

#include "math/MathBuilder.h"
#include "math/MathFontRegistry.h"
#include "math/MathParseError.h"
#include "math/MathParser.h"

#include <cstdio>

namespace muffin::math {
namespace {

constexpr qreal kKatexLineBoxHeightEm = 1.13636;
constexpr qreal kKatexLineBoxHeightAboveBaselineEm = 0.88889;
constexpr qreal kKatexLineBoxDepthBelowBaselineEm = kKatexLineBoxHeightEm - kKatexLineBoxHeightAboveBaselineEm;

std::unique_ptr<MathRenderNode> makeNamedSpan(QString name, std::vector<std::unique_ptr<MathRenderNode>> children = {}) {
  auto span = std::make_unique<MathRenderNode>();
  span->kind = MathRenderKind::Span;
  span->text = std::move(name);
  span->children = std::move(children);
  for (const auto& child : span->children) {
    span->width += child->width;
    span->height = qMax(span->height, child->height - child->shift);
    span->depth = qMax(span->depth, child->depth + child->shift);
  }
  return span;
}

std::unique_ptr<MathRenderNode> makeStrut() {
  auto strut = std::make_unique<MathRenderNode>();
  strut->kind = MathRenderKind::Span;
  strut->text = QStringLiteral("strut");
  return strut;
}

std::unique_ptr<MathRenderNode> makeBase(std::vector<std::unique_ptr<MathRenderNode>> parts) {
  std::vector<std::unique_ptr<MathRenderNode>> baseChildren;
  baseChildren.push_back(makeStrut());
  for (auto& part : parts) {
    baseChildren.push_back(std::move(part));
  }
  return makeNamedSpan(QStringLiteral("base"), std::move(baseChildren));
}

bool isPostOperatorSpace(const MathRenderNode& node) {
  return node.atomClass == QStringLiteral("mspace") && !node.allowBreak && node.text != QStringLiteral("newline");
}

QString outerAtomClass(const MathRenderNode& node, bool rightSide) {
  if ((node.kind == MathRenderKind::Span || node.kind == MathRenderKind::VList || node.kind == MathRenderKind::Phantom) &&
      !node.children.empty() &&
      (node.atomClass.isEmpty() || node.atomClass == QStringLiteral("mord"))) {
    const auto& child = rightSide ? node.children.back() : node.children.front();
    if (child) {
      const QString nested = outerAtomClass(*child, rightSide);
      if (!nested.isEmpty()) {
        return nested;
      }
    }
  }
  return node.atomClass;
}

bool isBaseBreakNode(const MathRenderNode& node) {
  const QString atomClass = outerAtomClass(node, true);
  return atomClass == QStringLiteral("mbin") || atomClass == QStringLiteral("mrel") || node.allowBreak;
}

std::vector<std::unique_ptr<MathRenderNode>> splitIntoKatexBases(std::unique_ptr<MathRenderNode> body) {
  std::vector<std::unique_ptr<MathRenderNode>> bases;
  if (!body) {
    return bases;
  }
  if (body->kind != MathRenderKind::Span || body->children.empty()) {
    std::vector<std::unique_ptr<MathRenderNode>> parts;
    parts.push_back(std::move(body));
    bases.push_back(makeBase(std::move(parts)));
    return bases;
  }

  std::vector<std::unique_ptr<MathRenderNode>> parts;
  auto& children = body->children;
  for (size_t i = 0; i < children.size(); ++i) {
    parts.push_back(std::move(children.at(i)));
    if (isBaseBreakNode(*parts.back())) {
      bool noBreak = false;
      while (i + 1 < children.size() && children.at(i + 1) && isPostOperatorSpace(*children.at(i + 1))) {
        ++i;
        if (children.at(i)->text == QStringLiteral("nobreak")) {
          noBreak = true;
        }
        parts.push_back(std::move(children.at(i)));
      }
      if (!noBreak) {
        bases.push_back(makeBase(std::move(parts)));
        parts.clear();
      }
    }
  }
  if (!parts.empty()) {
    bases.push_back(makeBase(std::move(parts)));
  }
  return bases;
}

std::unique_ptr<MathRenderNode> wrapKatexRoot(std::unique_ptr<MathRenderNode> body, const MathOptions& options, bool displayMode) {
  auto bases = splitIntoKatexBases(std::move(body));
  if (bases.empty()) {
    bases.push_back(makeBase({}));
  }

  std::vector<std::unique_ptr<MathRenderNode>> htmlChildren;
  for (auto& base : bases) {
    htmlChildren.push_back(std::move(base));
  }
  auto html = makeNamedSpan(QStringLiteral("katex-html"), std::move(htmlChildren));

  std::vector<std::unique_ptr<MathRenderNode>> rootChildren;
  rootChildren.push_back(std::move(html));
  auto root = makeNamedSpan(QStringLiteral("katex"), std::move(rootChildren));

  const qreal em = options.fontPointSize();
  if (!displayMode) {
    root->height = kKatexLineBoxHeightAboveBaselineEm * em;
    root->depth = kKatexLineBoxDepthBelowBaselineEm * em;
  } else {
    root->height = qMax(root->height, kKatexLineBoxHeightAboveBaselineEm * em);
    root->depth = qMax(root->depth, kKatexLineBoxDepthBelowBaselineEm * em);
  }
  return root;
}

}  // namespace

MathLayoutResult MathRenderer::render(const QString& tex, qreal rootFontPixelSize,
                                      const QColor& color, bool displayMode,
                                      qreal maxWidth) const {
  MathSettings settings;
  settings.displayMode = displayMode;
  return render(tex, rootFontPixelSize, color, displayMode, settings, maxWidth);
}

MathLayoutResult MathRenderer::render(const QString& tex, qreal rootFontPixelSize,
                                      const QColor& color, bool displayMode,
                                      const MathSettings& inputSettings,
                                      qreal maxWidth) const {
  MathFontRegistry::ensureLoaded();
  std::fprintf(stderr, "[render] A ensureLoaded\n"); std::fflush(stderr);

  MathSettings settings = inputSettings;
  settings.displayMode = displayMode;
  MathLayoutResult result;
  result.source = tex;
  QVector<MathParseNode> tree;
  try {
    MathParser parser(tex, settings);
    tree = parser.parse();
  } catch (const MathParseError& error) {
    result.error = error.message();
    MathParseNode errorNode;
    errorNode.type = MathNodeType::Error;
    errorNode.text = result.error;
    tree.push_back(std::move(errorNode));
  }
  std::fprintf(stderr, "[render] B parsed\n"); std::fflush(stderr);

  const MathStyle style = displayMode ? MathStyle::display() : MathStyle::textStyle();
  MathOptions options(style, rootFontPixelSize, color, settings);
  MathBuilder builder(options);

  std::fprintf(stderr, "[render] C build\n"); std::fflush(stderr);
  auto built = builder.buildExpression(tree);
  std::fprintf(stderr, "[render] D built\n"); std::fflush(stderr);
  result.root = wrapKatexRoot(std::move(built), options, displayMode);
  std::fprintf(stderr, "[render] E wrapped\n"); std::fflush(stderr);
  if (!result.root) {
    return result;
  }
  result.baseline = result.root->height;
  result.naturalSize = QSizeF(result.root->width, result.root->height + result.root->depth);
  result.size = result.naturalSize;
  if (maxWidth > 0.0 && result.size.width() > maxWidth) {
    result.size.setWidth(maxWidth);
    result.overflow = true;
  }
  return result;
}

}  // namespace muffin::math

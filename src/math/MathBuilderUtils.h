#pragma once

// Geometry, font-classification, and atom-shape helpers shared by MathBuilder,
// MathBuilderSpacing, and MathBuilderArray. These were previously copy-pasted into each
// translation unit's anonymous namespace (14 functions, each duplicated 2–3×). Defined inline
// here so unqualified calls from `namespace muffin::math { namespace { ... } }` resolve to these.

#include "math/MathFontMetrics.h"
#include "math/MathOptions.h"
#include "math/MathParseNode.h"
#include "math/MathRenderNode.h"

#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace muffin::math {

inline qreal axisHeight(const MathOptions& options) {
  return options.fontPointSize() * MathFontMetrics::globalMetrics(options.style().size()).axisHeight;
}

inline qreal ruleThickness(const MathOptions& options) {
  const qreal ruleEm = qMax(MathFontMetrics::globalMetrics(options.style().size()).defaultRuleThickness, options.settings().minRuleThickness);
  return options.fontPointSize() * ruleEm;
}

inline QString fontMetricsNameForClass(const QString& fontClass) {
  if (fontClass == QStringLiteral("mathnormal")) return QStringLiteral("Math-Italic");
  if (fontClass == QStringLiteral("mathit")) return QStringLiteral("Main-Italic");
  if (fontClass == QStringLiteral("amsrm")) return QStringLiteral("AMS-Regular");
  if (fontClass == QStringLiteral("mathbf")) return QStringLiteral("Main-Bold");
  if (fontClass == QStringLiteral("mathcal")) return QStringLiteral("Caligraphic-Regular");
  if (fontClass == QStringLiteral("mathfrak")) return QStringLiteral("Fraktur-Regular");
  if (fontClass == QStringLiteral("sans")) return QStringLiteral("SansSerif-Regular");
  if (fontClass == QStringLiteral("typewriter")) return QStringLiteral("Typewriter-Regular");
  if (fontClass == QStringLiteral("script")) return QStringLiteral("Script-Regular");
  if (fontClass == QStringLiteral("size1")) return QStringLiteral("Size1-Regular");
  if (fontClass == QStringLiteral("size2")) return QStringLiteral("Size2-Regular");
  if (fontClass == QStringLiteral("size3")) return QStringLiteral("Size3-Regular");
  if (fontClass == QStringLiteral("size4")) return QStringLiteral("Size4-Regular");
  return QStringLiteral("Main-Regular");
}

inline qreal italicCorrection(const MathRenderNode& node, const MathOptions& options) {
  if (node.italic != 0.0) {
    return node.italic;
  }
  if (node.kind == MathRenderKind::Span && node.children.size() == 1) {
    return italicCorrection(*node.children.front(), options);
  }
  if (node.text.size() != 1) {
    return 0.0;
  }
  const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(node.fontClass), node.text);
  return metrics ? metrics->italic * options.fontPointSize() : 0.0;
}

inline qreal skewCorrection(const MathRenderNode& node, const MathOptions& options) {
  if (node.kind == MathRenderKind::Span && node.children.size() == 1) {
    return skewCorrection(*node.children.front(), options);
  }
  if (node.text.size() != 1) {
    return 0.0;
  }
  const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(node.fontClass), node.text);
  return metrics ? metrics->skew * options.fontPointSize() : 0.0;
}

inline bool isMathFontCommand(const QString& label) {
  static const QSet<QString> commands{
      QStringLiteral("\\mathrm"),     QStringLiteral("\\mathbf"), QStringLiteral("\\mathit"), QStringLiteral("\\mathnormal"),
      QStringLiteral("\\mathsf"),     QStringLiteral("\\mathtt"), QStringLiteral("\\mathbb"), QStringLiteral("\\mathcal"),
      QStringLiteral("\\mathfrak"),   QStringLiteral("\\mathscr"), QStringLiteral("\\mathsfit"), QStringLiteral("\\Bbb"),
      QStringLiteral("\\bold"),       QStringLiteral("\\frak"),   QStringLiteral("\\rm"),     QStringLiteral("\\sf"),
      QStringLiteral("\\tt"),         QStringLiteral("\\bf"),     QStringLiteral("\\it"),     QStringLiteral("\\cal")};
  return commands.contains(label);
}

inline bool operatorCanGrow(const MathParseNode& node) {
  return node.opSymbol && node.label != QStringLiteral("\\smallint");
}

inline bool operatorUsesLimits(const MathParseNode& node, const MathOptions& options) {
  if (node.explicitLimits) {
    return node.limits;
  }
  return node.limits && (options.style().id() == MathStyle::Display || node.alwaysHandleSupSub);
}

// Unwrap group/color/class wrappers to the single element they carry.
inline const MathParseNode* baseElement(const MathParseNode& node) {
  if ((node.type == MathNodeType::Group || node.type == MathNodeType::Color || node.type == MathNodeType::Class) && node.body.size() == 1) {
    return baseElement(node.body.first());
  }
  return &node;
}

inline bool isCharacterBoxNode(const MathParseNode& node) {
  const MathParseNode* base = baseElement(node);
  if (base == nullptr) {
    return false;
  }
  return base->type == MathNodeType::Ord || base->type == MathNodeType::Text || base->type == MathNodeType::Binary ||
         base->type == MathNodeType::Relation || base->type == MathNodeType::Open || base->type == MathNodeType::Close ||
         base->type == MathNodeType::Punct || base->type == MathNodeType::Inner;
}

inline bool isCharacterBoxRenderNode(const MathRenderNode& node) {
  if (node.kind == MathRenderKind::Symbol) {
    return true;
  }
  if ((node.kind == MathRenderKind::Span || node.kind == MathRenderKind::Accent) && node.children.size() == 1) {
    return isCharacterBoxRenderNode(*node.children.front());
  }
  return false;
}

inline bool isSingleSymbolRenderNode(const MathRenderNode& node) {
  if (node.kind == MathRenderKind::Symbol && node.text.size() == 1) {
    return true;
  }
  if (node.kind == MathRenderKind::Span && node.children.size() == 1) {
    return isSingleSymbolRenderNode(*node.children.front());
  }
  return false;
}

inline int accentBaseCharacterCount(const QVector<MathParseNode>& base) {
  if (base.isEmpty()) {
    return 1;
  }
  int count = 0;
  for (const MathParseNode& node : base) {
    if (!node.text.isEmpty()) {
      count += qMax(1, node.text.size());
    } else if (!node.body.isEmpty()) {
      count += accentBaseCharacterCount(node.body);
    } else {
      ++count;
    }
  }
  return qMax(1, count);
}

inline std::unique_ptr<MathRenderNode> makeArrayCellWrapper(std::unique_ptr<MathRenderNode> content,
                                                            qreal rowHeight,
                                                            qreal rowDepth,
                                                            qreal alignedX) {
  auto wrapper = std::make_unique<MathRenderNode>();
  wrapper->kind = MathRenderKind::Span;
  wrapper->width = content ? content->width : 0.0;
  wrapper->height = rowHeight;
  wrapper->depth = rowDepth;
  wrapper->xOffset = alignedX;
  if (content) {
    content->xOffset = 0.0;
    content->yOffset = 0.0;
    wrapper->children.push_back(std::move(content));
  }
  return wrapper;
}

}  // namespace muffin::math

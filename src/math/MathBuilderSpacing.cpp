#include "math/MathBuilder.h"

#include "math/MathFontMetrics.h"
#include "math/MathLayoutTree.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include <vector>

namespace muffin::math {
namespace {

enum class MathAtomClass {
  Unknown = -3,
  Leftmost = -2,
  Rightmost = -1,
  Ord = 0,
  Op = 1,
  Bin = 2,
  Rel = 3,
  Open = 4,
  Close = 5,
  Punct = 6,
  Inner = 7
};

MathAtomClass atomClassForNode(const MathParseNode& node) {
  if (node.type == MathNodeType::SupSub && !node.base.isEmpty()) {
    return atomClassForNode(node.base.first());
  }
  if (node.type == MathNodeType::Group) {
    return MathAtomClass::Ord;
  }
  if (node.type == MathNodeType::Class) {
    if (node.mathClass == QStringLiteral("\\mathbin")) return MathAtomClass::Bin;
    if (node.mathClass == QStringLiteral("\\mathrel")) return MathAtomClass::Rel;
    if (node.mathClass == QStringLiteral("\\mathopen")) return MathAtomClass::Open;
    if (node.mathClass == QStringLiteral("\\mathclose")) return MathAtomClass::Close;
    if (node.mathClass == QStringLiteral("\\mathpunct")) return MathAtomClass::Punct;
    if (node.mathClass == QStringLiteral("\\mathinner")) return MathAtomClass::Inner;
    return MathAtomClass::Ord;
  }
  if (node.type == MathNodeType::LeftRight || node.type == MathNodeType::Array || node.type == MathNodeType::HorizBrace ||
      node.type == MathNodeType::XArrow) {
    return MathAtomClass::Inner;
  }
  if (node.type == MathNodeType::RaiseBox || node.type == MathNodeType::VCenter) {
    return MathAtomClass::Unknown;
  }
  switch (node.type) {
    case MathNodeType::Operator:
      return MathAtomClass::Op;
    case MathNodeType::Binary:
      return MathAtomClass::Bin;
    case MathNodeType::Relation:
      return MathAtomClass::Rel;
    case MathNodeType::Open:
      return MathAtomClass::Open;
    case MathNodeType::Close:
      return MathAtomClass::Close;
    case MathNodeType::Punct:
      return MathAtomClass::Punct;
    case MathNodeType::Inner:
      return MathAtomClass::Inner;
    default:
      return MathAtomClass::Ord;
  }
}

MathAtomClass atomClassForNodeType(MathNodeType type) {
  switch (type) {
    case MathNodeType::Operator: return MathAtomClass::Op;
    case MathNodeType::Binary: return MathAtomClass::Bin;
    case MathNodeType::Relation: return MathAtomClass::Rel;
    case MathNodeType::Open: return MathAtomClass::Open;
    case MathNodeType::Close: return MathAtomClass::Close;
    case MathNodeType::Punct: return MathAtomClass::Punct;
    case MathNodeType::Inner: return MathAtomClass::Inner;
    default: return MathAtomClass::Ord;
  }
}

QString atomClassName(MathAtomClass atomClass) {
  switch (atomClass) {
    case MathAtomClass::Op: return QStringLiteral("mop");
    case MathAtomClass::Bin: return QStringLiteral("mbin");
    case MathAtomClass::Rel: return QStringLiteral("mrel");
    case MathAtomClass::Open: return QStringLiteral("mopen");
    case MathAtomClass::Close: return QStringLiteral("mclose");
    case MathAtomClass::Punct: return QStringLiteral("mpunct");
    case MathAtomClass::Inner: return QStringLiteral("minner");
    case MathAtomClass::Unknown:
      return QString();
    case MathAtomClass::Ord:
    case MathAtomClass::Leftmost:
    case MathAtomClass::Rightmost:
      return QStringLiteral("mord");
  }
  return QStringLiteral("mord");
}

bool binLeftCancels(MathAtomClass previous) {
  if (previous == MathAtomClass::Unknown) {
    return false;
  }
  return previous == MathAtomClass::Leftmost || previous == MathAtomClass::Bin || previous == MathAtomClass::Open ||
         previous == MathAtomClass::Rel || previous == MathAtomClass::Op || previous == MathAtomClass::Punct;
}

bool binRightCancels(MathAtomClass next) {
  if (next == MathAtomClass::Unknown) {
    return false;
  }
  return next == MathAtomClass::Rightmost || next == MathAtomClass::Rel || next == MathAtomClass::Close || next == MathAtomClass::Punct;
}

bool isPartialGroupNode(const MathParseNode& node) {
  return node.type == MathNodeType::Color || node.type == MathNodeType::Html || node.type == MathNodeType::Href ||
         node.type == MathNodeType::Enclose || node.type == MathNodeType::Styling || node.type == MathNodeType::Sizing ||
         (node.type == MathNodeType::Class && node.mathClass.isEmpty() && !node.fontClass.isEmpty());
}

bool nodeUsesTightSpacing(const MathParseNode& node, const MathOptions& options) {
  if (node.type == MathNodeType::Styling) {
    if (node.style == QStringLiteral("\\displaystyle") || node.style == QStringLiteral("\\textstyle")) {
      return false;
    }
    if (node.style == QStringLiteral("\\scriptstyle") || node.style == QStringLiteral("\\scriptscriptstyle")) {
      return true;
    }
  }
  if (isPartialGroupNode(node) && !node.body.isEmpty()) {
    return nodeUsesTightSpacing(node.body.first(), options);
  }
  return options.style().isTight();
}

bool isMathFontCommand(const QString& label) {
  static const QSet<QString> commands{
      QStringLiteral("\\mathrm"),     QStringLiteral("\\mathbf"), QStringLiteral("\\mathit"), QStringLiteral("\\mathnormal"),
      QStringLiteral("\\mathsf"),     QStringLiteral("\\mathtt"), QStringLiteral("\\mathbb"), QStringLiteral("\\mathcal"),
      QStringLiteral("\\mathfrak"),   QStringLiteral("\\mathscr"), QStringLiteral("\\mathsfit"), QStringLiteral("\\Bbb"),
      QStringLiteral("\\bold"),       QStringLiteral("\\frak"),   QStringLiteral("\\rm"),     QStringLiteral("\\sf"),
      QStringLiteral("\\tt"),         QStringLiteral("\\bf"),     QStringLiteral("\\it"),     QStringLiteral("\\cal")};
  return commands.contains(label);
}

qreal axisHeight(const MathOptions& options) {
  return options.fontPointSize() * MathFontMetrics::globalMetrics(options.style().size()).axisHeight;
}

qreal ruleThickness(const MathOptions& options) {
  const qreal ruleEm = qMax(MathFontMetrics::globalMetrics(options.style().size()).defaultRuleThickness, options.settings().minRuleThickness);
  return options.fontPointSize() * ruleEm;
}

QString fontMetricsNameForClass(const QString& fontClass) {
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

qreal italicCorrection(const MathRenderNode& node, const MathOptions& options) {
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

qreal skewCorrection(const MathRenderNode& node, const MathOptions& options) {
  if (node.kind == MathRenderKind::Span && node.children.size() == 1) {
    return skewCorrection(*node.children.front(), options);
  }
  if (node.text.size() != 1) {
    return 0.0;
  }
  const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(node.fontClass), node.text);
  return metrics ? metrics->skew * options.fontPointSize() : 0.0;
}

bool operatorCanGrow(const MathParseNode& node) {
  return node.opSymbol && node.label != QStringLiteral("\\smallint");
}

bool operatorUsesLimits(const MathParseNode& node, const MathOptions& options) {
  if (node.explicitLimits) {
    return node.limits;
  }
  return node.limits && (options.style().id() == MathStyle::Display || node.alwaysHandleSupSub);
}

const MathParseNode* baseElement(const MathParseNode& node) {
  if ((node.type == MathNodeType::Group || node.type == MathNodeType::Color || node.type == MathNodeType::Class) && node.body.size() == 1) {
    return baseElement(node.body.first());
  }
  return &node;
}

bool isCharacterBoxNode(const MathParseNode& node) {
  const MathParseNode* base = baseElement(node);
  if (base == nullptr) {
    return false;
  }
  return base->type == MathNodeType::Ord || base->type == MathNodeType::Text || base->type == MathNodeType::Binary ||
         base->type == MathNodeType::Relation || base->type == MathNodeType::Open || base->type == MathNodeType::Close ||
         base->type == MathNodeType::Punct || base->type == MathNodeType::Inner;
}

int accentBaseCharacterCount(const QVector<MathParseNode>& base) {
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

bool isSingleSymbolRenderNode(const MathRenderNode& node) {
  if (node.kind == MathRenderKind::Symbol && node.text.size() == 1) {
    return true;
  }
  if (node.kind == MathRenderKind::Span && node.children.size() == 1) {
    return isSingleSymbolRenderNode(*node.children.front());
  }
  return false;
}

bool isExplicitSpacingExpression(const QVector<MathParseNode>& expression);

bool isExplicitSpacingNode(const MathParseNode& node) {
  if (node.type == MathNodeType::Spacing || node.type == MathNodeType::Kern) {
    return true;
  }
  if (node.type == MathNodeType::MathChoice) {
    return isExplicitSpacingExpression(node.display) && isExplicitSpacingExpression(node.body) &&
           isExplicitSpacingExpression(node.script) && isExplicitSpacingExpression(node.scriptScript);
  }
  return false;
}

bool isExplicitSpacingExpression(const QVector<MathParseNode>& expression) {
  if (expression.isEmpty()) {
    return true;
  }
  for (const MathParseNode& node : expression) {
    if (!isExplicitSpacingNode(node)) {
      return false;
    }
  }
  return true;
}

bool isCharacterBoxRenderNode(const MathRenderNode& node) {
  if (node.kind == MathRenderKind::Symbol) {
    return true;
  }
  if ((node.kind == MathRenderKind::Span || node.kind == MathRenderKind::Accent) && node.children.size() == 1) {
    return isCharacterBoxRenderNode(*node.children.front());
  }
  return false;
}

const MathParseNode* outermostNode(const MathParseNode& node, bool rightSide) {
  if (node.type == MathNodeType::SupSub && !node.base.isEmpty()) {
    return outermostNode(node.base.first(), rightSide);
  }
  if (isPartialGroupNode(node) && !node.body.isEmpty()) {
    return outermostNode(rightSide ? node.body.last() : node.body.first(), rightSide);
  }
  return &node;
}

MathAtomClass atomClassForOuterNode(const MathParseNode& node, bool rightSide) {
  if (const MathParseNode* outer = outermostNode(node, rightSide)) {
    return atomClassForNode(*outer);
  }
  return MathAtomClass::Ord;
}

std::unique_ptr<MathRenderNode> makeArrayCellWrapper(std::unique_ptr<MathRenderNode> content,
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

}  // namespace

MathBuilder::MathBuilder(MathOptions options) : options_(std::move(options)) {}

struct MathBuilder::BuildItem {
  std::unique_ptr<MathRenderNode> node;
  MathAtomClass atomClass = MathAtomClass::Unknown;
  bool explicitSpacing = false;
  bool tightSpacing = false;
};

std::unique_ptr<MathRenderNode> MathBuilder::buildExpression(const QVector<MathParseNode>& expression) {
  int crIndex = -1;
  for (int i = 0; i < expression.size(); ++i) {
    if (expression.at(i).type == MathNodeType::Cr) {
      crIndex = i;
      break;
    }
  }
  if (crIndex >= 0) {
    QVector<QVector<MathParseNode>> lines;
    QVector<qreal> gaps;
    QVector<MathParseNode> currentLine;
    for (const MathParseNode& node : expression) {
      if (node.type == MathNodeType::Cr) {
        lines.push_back(std::move(currentLine));
        currentLine = {};
        gaps.push_back(node.height.isEmpty() ? 0.0 : dimensionToPoints(node.height));
      } else {
        currentLine.push_back(node);
      }
    }
    lines.push_back(std::move(currentLine));

    std::vector<std::unique_ptr<MathRenderNode>> renderedLines;
    renderedLines.reserve(lines.size());
    qreal width = 0.0;
    QVector<qreal> rowHeights;
    QVector<qreal> rowDepths;
    rowHeights.reserve(lines.size());
    rowDepths.reserve(lines.size());
    const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
    const qreal pt = metrics.ptPerEm > 0.0 ? 1.0 / metrics.ptPerEm : 0.1;
    const qreal arstrutHeight = options_.fontPointSize() * 0.7 * 12.0 * pt;
    const qreal arstrutDepth = options_.fontPointSize() * 0.3 * 12.0 * pt;
    for (const QVector<MathParseNode>& line : lines) {
      auto rendered = MathBuilder(options_).buildExpression(line);
      width = qMax(width, rendered->width);
      rowHeights.push_back(qMax(rendered->height, arstrutHeight));
      rowDepths.push_back(qMax(rendered->depth, arstrutDepth));
      renderedLines.push_back(std::move(rendered));
    }
    for (int i = 0; i + 1 < rowDepths.size(); ++i) {
      if (i < gaps.size() && gaps.at(i) > 0.0) {
        rowDepths[i] += gaps.at(i);
      }
    }

    qreal totalHeight = 0.0;
    QVector<qreal> rowPositions(rowHeights.size(), 0.0);
    for (int i = 0; i < rowHeights.size(); ++i) {
      totalHeight += rowHeights.at(i);
      rowPositions[i] = totalHeight;
      totalHeight += rowDepths.at(i);
    }
    const qreal baseline = totalHeight / 2.0 + axisHeight(options_);
    std::vector<MathVListChild> rowChildren;
    for (int i = 0; i < renderedLines.size(); ++i) {
      const qreal alignedX = (width - renderedLines.at(i)->width) / 2.0;
      auto line = makeArrayCellWrapper(std::move(renderedLines.at(i)), rowHeights.at(i), rowDepths.at(i), alignedX);
      rowChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(line)), rowPositions.at(i) - baseline});
    }
    auto vlist = makeLayoutVListIndividualShift(std::move(rowChildren));
    vlist->width = width;
    return renderNodeFromLayout(*vlist);
  }

  qreal glueCssEmPerMu = MathFontMetrics::globalMetrics(options_.style().size()).cssEmPerMu;
  if (expression.size() == 1) {
    const MathParseNode& node = expression.first();
    if (node.type == MathNodeType::Styling) {
      MathStyle style = options_.style();
      if (node.style == QStringLiteral("\\displaystyle")) style = MathStyle::display();
      else if (node.style == QStringLiteral("\\textstyle")) style = MathStyle::textStyle();
      else if (node.style == QStringLiteral("\\scriptstyle")) style = MathStyle::script();
      else if (node.style == QStringLiteral("\\scriptscriptstyle")) style = MathStyle::scriptScript();
      glueCssEmPerMu = MathFontMetrics::globalMetrics(style.size()).cssEmPerMu;
    }
  }
  return makeSpanFromItems(buildSpacedItems(buildExpressionItems(expression), glueCssEmPerMu));
}

std::vector<MathBuilder::BuildItem> MathBuilder::buildExpressionItems(const QVector<MathParseNode>& expression) {
  std::vector<BuildItem> items;
  for (const MathParseNode& node : expression) {
    std::vector<BuildItem> nodeItems = buildNodeItems(node);
    for (auto& item : nodeItems) {
      items.push_back(std::move(item));
    }
  }
  return items;
}

std::vector<MathBuilder::BuildItem> MathBuilder::buildSpacedItems(std::vector<BuildItem> items, qreal glueCssEmPerMu) {
  std::vector<std::unique_ptr<MathRenderNode>> children;
  QVector<MathAtomClass> classes;
  classes.resize(static_cast<int>(items.size()));
  QVector<int> nonspaceIndexes;
  nonspaceIndexes.reserve(static_cast<int>(items.size()));
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    if (items.at(i).explicitSpacing) {
      continue;
    }
    nonspaceIndexes.push_back(i);
    classes[i] = items.at(i).atomClass;
  }
  for (int pos = 0; pos < nonspaceIndexes.size(); ++pos) {
    const int i = nonspaceIndexes.at(pos);
    const int previousIndex = pos == 0 ? -1 : nonspaceIndexes.at(pos - 1);
    const MathAtomClass previous = previousIndex < 0 ? MathAtomClass::Leftmost : classes.at(previousIndex);
    const MathAtomClass current = classes.at(i);
    if (previousIndex >= 0 && previous == MathAtomClass::Bin && binRightCancels(current)) {
      classes[previousIndex] = MathAtomClass::Ord;
    } else if (current == MathAtomClass::Bin && binLeftCancels(previous)) {
      classes[i] = MathAtomClass::Ord;
    }
  }
  if (!nonspaceIndexes.isEmpty()) {
    const int lastIndex = nonspaceIndexes.last();
    if (classes.at(lastIndex) == MathAtomClass::Bin && binRightCancels(MathAtomClass::Rightmost)) {
      classes[lastIndex] = MathAtomClass::Ord;
    }
  }

  int previousNonspace = -1;
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    BuildItem& item = items.at(i);
    if (!item.explicitSpacing) {
      const MathAtomClass current = classes.at(i);
      if (previousNonspace >= 0) {
        const MathAtomClass previousClass = classes.at(previousNonspace);
        if (previousClass != MathAtomClass::Unknown && current != MathAtomClass::Unknown) {
          const qreal spacing = spacingAfter(static_cast<int>(previousClass), static_cast<int>(current), item.tightSpacing, glueCssEmPerMu);
          if (spacing > 0.0) {
            auto glue = std::make_unique<MathRenderNode>();
            glue->kind = MathRenderKind::Span;
            glue->text = QStringLiteral("glue");
            glue->atomClass = QStringLiteral("mspace");
            glue->width = spacing * options_.fontPointSize();
            glue->tightSpacing = item.tightSpacing;
            children.push_back(std::move(glue));
          }
        }
      }
      previousNonspace = i;
    }
    if (!item.node) {
      continue;
    }
    if (item.explicitSpacing) {
      if (item.node->atomClass.isEmpty()) {
        item.node->atomClass = QStringLiteral("mspace");
      }
    } else if (classes.at(i) != MathAtomClass::Unknown) {
      item.node->atomClass = atomClassName(classes.at(i));
    } else if (item.node->atomClass.isEmpty()) {
      item.node->atomClass.clear();
    }
    item.node->tightSpacing = item.tightSpacing;
    children.push_back(std::move(item.node));
  }
  std::vector<BuildItem> result;
  result.reserve(children.size());
  for (auto& child : children) {
    BuildItem item;
    item.tightSpacing = child->tightSpacing;
    item.explicitSpacing = child->atomClass == QStringLiteral("mspace");
    item.atomClass = item.explicitSpacing ? MathAtomClass::Unknown : atomClassForNodeType(MathNodeType::Ord);
    item.node = std::move(child);
    result.push_back(std::move(item));
  }
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSpanFromItems(std::vector<BuildItem> items) {
  std::vector<std::unique_ptr<MathRenderNode>> children;
  children.reserve(items.size());
  for (auto& item : items) {
    if (item.node) {
      children.push_back(std::move(item.node));
    }
  }
  return makeSpan(std::move(children));
}

std::vector<MathBuilder::BuildItem> MathBuilder::buildNodeItems(const MathParseNode& node) {
  auto makeSingle = [&](std::unique_ptr<MathRenderNode> render, MathAtomClass atomClass, bool explicitSpacing = false) {
    BuildItem item;
    item.node = std::move(render);
    item.atomClass = atomClass;
    item.explicitSpacing = explicitSpacing;
    item.tightSpacing = nodeUsesTightSpacing(node, options_);
    std::vector<BuildItem> result;
    result.push_back(std::move(item));
    return result;
  };

  if (node.type == MathNodeType::Color) {
    QColor color(node.color);
    if (!color.isValid()) {
      static const QHash<QString, QColor> namedColors{
          {QStringLiteral("red"), QColor(QStringLiteral("#df0030"))},
          {QStringLiteral("blue"), QColor(QStringLiteral("#0057d8"))},
          {QStringLiteral("green"), QColor(QStringLiteral("#008000"))},
          {QStringLiteral("black"), QColor(QStringLiteral("#000000"))},
          {QStringLiteral("white"), QColor(QStringLiteral("#ffffff"))},
          {QStringLiteral("gray"), QColor(QStringLiteral("#808080"))},
          {QStringLiteral("grey"), QColor(QStringLiteral("#808080"))},
          {QStringLiteral("purple"), QColor(QStringLiteral("#800080"))},
          {QStringLiteral("orange"), QColor(QStringLiteral("#ff8000"))}};
      color = namedColors.value(node.color, options_.color());
    }
    return MathBuilder(options_.withColor(color)).buildExpressionItems(node.body);
  }
  if (node.type == MathNodeType::Styling) {
    MathStyle style = options_.style();
    if (node.style == QStringLiteral("\\displaystyle")) style = MathStyle::display();
    else if (node.style == QStringLiteral("\\textstyle")) style = MathStyle::textStyle();
    else if (node.style == QStringLiteral("\\scriptstyle")) style = MathStyle::script();
    else if (node.style == QStringLiteral("\\scriptscriptstyle")) style = MathStyle::scriptScript();
    return MathBuilder(options_.havingStyle(style)).buildExpressionItems(node.body);
  }
  if (node.type == MathNodeType::Sizing) {
    static const QHash<QString, qreal> scales{
        {QStringLiteral("\\tiny"), 0.5},       {QStringLiteral("\\sixptsize"), 0.55}, {QStringLiteral("\\scriptsize"), 0.7},
        {QStringLiteral("\\footnotesize"), 0.8}, {QStringLiteral("\\small"), 0.9},      {QStringLiteral("\\normalsize"), 1.0},
        {QStringLiteral("\\large"), 1.2},      {QStringLiteral("\\Large"), 1.44},      {QStringLiteral("\\LARGE"), 1.73},
        {QStringLiteral("\\huge"), 2.07},      {QStringLiteral("\\Huge"), 2.49}};
    return MathBuilder(options_.havingSizeScale(scales.value(node.size, 1.0))).buildExpressionItems(node.body);
  }
  if (node.type == MathNodeType::Href || node.type == MathNodeType::Html) {
    if (!node.body.isEmpty()) {
      return MathBuilder(options_).buildExpressionItems(node.body);
    }
  }
  if (node.type == MathNodeType::Class && node.mathClass.isEmpty() && node.fontClass.isEmpty()) {
    return MathBuilder(options_).buildExpressionItems(node.body);
  }

  return makeSingle(buildNode(node),
                    isExplicitSpacingNode(node) ? MathAtomClass::Unknown : atomClassForOuterNode(node, false),
                    isExplicitSpacingNode(node));
}

qreal MathBuilder::spacingAfter(int previous, int current, bool tight, qreal glueCssEmPerMu) const {
  const auto prev = static_cast<MathAtomClass>(previous);
  const auto next = static_cast<MathAtomClass>(current);
  const qreal thin = 3.0 * glueCssEmPerMu;
  const qreal medium = 4.0 * glueCssEmPerMu;
  const qreal thick = 5.0 * glueCssEmPerMu;

  if (tight) {
    if (prev == MathAtomClass::Ord && next == MathAtomClass::Op) return thin;
    if (prev == MathAtomClass::Op && (next == MathAtomClass::Ord || next == MathAtomClass::Op)) return thin;
    if (prev == MathAtomClass::Close && next == MathAtomClass::Op) return thin;
    if (prev == MathAtomClass::Inner && next == MathAtomClass::Op) return thin;
    return 0.0;
  }

  switch (prev) {
    case MathAtomClass::Ord:
      if (next == MathAtomClass::Op) return thin;
      if (next == MathAtomClass::Bin) return medium;
      if (next == MathAtomClass::Rel) return thick;
      if (next == MathAtomClass::Inner) return thin;
      break;
    case MathAtomClass::Op:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op) return thin;
      if (next == MathAtomClass::Rel) return thick;
      if (next == MathAtomClass::Inner) return thin;
      break;
    case MathAtomClass::Bin:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op || next == MathAtomClass::Open || next == MathAtomClass::Inner) return medium;
      break;
    case MathAtomClass::Rel:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op || next == MathAtomClass::Open || next == MathAtomClass::Inner) return thick;
      break;
    case MathAtomClass::Close:
      if (next == MathAtomClass::Op) return thin;
      if (next == MathAtomClass::Bin) return medium;
      if (next == MathAtomClass::Rel) return thick;
      if (next == MathAtomClass::Inner) return thin;
      break;
    case MathAtomClass::Punct:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op || next == MathAtomClass::Open || next == MathAtomClass::Close ||
          next == MathAtomClass::Punct || next == MathAtomClass::Inner) {
        return thin;
      }
      if (next == MathAtomClass::Rel) return thick;
      break;
    case MathAtomClass::Inner:
      if (next == MathAtomClass::Ord || next == MathAtomClass::Op || next == MathAtomClass::Open || next == MathAtomClass::Punct ||
          next == MathAtomClass::Inner) {
        return thin;
      }
      if (next == MathAtomClass::Bin) return medium;
      if (next == MathAtomClass::Rel) return thick;
      break;
    default:
      break;
  }
  return 0.0;
}

}  // namespace muffin::math

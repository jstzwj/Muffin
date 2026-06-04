#include "math/MathBuilder.h"

#include "math/MathDelimiter.h"
#include "math/MathFontMetrics.h"
#include "math/MathLayoutTree.h"
#include "math/MathSvgGeometry.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>

#include <memory>
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
  if (node.type == MathNodeType::LeftRight || node.type == MathNodeType::Array || node.type == MathNodeType::HorizBrace) {
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

std::unique_ptr<MathRenderNode> MathBuilder::buildNode(const MathParseNode& node) {
  switch (node.type) {
    case MathNodeType::Group:
      return buildExpression(node.body);
    case MathNodeType::SupSub:
      return makeSupSub(node);
    case MathNodeType::Fraction:
      return makeFraction(node);
    case MathNodeType::Sqrt:
      return makeSqrt(node);
    case MathNodeType::Accent:
      return makeAccent(node);
    case MathNodeType::AccentUnder:
      return makeAccentUnder(node);
    case MathNodeType::HorizBrace:
      return makeHorizBrace(node);
    case MathNodeType::Underline:
      return makeUnderline(node);
    case MathNodeType::Overline:
      return makeOverline(node);
    case MathNodeType::Phantom:
      return makePhantom(node);
    case MathNodeType::Smash:
      return makeSmash(node);
    case MathNodeType::Rule:
      return makeRuleNode(node);
    case MathNodeType::Kern:
      return makeKern(node);
    case MathNodeType::Lap:
      return makeLap(node);
    case MathNodeType::RaiseBox:
      return makeRaiseBox(node);
    case MathNodeType::VCenter:
      return makeVCenter(node);
    case MathNodeType::Enclose:
      return makeEnclose(node);
    case MathNodeType::IncludeGraphics:
      return makeIncludeGraphics(node);
    case MathNodeType::MathChoice:
      return makeMathChoice(node);
    case MathNodeType::Href:
    case MathNodeType::Html:
      return node.body.isEmpty() ? makeSymbol(node.text, MathNodeType::Text, options_) : buildExpression(node.body);
    case MathNodeType::Tag:
      return makeTag(node);
    case MathNodeType::Verb:
      return makeVerb(node);
    case MathNodeType::DelimSizing:
      return makeDelimSizing(node);
    case MathNodeType::LeftRight:
      return makeLeftRight(node);
    case MathNodeType::Array:
      return makeArray(node);
    case MathNodeType::Color: {
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
      return MathBuilder(options_.withColor(color)).buildExpression(node.body);
    }
    case MathNodeType::Styling: {
      MathStyle style = options_.style();
      if (node.style == QStringLiteral("\\displaystyle")) style = MathStyle::display();
      else if (node.style == QStringLiteral("\\textstyle")) style = MathStyle::textStyle();
      else if (node.style == QStringLiteral("\\scriptstyle")) style = MathStyle::script();
      else if (node.style == QStringLiteral("\\scriptscriptstyle")) style = MathStyle::scriptScript();
      return MathBuilder(options_.havingStyle(style)).buildExpression(node.body);
    }
    case MathNodeType::Sizing: {
      static const QHash<QString, qreal> scales{
          {QStringLiteral("\\tiny"), 0.5},       {QStringLiteral("\\sixptsize"), 0.55}, {QStringLiteral("\\scriptsize"), 0.7},
          {QStringLiteral("\\footnotesize"), 0.8}, {QStringLiteral("\\small"), 0.9},      {QStringLiteral("\\normalsize"), 1.0},
          {QStringLiteral("\\large"), 1.2},      {QStringLiteral("\\Large"), 1.44},      {QStringLiteral("\\LARGE"), 1.73},
          {QStringLiteral("\\huge"), 2.07},      {QStringLiteral("\\Huge"), 2.49}};
      return MathBuilder(options_.havingSizeScale(scales.value(node.size, 1.0))).buildExpression(node.body);
    }
    case MathNodeType::Class:
      if (!node.fontClass.isEmpty()) {
        std::vector<std::unique_ptr<MathRenderNode>> children;
        for (const MathParseNode& childNode : node.body) {
          if (!childNode.text.isEmpty()) {
            children.push_back(makeSymbol(childNode.text, childNode.type, options_, node.fontClass));
          } else {
            children.push_back(buildNode(childNode));
          }
        }
        return makeSpan(std::move(children));
      }
      return buildExpression(node.body);
    case MathNodeType::Error:
      return makeError(node.text, options_);
    case MathNodeType::Text: {
      const MathNodeType fontNodeType = isMathFontCommand(node.label) ? MathNodeType::Ord : MathNodeType::Text;
      if (!node.body.isEmpty()) {
        std::vector<std::unique_ptr<MathRenderNode>> children;
        for (const MathParseNode& childNode : node.body) {
          if (childNode.text.isEmpty() && childNode.body.isEmpty()) {
            children.push_back(buildNode(childNode));
          } else if (!childNode.text.isEmpty()) {
            children.push_back(makeSymbol(childNode.text, fontNodeType, options_, node.fontClass));
          } else {
            children.push_back(MathBuilder(options_).buildExpression(childNode.body));
          }
        }
        return makeSpan(std::move(children));
      }
      if (node.text.size() > 1) {
        return makeTextSpan(node.text, fontNodeType, options_, node.fontClass);
      }
      return makeSymbol(node.text, fontNodeType, options_, node.fontClass);
    }
    case MathNodeType::Operator:
      if (!node.body.isEmpty()) {
        auto opBody = buildExpression(node.body);
        opBody->fontClass = QStringLiteral("main");
        return opBody;
      }
      if (node.opSymbol) {
        const bool large = operatorCanGrow(node) && options_.style().id() == MathStyle::Display;
        auto op = makeSymbol(node.text, node.type, options_, large ? QStringLiteral("size2") : QStringLiteral("size1"));
        const qreal shift = (op->height - op->depth) / 2.0 - axisHeight(options_);
        if (!qFuzzyIsNull(shift)) {
          op->yOffset = -shift;
        }
        return op;
      }
      if (node.text.size() > 1) {
        return makeTextSpan(node.text, node.type, options_, QStringLiteral("main"));
      }
      return makeSymbol(node.text, node.type, options_, node.fontClass.isEmpty() ? QStringLiteral("main") : node.fontClass);
    case MathNodeType::Spacing:
      return makeSpacing(node);
    default:
      return makeSymbol(node.text, node.type, options_, node.fontClass);
  }
}

std::unique_ptr<MathRenderNode> MathBuilder::makeUnderline(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  auto line = std::make_unique<MathRenderNode>();
  line->kind = MathRenderKind::Rule;
  line->width = body->width;
  line->ruleThickness = ruleThickness(options_);
  line->color = options_.color();
  line->xOffset = 0.0;
  line->yOffset = body->depth + options_.fontPointSize() * 0.16;

  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  result->height = body->height;
  result->depth = body->depth + options_.fontPointSize() * 0.22;
  result->children.push_back(std::move(body));
  result->children.push_back(std::move(line));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeOverline(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  const qreal rule = ruleThickness(options_);
  const qreal gap = 3.0 * rule;
  auto line = std::make_unique<MathRenderNode>();
  line->kind = MathRenderKind::Rule;
  line->width = body->width;
  line->ruleThickness = rule;
  line->color = options_.color();
  line->xOffset = 0.0;
  line->yOffset = -(body->height + gap);

  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  result->height = body->height + gap + rule;
  result->depth = body->depth;
  result->children.push_back(std::move(body));
  result->children.push_back(std::move(line));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makePhantom(const MathParseNode& node) {
  auto body = MathBuilder(options_.withPhantom()).buildExpression(node.base);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Phantom;
  result->width = node.label == QStringLiteral("\\vphantom") ? 0.0 : body->width;
  result->height = node.label == QStringLiteral("\\hphantom") ? 0.0 : body->height;
  result->depth = node.label == QStringLiteral("\\hphantom") ? 0.0 : body->depth;
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSmash(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  result->height = node.smashHeight ? 0.0 : body->height;
  result->depth = node.smashDepth ? 0.0 : body->depth;
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeRuleNode(const MathParseNode& node) {
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Rect;
  result->width = dimensionToPoints(node.width);
  result->ruleThickness = dimensionToPoints(node.height);
  result->shift = node.shift.isEmpty() ? 0.0 : dimensionToPoints(node.shift);
  result->height = qMax<qreal>(0.0, result->ruleThickness + result->shift);
  result->depth = qMax<qreal>(0.0, -result->shift);
  result->color = options_.color();
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeKern(const MathParseNode& node) {
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Span;
  result->text = QStringLiteral("mspace");
  result->atomClass = QStringLiteral("mspace");
  result->width = dimensionToPoints(node.width);
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeLap(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = 0.0;
  result->height = body->height;
  result->depth = body->depth;
  if (node.label == QStringLiteral("\\mathllap")) {
    body->xOffset = -body->width;
  } else if (node.label == QStringLiteral("\\mathclap")) {
    body->xOffset = -body->width / 2.0;
  }
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeRaiseBox(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  const qreal shift = dimensionToPoints(node.shift);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  body->yOffset = -shift;
  result->height = qMax<qreal>(0.0, body->height + shift);
  result->depth = qMax<qreal>(0.0, body->depth - shift);
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeVCenter(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  const qreal axis = axisHeight(options_);
  const qreal dy = 0.5 * ((body->height - axis) - (body->depth + axis));
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width;
  body->yOffset = dy;
  result->height = qMax<qreal>(0.0, body->height - dy);
  result->depth = qMax<qreal>(0.0, body->depth + dy);
  result->children.push_back(std::move(body));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeEnclose(const MathParseNode& node) {
  auto body = MathBuilder(options_).buildExpression(node.base);
  const QString label = node.label;
  const bool isCancel = label == QStringLiteral("\\cancel") || label == QStringLiteral("\\bcancel") || label == QStringLiteral("\\xcancel");
  const bool isBox = label == QStringLiteral("\\fbox") || label == QStringLiteral("\\boxed") || label == QStringLiteral("\\colorbox") ||
                     label == QStringLiteral("\\fcolorbox");
  const bool isSingleChar = isCharacterBoxRenderNode(*body);
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  const qreal em = options_.fontPointSize();
  const qreal boxRule = qMax(metrics.fboxrule, options_.settings().minRuleThickness) * em;
  const qreal bodyHeight = body->height;
  const qreal bodyDepth = body->depth;
  qreal hPad = 0.0;
  qreal topPad = 0.0;
  qreal bottomPad = 0.0;

  if (isCancel) {
    hPad = isSingleChar ? 0.0 : 0.2 * em;
    topPad = isSingleChar ? 0.2 * em : 0.0;
    bottomPad = topPad;
  } else if (isBox) {
    hPad = metrics.fboxsep * em;
    topPad = (metrics.fboxsep * em) + (label == QStringLiteral("\\colorbox") ? 0.0 : boxRule);
    bottomPad = topPad;
  } else {
    hPad = 0.3 * em;
    topPad = 0.3 * em;
    bottomPad = 0.3 * em;
  }

  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = body->width + hPad * 2.0;
  result->height = isCancel ? body->height : body->height + topPad;
  result->depth = isCancel ? body->depth : body->depth + bottomPad;
  body->xOffset = hPad;
  result->children.push_back(std::move(body));

  if (label == QStringLiteral("\\fbox") || label == QStringLiteral("\\colorbox") || label == QStringLiteral("\\fcolorbox")) {
    auto bg = std::make_unique<MathRenderNode>();
    bg->kind = MathRenderKind::Rect;
    bg->width = result->width;
    bg->height = result->height;
    bg->depth = result->depth;
    QColor background(node.backgroundColor);
    bg->color = background.isValid() ? background : QColor(255, 255, 255, 0);
    bg->xOffset = 0.0;
    bg->yOffset = 0.0;
    result->children.insert(result->children.begin(), std::move(bg));
  }
  auto overlay = std::make_unique<MathRenderNode>();
  overlay->kind = MathRenderKind::Stretchy;
  overlay->text = label;
  overlay->width = result->width;
  overlay->height = bodyHeight + topPad;
  overlay->depth = bodyDepth + bottomPad;
  overlay->ruleThickness = isBox ? boxRule : ruleThickness(options_);
  QColor border(node.borderColor);
  overlay->color = border.isValid() ? border : options_.color();
  overlay->xOffset = 0.0;
  overlay->yOffset = 0.0;
  result->children.push_back(std::move(overlay));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeIncludeGraphics(const MathParseNode& node) {
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Rect;
  result->imageSource = node.href;
  result->text = node.alt.isEmpty() ? QFileInfo(node.href).baseName() : node.alt;
  result->font = options_.fontForClass(QStringLiteral("main"));
  qreal height = node.height.isEmpty() ? options_.fontPointSize() * 0.9 : dimensionToPoints(node.height);
  qreal total = node.totalHeight.isEmpty() ? height : dimensionToPoints(node.totalHeight);
  result->width = node.width.isEmpty() ? qMax<qreal>(height, options_.fontPointSize()) : dimensionToPoints(node.width);
  result->ruleThickness = height;
  result->height = height;
  result->depth = qMax<qreal>(0.0, total - height);
  result->color = options_.color();

  QImageReader reader(node.href);
  const QSize imageSize = reader.size();
  if (node.width.isEmpty() && imageSize.isValid() && imageSize.height() > 0) {
    result->width = height * qreal(imageSize.width()) / qreal(imageSize.height());
  }
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeMathChoice(const MathParseNode& node) {
  switch (options_.style().size()) {
    case 0:
      return buildExpression(node.display);
    case 2:
      return buildExpression(node.script);
    case 3:
      return buildExpression(node.scriptScript);
    case 1:
    default:
      return buildExpression(node.body);
  }
}

std::unique_ptr<MathRenderNode> MathBuilder::makeTag(const MathParseNode& node) {
  if (node.body.isEmpty()) {
    auto open = makeSymbol(QStringLiteral("("), MathNodeType::Open, options_);
    auto tag = MathBuilder(options_.havingStyle(options_.style().text())).buildExpression(node.tag);
    auto close = makeSymbol(QStringLiteral(")"), MathNodeType::Close, options_);
    std::vector<std::unique_ptr<MathRenderNode>> tagChildren;
    tagChildren.push_back(std::move(open));
    tagChildren.push_back(std::move(tag));
    tagChildren.push_back(std::move(close));
    return makeSpan(std::move(tagChildren));
  }
  auto body = MathBuilder(options_).buildExpression(node.body);
  auto tag = MathBuilder(options_.havingStyle(options_.style().text())).buildExpression(node.tag);
  auto gap = std::make_unique<MathRenderNode>();
  gap->kind = MathRenderKind::Span;
  gap->width = options_.fontPointSize() * 2.0;
  std::vector<std::unique_ptr<MathRenderNode>> children;
  children.push_back(std::move(body));
  children.push_back(std::move(gap));
  children.push_back(std::move(tag));
  return makeSpan(std::move(children));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeVerb(const MathParseNode& node) {
  QString text = node.text;
  text.replace(QLatin1Char(' '), node.label == QStringLiteral("\\verb*") ? QChar(0x2423) : QChar(0x00a0));
  auto result = makeSymbol(text, MathNodeType::Text, options_.havingStyle(options_.style().text()));
  result->font.setStyleHint(QFont::Monospace);
  result->font.setFamily(QStringLiteral("Consolas"));
  const QFontMetricsF metrics(result->font);
  result->width = metrics.horizontalAdvance(text);
  result->height = metrics.ascent();
  result->depth = metrics.descent();
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSymbol(const QString& text, MathNodeType type, const MathOptions& options, const QString& forcedFontClass) {
  auto node = std::make_unique<MathRenderNode>();
  node->kind = MathRenderKind::Symbol;
  node->text = text;
  node->atomClass = atomClassName(atomClassForNodeType(type));
  QString fontClass = forcedFontClass.isEmpty() ? QStringLiteral("mathnormal") : forcedFontClass;
  if (forcedFontClass.isEmpty() && type == MathNodeType::Ord && text.size() == 1 && text.at(0).isDigit()) {
    fontClass = QStringLiteral("main");
  }
  if (type == MathNodeType::Operator || type == MathNodeType::Binary || type == MathNodeType::Relation || type == MathNodeType::Open ||
      type == MathNodeType::Close || type == MathNodeType::Punct || type == MathNodeType::Inner || type == MathNodeType::Spacing) {
    if (forcedFontClass.isEmpty()) {
      fontClass = QStringLiteral("main");
    }
  }
  if (text.size() > 1 && type == MathNodeType::Operator) {
    if (forcedFontClass.isEmpty()) {
      fontClass = QStringLiteral("main");
    }
  }
  if (type == MathNodeType::Text && forcedFontClass.isEmpty()) {
    fontClass = QStringLiteral("main");
  }
  node->fontClass = fontClass;
  node->font = options.fontForClass(fontClass);
  node->color = options.color();
  node->phantom = options.phantom();
  const qreal em = options.fontPointSize();
  if (text.size() == 1) {
    const std::optional<CharacterMetrics> metrics = MathFontMetrics::characterMetrics(fontMetricsNameForClass(fontClass), text);
    if (metrics) {
      node->width = metrics->width * em;
      node->height = metrics->height * em;
      node->depth = metrics->depth * em;
      node->italic = metrics->italic * em;
      if (node->italic > 0.0 && type != MathNodeType::Text && fontClass != QStringLiteral("mathit")) {
        node->italicMarginRight = node->italic;
        node->width += node->italicMarginRight;
      }
      return node;
    }
  }
  const QFontMetricsF metrics(node->font);
  node->width = text.trimmed().isEmpty() ? em * 0.18 * text.size() : metrics.horizontalAdvance(text);
  node->height = metrics.ascent();
  node->depth = metrics.descent();
  return node;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeTextSpan(const QString& text,
                                                          MathNodeType type,
                                                          const MathOptions& options,
                                                          const QString& forcedFontClass) {
  std::vector<std::unique_ptr<MathRenderNode>> children;
  for (const QChar& ch : text) {
    children.push_back(makeSymbol(QString(ch), type, options, forcedFontClass));
  }
  return makeSpan(std::move(children));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSpacing(const MathParseNode& node) {
  const QString key = node.label.isEmpty() ? node.text : node.label;
  static const QHash<QString, qreal> widths{
      {QStringLiteral("\\,"), 3.0 / 18.0},
      {QStringLiteral("\\thinspace"), 3.0 / 18.0},
      {QStringLiteral("\\:"), 4.0 / 18.0},
      {QStringLiteral("\\medspace"), 4.0 / 18.0},
      {QStringLiteral("\\;"), 5.0 / 18.0},
      {QStringLiteral("\\thickspace"), 5.0 / 18.0},
      {QStringLiteral("\\!"), -3.0 / 18.0},
      {QStringLiteral("\\negthinspace"), -3.0 / 18.0},
      {QStringLiteral("\\negmedspace"), -4.0 / 18.0},
      {QStringLiteral("\\negthickspace"), -5.0 / 18.0},
      {QStringLiteral("\\quad"), 1.0},
      {QStringLiteral("\\qquad"), 2.0},
      {QStringLiteral("\\ "), 0.25},
      {QStringLiteral("\\space"), 0.25},
      {QStringLiteral("\\nobreakspace"), 0.25},
      {QStringLiteral("\\allowbreak"), 0.0},
      {QStringLiteral(" "), 0.25},
      {QStringLiteral("~"), 0.25}};
  auto space = std::make_unique<MathRenderNode>();
  space->kind = MathRenderKind::Span;
  space->text = key == QStringLiteral("\\allowbreak") ? QStringLiteral("allowbreak") : QStringLiteral("mspace");
  space->atomClass = QStringLiteral("mspace");
  space->allowBreak = key == QStringLiteral("\\allowbreak");
  space->width = widths.value(key, node.text.isEmpty() || node.text.size() > 1 ? 0.0 : 0.25) * options_.fontPointSize();
  space->phantom = options_.phantom();
  return space;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeDelimiter(const QString& delimiter, qreal targetHeight, MathNodeType type) {
  return MathDelimiter::makeCustom(delimiter, targetHeight, true, type, options_);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSpan(std::vector<std::unique_ptr<MathRenderNode>> children) {
  auto span = std::make_unique<MathRenderNode>();
  span->kind = MathRenderKind::Span;
  span->phantom = options_.phantom();
  span->children = std::move(children);
  for (const auto& child : span->children) {
    span->width += child->width;
    span->height = qMax(span->height, child->height - child->shift);
    span->depth = qMax(span->depth, child->depth + child->shift);
  }
  return span;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSupSub(const MathParseNode& node) {
  if (node.base.size() == 1 && node.base.first().type == MathNodeType::HorizBrace) {
    return makeHorizBraceSupSub(node);
  }
  auto base = node.base.isEmpty() ? makeError(QStringLiteral("?"), options_) : MathBuilder(options_).buildExpression(node.base);
  const qreal em = options_.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  const bool limitsOperator = !node.base.isEmpty() && node.base.first().type == MathNodeType::Operator &&
                              operatorUsesLimits(node.base.first(), options_);
  if (limitsOperator) {
    const qreal baseShift = !node.base.isEmpty() && node.base.first().opSymbol
                                ? ((base->height - base->depth) / 2.0 - axisHeight(options_))
                                : 0.0;
    base->yOffset = 0.0;
    qreal slant = italicCorrection(*base, options_);
    if (qFuzzyIsNull(slant) && !node.base.isEmpty() && node.base.first().opSymbol && !node.base.first().text.isEmpty()) {
      const bool large = operatorCanGrow(node.base.first()) && options_.style().id() == MathStyle::Display;
      const std::optional<CharacterMetrics> opMetrics =
          MathFontMetrics::characterMetrics(large ? QStringLiteral("Size2-Regular") : QStringLiteral("Size1-Regular"), node.base.first().text);
      if (opMetrics) {
        slant = opMetrics->italic * em;
      }
    }
    std::unique_ptr<MathRenderNode> sup;
    std::unique_ptr<MathRenderNode> sub;
    qreal supKern = 0.0;
    qreal subKern = 0.0;
    qreal width = base->width;
    if (!node.sup.isEmpty()) {
      sup = MathBuilder(options_.sup()).buildExpression(node.sup);
      supKern = qMax(metrics.bigOpSpacing1 * em, metrics.bigOpSpacing3 * em - sup->depth);
      width = qMax(width, sup->width);
    }
    if (!node.sub.isEmpty()) {
      sub = MathBuilder(options_.sub()).buildExpression(node.sub);
      subKern = qMax(metrics.bigOpSpacing2 * em, metrics.bigOpSpacing4 * em - sub->height);
      width = qMax(width, sub->width);
    }

    base->xOffset = (width - base->width) / 2.0;
    const qreal baseHeight = base->height;
    const qreal baseDepth = base->depth;
    auto baseLayout = layoutFromRenderNode(std::move(base));
    std::unique_ptr<MathLayoutNode> limitsLayout;
    if (sup && sub) {
      sup->xOffset = (width - sup->width) / 2.0 + slant / 2.0;
      sub->xOffset = (width - sub->width) / 2.0 - slant / 2.0;
      const qreal bottom = metrics.bigOpSpacing5 * em + sub->height + sub->depth + subKern + baseDepth + baseShift;
      std::vector<MathVListEntry> entries;
      entries.push_back(makeLayoutVListKern(metrics.bigOpSpacing5 * em));
      entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(sub)), 0.0, -slant, 0.0}));
      entries.push_back(makeLayoutVListKern(subKern));
      entries.push_back(makeLayoutVListElem(MathVListChild{std::move(baseLayout)}));
      entries.push_back(makeLayoutVListKern(supKern));
      entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(sup)), 0.0, slant, 0.0}));
      entries.push_back(makeLayoutVListKern(metrics.bigOpSpacing5 * em));
      limitsLayout = makeLayoutVListBottom(bottom, std::move(entries));
    } else if (sub) {
      sub->xOffset = (width - sub->width) / 2.0 - slant / 2.0;
      const qreal top = baseHeight - baseShift;
      std::vector<MathVListEntry> entries;
      entries.push_back(makeLayoutVListKern(metrics.bigOpSpacing5 * em));
      entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(sub)), 0.0, -slant, 0.0}));
      entries.push_back(makeLayoutVListKern(subKern));
      entries.push_back(makeLayoutVListElem(MathVListChild{std::move(baseLayout)}));
      limitsLayout = makeLayoutVListTop(top, std::move(entries));
    } else if (sup) {
      sup->xOffset = (width - sup->width) / 2.0 + slant / 2.0;
      const qreal bottom = baseDepth + baseShift;
      std::vector<MathVListEntry> entries;
      entries.push_back(makeLayoutVListElem(MathVListChild{std::move(baseLayout)}));
      entries.push_back(makeLayoutVListKern(supKern));
      entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(sup)), 0.0, slant, 0.0}));
      entries.push_back(makeLayoutVListKern(metrics.bigOpSpacing5 * em));
      limitsLayout = makeLayoutVListBottom(bottom, std::move(entries));
    } else {
      return renderNodeFromLayout(*baseLayout);
    }
    limitsLayout->width = width;
    return renderNodeFromLayout(*limitsLayout);
  }

  const qreal baseWidth = base->width;
  const qreal baseHeight = base->height;
  const qreal baseDepth = base->depth;
  const qreal baseItalic = italicCorrection(*base, options_);
  const bool isCharBox = !node.base.isEmpty() && isCharacterBoxNode(node.base.first());
  const bool shiftSubByItalic = isSingleSymbolRenderNode(*base);

  qreal scriptWidth = 0.0;
  qreal supShift = 0.0;
  qreal subShift = 0.0;
  std::unique_ptr<MathRenderNode> supNode;
  std::unique_ptr<MathRenderNode> subNode;
  if (!node.sup.isEmpty()) {
    const MathOptions scriptOptions = options_.sup();
    supNode = MathBuilder(scriptOptions).buildExpression(node.sup);
    const qreal metricShift = (options_.style().id() == MathStyle::Display) ? metrics.sup1 : (options_.style().cramped() ? metrics.sup3 : metrics.sup2);
    if (!isCharBox) {
      supShift = baseHeight - MathFontMetrics::globalMetrics(scriptOptions.style().size()).supDrop * scriptOptions.fontPointSize();
    }
    supShift = qMax(supShift, metricShift * em);
    const qreal minSupBottom = 0.25 * metrics.xHeight * em;
    supShift = qMax(supShift, supNode->depth + minSupBottom);
    scriptWidth = qMax(scriptWidth, supNode->width);
  }
  if (!node.sub.isEmpty()) {
    const MathOptions scriptOptions = options_.sub();
    subNode = MathBuilder(scriptOptions).buildExpression(node.sub);
    if (!isCharBox) {
      subShift = baseDepth + MathFontMetrics::globalMetrics(scriptOptions.style().size()).subDrop * scriptOptions.fontPointSize();
    }
    if (!supNode) {
      subShift = qMax(subShift, metrics.sub1 * em);
      subShift = qMax(subShift, subNode->height - 0.8 * metrics.xHeight * em);
    } else {
      subShift = qMax(subShift, metrics.sub2 * em);
    }
    scriptWidth = qMax(scriptWidth, subNode->width);
  }
  if (supNode && subNode) {
    const qreal rule = ruleThickness(options_);
    const qreal minGap = 4.0 * rule;
    const qreal gap = (supShift - supNode->depth) - (subNode->height - subShift);
    if (gap < minGap) {
      subShift += minGap - gap;
    }
    const qreal psi = 0.8 * metrics.xHeight * em - (supShift - supNode->depth);
    if (psi > 0.0) {
      supShift += psi;
      subShift -= psi;
    }
  }

  std::vector<MathVListChild> scriptChildren;
  const qreal scriptspace = (0.5 / metrics.ptPerEm) * options_.basePointSize();
  if (supNode) {
    scriptChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(supNode)), -supShift, 0.0, scriptspace});
  }
  if (subNode) {
    const qreal marginLeft = shiftSubByItalic ? -baseItalic : 0.0;
    scriptChildren.insert(scriptChildren.begin(), MathVListChild{layoutFromRenderNode(std::move(subNode)), subShift, marginLeft, scriptspace});
  }

  if (scriptChildren.empty()) {
    return base;
  }

  std::unique_ptr<MathLayoutNode> scriptLayout;
  const bool hasSupScript = !node.sup.isEmpty();
  const bool hasSubScript = !node.sub.isEmpty();
  if (hasSupScript && hasSubScript) {
    scriptLayout = makeLayoutVListIndividualShift(std::move(scriptChildren));
  } else if (hasSubScript) {
    std::vector<MathVListEntry> entries;
    entries.push_back(makeLayoutVListElem(std::move(scriptChildren.front())));
    scriptLayout = makeLayoutVListShift(subShift, std::move(entries));
  } else {
    std::vector<MathVListEntry> entries;
    entries.push_back(makeLayoutVListElem(std::move(scriptChildren.front())));
    scriptLayout = makeLayoutVListShift(-supShift, std::move(entries));
  }
  std::vector<std::unique_ptr<MathLayoutNode>> supsubChildren;
  supsubChildren.push_back(layoutFromRenderNode(std::move(base)));
  supsubChildren.push_back(std::move(scriptLayout));
  return renderNodeFromLayout(*makeLayoutSpan(std::move(supsubChildren)));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeFraction(const MathParseNode& node) {
  MathOptions localOptions = options_;
  if (node.style == QStringLiteral("\\displaystyle")) {
    localOptions = options_.havingStyle(MathStyle::display());
  } else if (node.style == QStringLiteral("\\textstyle")) {
    localOptions = options_.havingStyle(MathStyle::textStyle());
  } else if (node.style == QStringLiteral("\\scriptstyle")) {
    localOptions = options_.havingStyle(MathStyle::script());
  } else if (node.style == QStringLiteral("\\scriptscriptstyle")) {
    localOptions = options_.havingStyle(MathStyle::scriptScript());
  }
  auto numerator = MathBuilder(localOptions.fracNum()).buildExpression(node.numerator);
  auto denominator = MathBuilder(localOptions.fracDen()).buildExpression(node.denominator);
  const qreal em = localOptions.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(localOptions.style().size());
  const qreal width = qMax(numerator->width, denominator->width);
  const qreal rule = node.lineThickness >= 0.0 ? node.lineThickness * em : ruleThickness(localOptions);
  const bool hasBarLine = rule > 0.0;
  const qreal axis = axisHeight(localOptions);
  const bool display = localOptions.style().size() == MathStyle::display().size();
  qreal numShift = (display ? metrics.num1 : (hasBarLine ? metrics.num2 : metrics.num3)) * em;
  qreal denomShift = (display ? metrics.denom1 : metrics.denom2) * em;
  const qreal ruleSpacing = hasBarLine ? rule : ruleThickness(localOptions);
  const qreal clearance = hasBarLine ? (display ? 3.0 : 1.0) * ruleSpacing : (display ? 7.0 : 3.0) * ruleSpacing;

  if (hasBarLine) {
    const qreal numeratorClearance = (numShift - numerator->depth) - (axis + 0.5 * rule);
    if (numeratorClearance < clearance) {
      numShift += clearance - numeratorClearance;
    }
    const qreal denominatorClearance = (axis - 0.5 * rule) - (denominator->height - denomShift);
    if (denominatorClearance < clearance) {
      denomShift += clearance - denominatorClearance;
    }
  } else {
    const qreal candidateClearance = (numShift - numerator->depth) - (denominator->height - denomShift);
    if (candidateClearance < clearance) {
      const qreal delta = 0.5 * (clearance - candidateClearance);
      numShift += delta;
      denomShift += delta;
    }
  }

  numerator->xOffset = (width - numerator->width) / 2.0;
  denominator->xOffset = (width - denominator->width) / 2.0;

  std::vector<MathVListChild> vlistChildren;
  vlistChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(denominator)), denomShift});
  if (hasBarLine) {
    auto line = std::make_unique<MathRenderNode>();
    line->kind = MathRenderKind::Rule;
    line->width = width;
    line->height = rule;
    line->depth = 0;
    line->ruleThickness = rule;
    line->color = localOptions.color();
    vlistChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(line)), -(axis - 0.5 * rule)});
  }
  vlistChildren.push_back(MathVListChild{layoutFromRenderNode(std::move(numerator)), -numShift});
  auto fracLayout = makeLayoutVListIndividualShift(std::move(vlistChildren));
  fracLayout->width = width;
  auto frac = renderNodeFromLayout(*fracLayout);
  return makeGenfrac(node, std::move(frac));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeGenfrac(const MathParseNode& node, std::unique_ptr<MathRenderNode> frac) {
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  qreal delimiterSize = 0.0;
  if (options_.style().size() == MathStyle::display().size()) {
    delimiterSize = metrics.delim1 * options_.fontPointSize();
  } else if (options_.style().size() == MathStyle::scriptScript().size()) {
    delimiterSize = MathFontMetrics::globalMetrics(MathStyle::script().size()).delim2 * options_.fontPointSize();
  } else {
    delimiterSize = metrics.delim2 * options_.fontPointSize();
  }

  const qreal nullDelimiterWidth = (1.2 / metrics.ptPerEm) * options_.basePointSize();
  std::vector<std::unique_ptr<MathRenderNode>> children;

  if (node.leftDelim.isEmpty() || node.leftDelim == QStringLiteral(".")) {
    auto nullDelimiter = std::make_unique<MathRenderNode>();
    nullDelimiter->kind = MathRenderKind::Span;
    nullDelimiter->text = QStringLiteral("nulldelimiter");
    nullDelimiter->width = nullDelimiterWidth;
    children.push_back(std::move(nullDelimiter));
  } else {
    children.push_back(MathDelimiter::makeCustom(node.leftDelim, delimiterSize, true, MathNodeType::Open, options_));
  }

  children.push_back(std::move(frac));

  if (node.rightDelim.isEmpty() || node.rightDelim == QStringLiteral(".")) {
    auto nullDelimiter = std::make_unique<MathRenderNode>();
    nullDelimiter->kind = MathRenderKind::Span;
    nullDelimiter->text = QStringLiteral("nulldelimiter");
    nullDelimiter->width = nullDelimiterWidth;
    children.push_back(std::move(nullDelimiter));
  } else {
    children.push_back(MathDelimiter::makeCustom(node.rightDelim, delimiterSize, true, MathNodeType::Close, options_));
  }

  return makeSpan(std::move(children));
}

std::unique_ptr<MathRenderNode> MathBuilder::makeSqrt(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.body);
  const qreal em = options_.fontPointSize();
  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  if (qFuzzyIsNull(body->height)) {
    body->height = metrics.xHeight * em;
  }
  const qreal theta = metrics.defaultRuleThickness * em;
  const qreal phi = options_.style().id() < MathStyle::Text ? metrics.xHeight * em : theta;
  qreal lineClearance = theta + 0.25 * phi;
  const qreal minDelimiterHeightEm = qMax<qreal>((body->height + body->depth + lineClearance + theta) / em, metrics.xHeight);
  const qreal extraVinculum = qMax<qreal>(0.0, options_.settings().minRuleThickness - metrics.sqrtRuleThickness);
  QString sqrtName = QStringLiteral("sqrtTall");
  qreal texHeightEm = minDelimiterHeightEm + extraVinculum;
  qreal spanHeightEm = minDelimiterHeightEm + extraVinculum + 0.08;
  qreal advanceWidthEm = 1.056;
  int viewBoxHeight = qFloor(1000.0 * minDelimiterHeightEm + extraVinculum) + 80;
  if (minDelimiterHeightEm < 1.0) {
    sqrtName = QStringLiteral("sqrtMain");
    texHeightEm = 1.0 + extraVinculum;
    spanHeightEm = 1.0 + extraVinculum + 0.08;
    advanceWidthEm = 0.833;
    viewBoxHeight = 1000 + qRound(1000.0 * extraVinculum) + 80;
  } else if (minDelimiterHeightEm < 1.2) {
    sqrtName = QStringLiteral("sqrtSize1");
    texHeightEm = 1.2 + extraVinculum;
    spanHeightEm = 1.2 + extraVinculum + 0.08;
    advanceWidthEm = 1.0;
    viewBoxHeight = qRound((1000 + 80) * 1.2);
  } else if (minDelimiterHeightEm < 1.8) {
    sqrtName = QStringLiteral("sqrtSize2");
    texHeightEm = 1.8 + extraVinculum;
    spanHeightEm = 1.8 + extraVinculum + 0.08;
    advanceWidthEm = 1.0;
    viewBoxHeight = qRound((1000 + 80) * 1.8);
  } else if (minDelimiterHeightEm < 2.4) {
    sqrtName = QStringLiteral("sqrtSize3");
    texHeightEm = 2.4 + extraVinculum;
    spanHeightEm = 2.4 + extraVinculum + 0.08;
    advanceWidthEm = 1.0;
    viewBoxHeight = qRound((1000 + 80) * 2.4);
  } else if (minDelimiterHeightEm < 3.0) {
    sqrtName = QStringLiteral("sqrtSize4");
    texHeightEm = 3.0 + extraVinculum;
    spanHeightEm = 3.0 + extraVinculum + 0.08;
    advanceWidthEm = 1.0;
    viewBoxHeight = qRound((1000 + 80) * 3.0);
  }
  const qreal rule = (metrics.sqrtRuleThickness + extraVinculum) * em;
  const qreal delimDepth = texHeightEm * em - rule;
  if (delimDepth > body->height + body->depth + lineClearance) {
    lineClearance = (lineClearance + delimDepth - body->height - body->depth) / 2.0;
  }
  const qreal imgShift = texHeightEm * em - body->height - lineClearance - rule;
  const qreal pad = qMax<qreal>(0.0, spanHeightEm - texHeightEm) * em;
  auto radical = std::make_unique<MathRenderNode>();
  radical->kind = MathRenderKind::Stretchy;
  radical->text = QStringLiteral("\\sqrt");
  radical->pathName = sqrtName;
  radical->svgPath = MathSvgGeometry::sqrtPath(sqrtName, extraVinculum, viewBoxHeight);
  radical->viewBox = QRectF(0.0, 0.0, 400000.0, viewBoxHeight);
  radical->width = advanceWidthEm * em;
  radical->height = texHeightEm * em;
  radical->depth = 0.0;
  radical->ruleThickness = rule;
  radical->color = options_.color();

  body->xOffset = advanceWidthEm * em;
  const qreal innerHeight = body->height;
  Q_UNUSED(pad);
  std::vector<MathVListEntry> sqrtBodyChildren;
  sqrtBodyChildren.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(body))}));
  sqrtBodyChildren.push_back(makeLayoutVListKern(-(innerHeight + imgShift)));
  sqrtBodyChildren.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(radical))}));
  sqrtBodyChildren.push_back(makeLayoutVListKern(rule));
  auto sqrtBodyLayout = makeLayoutVListFirstBaseline(std::move(sqrtBodyChildren));
  sqrtBodyLayout->width = qMax(sqrtBodyLayout->width, advanceWidthEm * em);

  auto sqrtBody = renderNodeFromLayout(*sqrtBodyLayout);
  if (!node.rootIndex.isEmpty()) {
    auto index = MathBuilder(options_.havingStyle(MathStyle::scriptScript())).buildExpression(node.rootIndex);
    const qreal toShift = 0.6 * (sqrtBody->height - sqrtBody->depth);
    std::vector<MathVListEntry> rootChildren;
    rootChildren.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(index))}));
    auto rootLayout = makeLayoutVListShift(-toShift, std::move(rootChildren));
    auto root = renderNodeFromLayout(*rootLayout);
    const qreal rootMarginLeft = em * (5.0 / 18.0);
    const qreal rootMarginRight = -em * (10.0 / 18.0);
    const qreal rootAdvance = qMax<qreal>(0.0, rootMarginLeft + root->width + rootMarginRight);
    root->xOffset = rootMarginLeft;
    root->width = rootAdvance;
    sqrtBody->xOffset = 0.0;
    std::vector<std::unique_ptr<MathRenderNode>> indexedChildren;
    indexedChildren.push_back(std::move(root));
    indexedChildren.push_back(std::move(sqrtBody));
    return makeSpan(std::move(indexedChildren));
  }
  return sqrtBody;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeAccent(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  std::unique_ptr<MathRenderNode> accent;

  if (node.label == QStringLiteral("\\widehat") || node.label == QStringLiteral("\\widetilde") ||
      node.label == QStringLiteral("\\widecheck") || node.label == QStringLiteral("\\overrightarrow") ||
      node.label == QStringLiteral("\\overleftarrow") || node.label == QStringLiteral("\\overleftrightarrow")) {
    accent = makeStretchyAccent(node, *body);
    std::vector<MathVListEntry> entries;
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(body))}));
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(accent))}));
    auto layout = makeLayoutVListFirstBaseline(std::move(entries));
    layout->renderKind = MathRenderKind::Accent;
    return renderNodeFromLayout(*layout);
  } else {
    QString accentText = QStringLiteral("^");
    if (node.label == QStringLiteral("\\tilde") || node.label == QStringLiteral("\\widetilde")) accentText = QStringLiteral("~");
    else if (node.label == QStringLiteral("\\bar")) accentText = QStringLiteral("\u02c9");
    else if (node.label == QStringLiteral("\\dot")) accentText = QStringLiteral(".");
    else if (node.label == QStringLiteral("\\ddot")) accentText = QStringLiteral("..");
    else if (node.label == QStringLiteral("\\acute")) accentText = QStringLiteral("\u00b4");
    else if (node.label == QStringLiteral("\\grave")) accentText = QStringLiteral("`");
    else if (node.label == QStringLiteral("\\check") || node.label == QStringLiteral("\\widecheck")) accentText = QStringLiteral("\u02c7");
    else if (node.label == QStringLiteral("\\breve")) accentText = QStringLiteral("\u02d8");
    else if (node.label == QStringLiteral("\\mathring")) accentText = QStringLiteral("\u02da");
    if (node.label == QStringLiteral("\\vec")) {
      accent = std::make_unique<MathRenderNode>();
      accent->kind = MathRenderKind::Stretchy;
      accent->pathName = QStringLiteral("vec");
      accent->svgPath = MathSvgGeometry::path(QStringLiteral("vec"));
      accent->viewBox = QRectF(0.0, 0.0, 471.0, 714.0);
      accent->width = options_.fontPointSize() * 0.471;
      accent->height = options_.fontPointSize() * 0.714;
      accent->depth = 0.0;
      accent->color = options_.color();
    } else {
      accent = makeSymbol(accentText, MathNodeType::Text, options_.havingStyle(options_.style().text()), QStringLiteral("main"));
      accent->italic = 0.0;
      accent->italicMarginRight = 0.0;
    }
  }

  const GlobalFontMetrics metrics = MathFontMetrics::globalMetrics(options_.style().size());
  qreal clearance = qMin(body->height, metrics.xHeight * options_.fontPointSize());
  const qreal accentWidth = accent->width;
  const qreal skew = isCharacterBoxRenderNode(*body) ? skewCorrection(*body, options_) : 0.0;
  accent->xOffset = (body->width - accentWidth) / 2.0 + skew;
  accent->height += accent->depth;
  accent->depth = 0.0;

  std::vector<std::unique_ptr<MathRenderNode>> accentChildren;
  accentChildren.push_back(std::move(accent));
  auto accentWrapper = makeSpan(std::move(accentChildren));
  accentWrapper->atomClass = QStringLiteral("accent-body");
  accentWrapper->width = 0.0;

  std::vector<MathVListEntry> entries;
  entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(body))}));
  entries.push_back(makeLayoutVListKern(-clearance));
  entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(accentWrapper))}));
  auto layout = makeLayoutVListFirstBaseline(std::move(entries));
  layout->renderKind = MathRenderKind::Accent;
  return renderNodeFromLayout(*layout);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeAccentUnder(const MathParseNode& node) {
  auto body = MathBuilder(options_.cramped()).buildExpression(node.base);
  auto accent = makeStretchyAccent(node, *body);
  const qreal gap = node.label == QStringLiteral("\\utilde") ? options_.fontPointSize() * 0.12 : ruleThickness(options_);
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = qMax(body->width, accent->width);
  body->xOffset = (result->width - body->width) / 2.0;
  accent->xOffset = (result->width - accent->width) / 2.0;
  accent->yOffset = body->depth + gap + accent->height;
  result->height = body->height;
  result->depth = body->depth + gap + accent->height + accent->depth;
  result->children.push_back(std::move(body));
  result->children.push_back(std::move(accent));
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeHorizBrace(const MathParseNode& node) {
  auto body = MathBuilder(options_.havingStyle(MathStyle::display())).buildExpression(node.base);
  auto brace = makeBraceGlyph(node, *body);
  const qreal gap = options_.fontPointSize() * 0.1;
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::Accent;
  result->width = qMax(body->width, brace->width);
  body->xOffset = (result->width - body->width) / 2.0;
  brace->xOffset = (result->width - brace->width) / 2.0;
  if (node.isOver) {
    brace->yOffset = -(body->height + gap);
    result->height = body->height + gap + brace->height;
    result->depth = body->depth;
    result->children.push_back(std::move(brace));
    result->children.push_back(std::move(body));
  } else {
    brace->yOffset = body->depth + gap + brace->height;
    result->height = body->height;
    result->depth = body->depth + gap + brace->height;
    result->children.push_back(std::move(body));
    result->children.push_back(std::move(brace));
  }
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeHorizBraceSupSub(const MathParseNode& node) {
  const MathParseNode& braceNode = node.base.first();
  auto braceBase = makeHorizBrace(braceNode);
  const bool useSup = braceNode.isOver && !node.sup.isEmpty();
  const bool useSub = !braceNode.isOver && !node.sub.isEmpty();
  if (!useSup && !useSub) {
    return braceBase;
  }

  auto annotation = MathBuilder(braceNode.isOver ? options_.sup() : options_.sub()).buildExpression(useSup ? node.sup : node.sub);
  const qreal gap = options_.fontPointSize() * 0.2;
  const qreal width = qMax(braceBase->width, annotation->width);
  braceBase->xOffset = (width - braceBase->width) / 2.0;
  annotation->xOffset = (width - annotation->width) / 2.0;

  std::vector<MathVListEntry> entries;
  std::unique_ptr<MathLayoutNode> layout;
  if (braceNode.isOver) {
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(braceBase))}));
    entries.push_back(makeLayoutVListKern(gap));
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(annotation))}));
    layout = makeLayoutVListFirstBaseline(std::move(entries));
  } else {
    const qreal bottom = braceBase->depth + gap + annotation->height + annotation->depth;
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(annotation))}));
    entries.push_back(makeLayoutVListKern(gap));
    entries.push_back(makeLayoutVListElem(MathVListChild{layoutFromRenderNode(std::move(braceBase))}));
    layout = makeLayoutVListBottom(bottom, std::move(entries));
  }
  layout->width = width;
  return renderNodeFromLayout(*layout);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeStretchyAccent(const MathParseNode& node, const MathRenderNode& body) {
  auto accent = std::make_unique<MathRenderNode>();
  accent->kind = MathRenderKind::Stretchy;
  accent->text = node.label;
  const qreal em = options_.fontPointSize();
  if (node.label == QStringLiteral("\\widehat") || node.label == QStringLiteral("\\widecheck") ||
      node.label == QStringLiteral("\\widetilde") || node.label == QStringLiteral("\\utilde")) {
    const int numChars = accentBaseCharacterCount(node.base);
    const bool hatLike = node.label == QStringLiteral("\\widehat") || node.label == QStringLiteral("\\widecheck");
    const QString prefix = node.label == QStringLiteral("\\widecheck") ? QStringLiteral("widecheck") : QStringLiteral("widehat");
    if (numChars > 5) {
      accent->pathName = hatLike ? prefix + QStringLiteral("4") : QStringLiteral("tilde4");
      accent->viewBox = hatLike ? QRectF(0.0, 0.0, 2364.0, 420.0) : QRectF(0.0, 0.0, 2340.0, 312.0);
      accent->height = em * (hatLike ? 0.42 : 0.34);
    } else {
      static const int imgIndexByChars[6] = {1, 1, 2, 2, 3, 3};
      const int imgIndex = imgIndexByChars[qBound(0, numChars, 5)];
      if (hatLike) {
        static const qreal viewWidths[5] = {0.0, 1062.0, 2364.0, 2364.0, 2364.0};
        static const qreal viewHeights[5] = {0.0, 239.0, 300.0, 360.0, 420.0};
        static const qreal heights[5] = {0.0, 0.24, 0.30, 0.36, 0.42};
        accent->pathName = prefix + QString::number(imgIndex);
        accent->viewBox = QRectF(0.0, 0.0, viewWidths[imgIndex], viewHeights[imgIndex]);
        accent->height = em * heights[imgIndex];
      } else {
        static const qreal viewWidths[5] = {0.0, 600.0, 1033.0, 2339.0, 2340.0};
        static const qreal viewHeights[5] = {0.0, 260.0, 286.0, 306.0, 312.0};
        static const qreal heights[5] = {0.0, 0.26, 0.286, 0.306, 0.34};
        accent->pathName = QStringLiteral("tilde") + QString::number(imgIndex);
        accent->viewBox = QRectF(0.0, 0.0, viewWidths[imgIndex], viewHeights[imgIndex]);
        accent->height = em * heights[imgIndex];
      }
    }
    accent->width = body.width;
  } else {
    static const QHash<QString, QString> pathNames{
        {QStringLiteral("\\overrightarrow"), QStringLiteral("rightarrow")},
        {QStringLiteral("\\overleftarrow"), QStringLiteral("leftarrow")},
        {QStringLiteral("\\overleftrightarrow"), QStringLiteral("leftrightarrow")},
        {QStringLiteral("\\underleftarrow"), QStringLiteral("leftarrow")},
        {QStringLiteral("\\underrightarrow"), QStringLiteral("rightarrow")},
        {QStringLiteral("\\underleftrightarrow"), QStringLiteral("leftrightarrow")}};
    accent->pathName = pathNames.value(node.label);
    accent->width = qMax<qreal>(body.width, em * 0.888);
    accent->height = em * 0.522;
  }
  accent->depth = 0.0;
  accent->ruleThickness = ruleThickness(options_);
  accent->color = options_.color();
  return accent;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeBraceGlyph(const MathParseNode& node, const MathRenderNode& body) {
  auto brace = std::make_unique<MathRenderNode>();
  brace->kind = MathRenderKind::Stretchy;
  brace->text = node.label;
  brace->width = qMax<qreal>(body.width, options_.fontPointSize() * 1.6);
  brace->height = options_.fontPointSize() * 0.548;
  brace->depth = 0.0;
  brace->ruleThickness = ruleThickness(options_);
  brace->color = options_.color();
  return brace;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeDelimSizing(const MathParseNode& node) {
  return MathDelimiter::makeSized(node.text, qMax(1, node.delimiterSize), node.body.isEmpty() ? MathNodeType::Ord : node.body.first().type, options_);
}

std::unique_ptr<MathRenderNode> MathBuilder::makeLeftRight(const MathParseNode& node) {
  auto inner = MathBuilder(options_).buildExpression(node.body);
  const qreal innerHeight = inner->height;
  const qreal innerDepth = inner->depth;
  auto result = std::make_unique<MathRenderNode>();
  result->kind = MathRenderKind::LeftRight;
  const qreal nullDelimiterWidth = options_.fontPointSize() * 0.12;

  qreal x = 0.0;
  if (node.leftDelim == QStringLiteral(".")) {
    x += nullDelimiterWidth;
  } else {
    auto left = MathDelimiter::makeLeftRight(node.leftDelim, innerHeight, innerDepth, MathNodeType::Open, options_);
    left->xOffset = x;
    left->yOffset = 0.0;
    x += left->width;
    result->height = qMax(result->height, left->height);
    result->depth = qMax(result->depth, left->depth);
    result->children.push_back(std::move(left));
  }

  inner->xOffset = x;
  inner->yOffset = 0.0;
  x += inner->width;
  result->height = qMax(result->height, inner->height);
  result->depth = qMax(result->depth, inner->depth);
  result->children.push_back(std::move(inner));

  if (node.rightDelim == QStringLiteral(".")) {
    x += nullDelimiterWidth;
  } else {
    auto right = MathDelimiter::makeLeftRight(node.rightDelim, innerHeight, innerDepth, MathNodeType::Close, options_);
    right->xOffset = x;
    right->yOffset = 0.0;
    x += right->width;
    result->height = qMax(result->height, right->height);
    result->depth = qMax(result->depth, right->depth);
    result->children.push_back(std::move(right));
  }
  result->width = x;
  return result;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeArray(const MathParseNode& node) {
  const int rowCount = node.rows.size();
  int colCount = 0;
  for (const auto& row : node.rows) {
    colCount = qMax(colCount, row.size());
  }
  if (rowCount <= 0 || colCount <= 0) {
    return makeError(QStringLiteral("empty array"), options_);
  }

  std::vector<std::vector<std::unique_ptr<MathRenderNode>>> cells;
  QVector<qreal> colWidths(colCount, 0.0);
  QVector<qreal> rowHeights(rowCount, 0.0);
  QVector<qreal> rowDepths(rowCount, 0.0);
  MathStyle cellStyle = options_.style().text();
  if (node.arrayCellStyle == QStringLiteral("script")) {
    cellStyle = MathStyle::script();
  } else if (node.arrayCellStyle == QStringLiteral("display")) {
    cellStyle = MathStyle::display();
  } else if (node.arrayCellStyle == QStringLiteral("text")) {
    cellStyle = MathStyle::textStyle();
  }

  for (int r = 0; r < rowCount; ++r) {
    std::vector<std::unique_ptr<MathRenderNode>> rowCells;
    for (int c = 0; c < colCount; ++c) {
      std::unique_ptr<MathRenderNode> cell;
      if (c < node.rows.at(r).size()) {
        QVector<MathParseNode> cellBody;
        for (const auto& item : node.rows.at(r).at(c).body) {
          if (item) {
            cellBody.push_back(*item);
          }
        }
        cell = MathBuilder(options_.havingStyle(cellStyle)).buildExpression(cellBody);
      } else {
        cell = std::make_unique<MathRenderNode>();
      }
      colWidths[c] = qMax(colWidths[c], cell->width);
      rowHeights[r] = qMax(rowHeights[r], cell->height);
      rowDepths[r] = qMax(rowDepths[r], cell->depth);
      rowCells.push_back(std::move(cell));
    }
    cells.push_back(std::move(rowCells));
  }

  const GlobalFontMetrics fontMetrics = MathFontMetrics::globalMetrics(options_.style().size());
  const qreal pt = fontMetrics.ptPerEm > 0.0 ? 1.0 / fontMetrics.ptPerEm : 0.1;
  const qreal arrayColSepEm = node.colSeparationType == QStringLiteral("small")
                                  ? 0.2778 * (options_.havingStyle(MathStyle::script()).sizeMultiplier() / options_.sizeMultiplier())
                                  : 5.0 * pt;
  const qreal baselineSkipEm = 12.0 * pt;
  const qreal jotEm = 3.0 * pt;
  const qreal arrayskipEm = qMax<qreal>(0.0, node.arrayStretch) * baselineSkipEm;
  const qreal arstrutHeight = options_.fontPointSize() * 0.7 * arrayskipEm;
  const qreal arstrutDepth = options_.fontPointSize() * 0.3 * arrayskipEm;
  const qreal rule = ruleThickness(options_);
  QVector<qreal> rowExtraGaps(rowCount, 0.0);

  for (int r = 0; r < rowCount; ++r) {
    rowHeights[r] = qMax(rowHeights[r], arstrutHeight);
    rowDepths[r] = qMax(rowDepths[r], arstrutDepth);
    if (r < node.rowGaps.size() && !qFuzzyIsNull(node.rowGaps.at(r))) {
      const qreal rowGap = options_.fontPointSize() * node.rowGaps.at(r);
      if (rowGap > 0.0) {
        rowDepths[r] = qMax(rowDepths[r], arstrutDepth + rowGap);
      } else {
        rowExtraGaps[r] = rowGap;
      }
    }
    if (node.addJot && r + 1 < rowCount) {
      rowDepths[r] += options_.fontPointSize() * jotEm;
    }
  }

  struct PlacedColumn {
    int column = -1;
    qreal x = 0.0;
    qreal width = 0.0;
    QChar align = QLatin1Char('c');
  };
  struct PlacedSeparator {
    qreal x = 0.0;
    bool dashed = false;
  };

  QVector<PlacedColumn> placedColumns;
  QVector<PlacedSeparator> verticalSeparators;
  qreal bodyWidth = 0.0;
  int alignIndex = 0;
  const bool useExplicitSpec = !node.columns.isEmpty();
  const qreal verticalSeparatorWidth = qMax<qreal>(1.0, rule);
  const auto addGap = [&](qreal em) {
    if (em > 0.0) {
      bodyWidth += em * options_.fontPointSize();
    }
  };

  if (useExplicitSpec) {
    bool sawAlign = false;
    bool previousWasSeparator = false;
    for (const MathArrayColumn& spec : node.columns) {
      if (spec.type == MathArrayColumn::Type::Separator) {
        if (previousWasSeparator) {
          addGap(fontMetrics.doubleRuleSep);
        }
        verticalSeparators.push_back({bodyWidth, spec.separator == QLatin1Char(':')});
        bodyWidth += verticalSeparatorWidth;
        previousWasSeparator = true;
        continue;
      }
      if (alignIndex >= colCount) {
        continue;
      }
      previousWasSeparator = false;
      const qreal pregap = spec.pregap >= 0.0 ? spec.pregap : (sawAlign || node.hskipBeforeAndAfter ? arrayColSepEm : 0.0);
      addGap(pregap);
      placedColumns.push_back({alignIndex, bodyWidth, colWidths[alignIndex], spec.align});
      bodyWidth += colWidths[alignIndex];
      const qreal postgap = spec.postgap >= 0.0 ? spec.postgap : ((alignIndex + 1 < colCount || node.hskipBeforeAndAfter) ? arrayColSepEm : 0.0);
      addGap(postgap);
      ++alignIndex;
      sawAlign = true;
    }
  }
  while (alignIndex < colCount) {
    const qreal pregap = alignIndex > 0 ? arrayColSepEm : 0.0;
    addGap(pregap);
    const QChar align = alignIndex < node.columnAlignments.size() ? node.columnAlignments.at(alignIndex) : QLatin1Char('c');
    placedColumns.push_back({alignIndex, bodyWidth, colWidths[alignIndex], align});
    bodyWidth += colWidths[alignIndex];
    if (alignIndex + 1 < colCount) {
      addGap(arrayColSepEm);
    }
    ++alignIndex;
  }

  auto array = std::make_unique<MathRenderNode>();
  array->kind = MathRenderKind::Array;
  array->columns = colCount;
  array->rows = rowCount;
  qreal totalHeight = 0.0;
  QVector<qreal> rowPositions(rowCount, 0.0);
  QVector<qreal> hlinePositions(node.arrayLines.size(), 0.0);
  const auto setHLinePositions = [&](int beforeRow) {
    int hlinesInGap = 0;
    for (int i = 0; i < node.arrayLines.size(); ++i) {
      if (node.arrayLines.at(i).beforeRow != beforeRow) {
        continue;
      }
      if (hlinesInGap > 0) {
        totalHeight += options_.fontPointSize() * 0.25;
      }
      hlinePositions[i] = totalHeight;
      ++hlinesInGap;
    }
  };
  setHLinePositions(0);
  for (int r = 0; r < rowCount; ++r) {
    totalHeight += rowHeights[r];
    rowPositions[r] = totalHeight;
    totalHeight += rowDepths[r] + rowExtraGaps[r];
    setHLinePositions(r + 1);
  }
  qreal xOffset = 0.0;
  qreal leftDelimiterWidth = 0.0;
  qreal rightDelimiterWidth = 0.0;
  qreal delimiterHeight = 0.0;
  qreal delimiterDepth = 0.0;
  const qreal targetDelimHeight = totalHeight;
  const qreal nullDelimiterWidth = options_.fontPointSize() * 0.12;
  const bool hasLeftDelimiter = !node.leftDelim.isEmpty() && node.leftDelim != QStringLiteral(".");
  const bool hasRightDelimiter = !node.rightDelim.isEmpty() && node.rightDelim != QStringLiteral(".");
  if (node.leftDelim == QStringLiteral(".")) {
    xOffset = nullDelimiterWidth;
  } else if (hasLeftDelimiter) {
    auto left = makeDelimiter(node.leftDelim, targetDelimHeight, MathNodeType::Open);
    left->xOffset = 0.0;
    left->yOffset = 0.0;
    leftDelimiterWidth = left->width;
    xOffset = leftDelimiterWidth;
    delimiterHeight = qMax(delimiterHeight, left->height);
    delimiterDepth = qMax(delimiterDepth, left->depth);
    array->children.push_back(std::move(left));
  }

  const qreal axis = axisHeight(options_);
  qreal baseline = totalHeight / 2.0 + axis;
  auto tableBody = std::make_unique<MathRenderNode>();
  tableBody->kind = MathRenderKind::VList;
  tableBody->width = bodyWidth;
  tableBody->height = baseline;
  tableBody->depth = totalHeight - baseline;

  for (const PlacedColumn& placed : placedColumns) {
    std::vector<MathVListChild> columnChildren;
    const int c = placed.column;
    for (int r = 0; r < rowCount; ++r) {
      auto cell = std::move(cells[r][c]);
      qreal alignedX = 0.0;
      if (placed.align == QLatin1Char('r')) {
        alignedX = colWidths[c] - cell->width;
      } else if (placed.align == QLatin1Char('c')) {
        alignedX = (colWidths[c] - cell->width) / 2.0;
      }
      columnChildren.push_back(MathVListChild{layoutFromRenderNode(makeArrayCellWrapper(std::move(cell), rowHeights[r], rowDepths[r], alignedX)),
                                              rowPositions[r] - baseline});
    }
    auto colLayout = makeLayoutVListIndividualShift(std::move(columnChildren));
    colLayout->width = placed.width;
    auto colVList = renderNodeFromLayout(*colLayout);
    auto colSpan = std::make_unique<MathRenderNode>();
    colSpan->kind = MathRenderKind::Span;
    colSpan->width = placed.width;
    colSpan->height = colVList->height;
    colSpan->depth = colVList->depth;
    colSpan->xOffset = placed.x;
    colSpan->children.push_back(std::move(colVList));
    tableBody->children.push_back(std::move(colSpan));
  }

  tableBody->xOffset = xOffset;
  array->children.push_back(std::move(tableBody));

  for (int i = 0; i < node.arrayLines.size(); ++i) {
    const MathArrayLine& arrayLine = node.arrayLines.at(i);
    auto line = std::make_unique<MathRenderNode>();
    line->kind = MathRenderKind::Rule;
    line->width = bodyWidth;
    line->ruleThickness = rule;
    line->color = options_.color();
    line->xOffset = xOffset;
    line->yOffset = hlinePositions.value(i) - baseline;
    line->text = arrayLine.dashed ? QStringLiteral("dashed") : QString();
    array->children.push_back(std::move(line));
  }

  for (const PlacedSeparator& separator : verticalSeparators) {
    auto line = std::make_unique<MathRenderNode>();
    line->kind = MathRenderKind::Rule;
    line->height = baseline;
    line->depth = totalHeight - baseline;
    line->width = verticalSeparatorWidth;
    line->ruleThickness = rule;
    line->color = options_.color();
    line->xOffset = xOffset + separator.x;
    line->yOffset = 0.0;
    line->shift = -1.0;
    line->text = separator.dashed ? QStringLiteral("dashed") : QString();
    array->children.push_back(std::move(line));
  }

  if (hasRightDelimiter) {
    auto right = makeDelimiter(node.rightDelim, targetDelimHeight, MathNodeType::Close);
    right->xOffset = xOffset + bodyWidth;
    right->yOffset = 0.0;
    rightDelimiterWidth = right->width;
    delimiterHeight = qMax(delimiterHeight, right->height);
    delimiterDepth = qMax(delimiterDepth, right->depth);
    array->children.push_back(std::move(right));
  }

  array->width = bodyWidth + xOffset + (node.rightDelim == QStringLiteral(".") ? nullDelimiterWidth : (hasRightDelimiter ? rightDelimiterWidth : 0.0));
  array->height = qMax(baseline, delimiterHeight);
  array->depth = qMax(totalHeight - baseline, delimiterDepth);
  return array;
}

std::unique_ptr<MathRenderNode> MathBuilder::makeError(const QString& text, const MathOptions& options) {
  auto node = std::make_unique<MathRenderNode>();
  node->kind = MathRenderKind::Error;
  node->text = text;
  node->font = options.fontForClass(QStringLiteral("main"));
  node->color = options.settings().errorColor;
  const QFontMetricsF metrics(node->font);
  node->width = metrics.horizontalAdvance(text);
  node->height = metrics.ascent();
  node->depth = metrics.descent();
  return node;
}

qreal MathBuilder::dimensionToPoints(const QString& value) const {
  const QString trimmed = value.trimmed();
  static const QRegularExpression re(QStringLiteral("^([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*([a-zA-Z]+)?$"));
  const QRegularExpressionMatch match = re.match(trimmed);
  if (!match.hasMatch()) {
    return 0.0;
  }
  const qreal number = match.captured(1).toDouble();
  const QString unit = match.captured(2).isEmpty() ? QStringLiteral("em") : match.captured(2);
  const qreal em = options_.fontPointSize();
  if (unit == QStringLiteral("em")) return number * em;
  if (unit == QStringLiteral("ex")) return number * em * 0.431;
  if (unit == QStringLiteral("mu")) return number * em / 18.0;
  if (unit == QStringLiteral("pt")) return number;
  if (unit == QStringLiteral("mm")) return number * 72.0 / 25.4;
  if (unit == QStringLiteral("cm")) return number * 72.0 / 2.54;
  if (unit == QStringLiteral("in")) return number * 72.0;
  if (unit == QStringLiteral("px")) return number;
  if (unit == QStringLiteral("bp")) return number;
  if (unit == QStringLiteral("pc")) return number * 12.0;
  if (unit == QStringLiteral("dd")) return number * 1238.0 / 1157.0;
  if (unit == QStringLiteral("cc")) return number * 12.0 * 1238.0 / 1157.0;
  if (unit == QStringLiteral("sp")) return number / 65536.0;
  return 0.0;
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

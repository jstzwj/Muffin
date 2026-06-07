#include "math/MathParser.h"

#include "math/MathFunctionRegistry.h"
#include "math/MathParseError.h"
#include "math/MathSymbols.h"

#include <QHash>
#include <QRegularExpression>

#include <numeric>

namespace muffin::math {
namespace {

QVector<std::shared_ptr<MathParseNode>> toSharedNodes(QVector<MathParseNode> nodes) {
  QVector<std::shared_ptr<MathParseNode>> shared;
  shared.reserve(nodes.size());
  for (MathParseNode& node : nodes) {
    shared.push_back(std::make_shared<MathParseNode>(std::move(node)));
  }
  return shared;
}

qreal sizeTextToEm(const QString& sizeText) {
  static const QHash<QString, qreal> unitToEm{
      {QStringLiteral("em"), 1.0},          {QStringLiteral("ex"), 0.431},       {QStringLiteral("mu"), 1.0 / 18.0},
      {QStringLiteral("pt"), 1.0 / 10.0},   {QStringLiteral("mm"), 0.2845},      {QStringLiteral("cm"), 2.845},
      {QStringLiteral("in"), 7.227},        {QStringLiteral("px"), 0.1},         {QStringLiteral("bp"), 0.1004},
      {QStringLiteral("pc"), 1.2},          {QStringLiteral("dd"), 0.1070},      {QStringLiteral("cc"), 1.284},
      {QStringLiteral("sp"), 1.0 / 655360.0}};
  QRegularExpression re(QStringLiteral("^([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*([a-zA-Z]+)?$"));
  const QRegularExpressionMatch match = re.match(sizeText.trimmed());
  if (!match.hasMatch()) {
    return 0.0;
  }
  const qreal number = match.captured(1).toDouble();
  const QString unit = match.captured(2).isEmpty() ? QStringLiteral("em") : match.captured(2).toLower();
  return number * unitToEm.value(unit, 1.0);
}

}  // namespace

MathParseNode MathParser::parseBeginEnvironment() {
  const QString name = parseRawGroupText(QStringLiteral("\\begin"));
  if (name == QStringLiteral("matrix") || name == QStringLiteral("pmatrix") || name == QStringLiteral("bmatrix") ||
      name == QStringLiteral("Bmatrix") || name == QStringLiteral("vmatrix") || name == QStringLiteral("Vmatrix") ||
      name == QStringLiteral("matrix*") || name == QStringLiteral("pmatrix*") || name == QStringLiteral("bmatrix*") ||
      name == QStringLiteral("Bmatrix*") || name == QStringLiteral("vmatrix*") || name == QStringLiteral("Vmatrix*") ||
      name == QStringLiteral("array") || name == QStringLiteral("darray") || name == QStringLiteral("smallmatrix") ||
      name == QStringLiteral("subarray") ||
      name == QStringLiteral("cases") || name == QStringLiteral("dcases") || name == QStringLiteral("rcases") ||
      name == QStringLiteral("drcases") || name == QStringLiteral("aligned") || name == QStringLiteral("split") ||
      name == QStringLiteral("align") || name == QStringLiteral("align*") || name == QStringLiteral("gathered") ||
      name == QStringLiteral("gather") || name == QStringLiteral("gather*") || name == QStringLiteral("alignedat") ||
      name == QStringLiteral("alignat") || name == QStringLiteral("alignat*")) {
    return parseArrayEnvironment(name);
  }
  return errorNode(QStringLiteral("Unsupported environment %1").arg(name));
}

MathParseNode MathParser::parseArrayEnvironment(const QString& name) {
  MathParseNode array;
  array.type = MathNodeType::Array;
  array.label = name;

  configureArrayEnvironment(array, name);

  if ((name == QStringLiteral("array") || name == QStringLiteral("darray")) && lexer_.peek().text == QStringLiteral("{")) {
    const QString preamble = parseRawGroupText(QStringLiteral("\\begin{array}"));
    parseArrayPreamble(array, preamble);
  } else if (name == QStringLiteral("subarray") && lexer_.peek().text == QStringLiteral("{")) {
    const QString alignment = parseRawGroupText(QStringLiteral("\\begin{subarray}")).trimmed();
    if (!alignment.isEmpty()) {
      const QChar align = alignment.at(0);
      if (align == QLatin1Char('l') || align == QLatin1Char('c')) {
        array.columns.clear();
        MathArrayColumn column;
        column.align = align;
        column.pregap = 0.0;
        column.postgap = 0.0;
        array.columns.push_back(column);
        array.columnAlignments = QString(align);
      }
    }
  } else if (name.endsWith(QLatin1Char('*')) && lexer_.peek().text == QStringLiteral("[")) {
    const QString alignment = parseOptionalBracketText().trimmed();
    if (!alignment.isEmpty()) {
      const QChar align = alignment.at(0);
      if (align == QLatin1Char('l') || align == QLatin1Char('c') || align == QLatin1Char('r')) {
        array.columns.clear();
        MathArrayColumn column;
        column.align = align;
        array.columns.push_back(column);
        array.columnAlignments = QString(align);
      }
    }
  }

  if (array.columnAlignments.isEmpty()) {
    array.columnAlignments = QStringLiteral("c");
  }
  if (array.columns.isEmpty()) {
    MathArrayColumn column;
    column.align = array.columnAlignments.at(0);
    array.columns.push_back(column);
  }

  QVector<MathArrayCell> row;
  consumeArrayHLines(array, 0);
  while (lexer_.peek().text != QStringLiteral("EOF")) {
    if (lexer_.peek().text == QStringLiteral("\\end")) {
      lexer_.consume();
      const QString endName = parseRawGroupText(QStringLiteral("\\end"));
      if (endName != name && settings_.throwOnError) {
        throw MathParseError(QStringLiteral("Mismatch: \\begin{%1} ended by \\end{%2}").arg(name, endName), QStringLiteral("\\end"), lexer_.peek().position);
      }
      break;
    }

    MathArrayCell cell;
    cell.body = toSharedNodes(parseExpressionUntilAny(
        {QStringLiteral("&"), QStringLiteral("\\\\"), QStringLiteral("\\cr"), QStringLiteral("\\end"), QStringLiteral("\\hline"), QStringLiteral("\\hdashline")}));
    row.push_back(std::move(cell));
    if (lexer_.peek().text == QStringLiteral("&")) {
      lexer_.consume();
    } else if (lexer_.peek().text == QStringLiteral("\\\\") || lexer_.peek().text == QStringLiteral("\\cr")) {
      lexer_.consume();
      array.rows.push_back(std::move(row));
      row = {};
      array.rowGaps.push_back(0.0);
      if (lexer_.peek().text == QStringLiteral("[")) {
        array.rowGaps[array.rowGaps.size() - 1] = sizeTextToEm(parseOptionalBracketText());
      }
      consumeArrayHLines(array, array.rows.size());
    } else if (lexer_.peek().text == QStringLiteral("\\hline") || lexer_.peek().text == QStringLiteral("\\hdashline")) {
      array.rows.push_back(std::move(row));
      row = {};
      array.rowGaps.push_back(0.0);
      consumeArrayHLines(array, array.rows.size());
    }
  }
  if (!row.isEmpty() || array.rows.isEmpty()) {
    array.rows.push_back(std::move(row));
  }

  if (array.colSeparationType == QStringLiteral("align") || array.colSeparationType == QStringLiteral("alignat")) {
    for (QVector<MathArrayCell>& alignedRow : array.rows) {
      for (int c = 1; c < alignedRow.size(); c += 2) {
        MathParseNode emptyGroup;
        emptyGroup.type = MathNodeType::Group;
        alignedRow[c].body.insert(0, std::make_shared<MathParseNode>(std::move(emptyGroup)));
      }
    }
  }

  const int colCount = std::accumulate(array.rows.cbegin(), array.rows.cend(), 0, [](int current, const QVector<MathArrayCell>& cells) {
    return qMax(current, cells.size());
  });
  while (array.columnAlignments.size() < colCount) {
    array.columnAlignments += array.columnAlignments.isEmpty() ? QLatin1Char('c') : array.columnAlignments.at(array.columnAlignments.size() - 1);
  }
  int alignCount = 0;
  for (const MathArrayColumn& column : array.columns) {
    if (column.type == MathArrayColumn::Type::Align) {
      ++alignCount;
    }
  }
  while (alignCount < colCount) {
    MathArrayColumn column;
    column.align = array.columnAlignments.at(qMin(alignCount, array.columnAlignments.size() - 1));
    array.columns.push_back(column);
    ++alignCount;
  }
  return array;
}

void MathParser::parseArrayPreamble(MathParseNode& array, const QString& preamble) {
  array.columns.clear();
  array.columnAlignments.clear();

  for (qsizetype i = 0; i < preamble.size(); ++i) {
    const QChar ch = preamble.at(i);
    if (ch == QLatin1Char('l') || ch == QLatin1Char('c') || ch == QLatin1Char('r')) {
      MathArrayColumn column;
      column.type = MathArrayColumn::Type::Align;
      column.align = ch;
      array.columns.push_back(column);
      array.columnAlignments += ch;
    } else if (ch == QLatin1Char('|') || ch == QLatin1Char(':')) {
      MathArrayColumn column;
      column.type = MathArrayColumn::Type::Separator;
      column.separator = ch;
      array.columns.push_back(column);
    } else if (ch == QLatin1Char('@') && i + 1 < preamble.size() && preamble.at(i + 1) == QLatin1Char('{')) {
      ++i;
      int depth = 1;
      QString content;
      while (++i < preamble.size() && depth > 0) {
        const QChar item = preamble.at(i);
        if (item == QLatin1Char('{')) {
          ++depth;
          content += item;
        } else if (item == QLatin1Char('}')) {
          --depth;
          if (depth > 0) {
            content += item;
          }
        } else {
          content += item;
        }
      }
      if (!array.columns.isEmpty()) {
        MathArrayColumn& previous = array.columns.last();
        if (previous.type == MathArrayColumn::Type::Align) {
          if (content.isEmpty()) {
            previous.postgap = 0.0;
          } else if (content == QStringLiteral("\\quad")) {
            previous.postgap = 1.0;
          } else if (content == QStringLiteral("\\qquad")) {
            previous.postgap = 2.0;
          }
        }
      }
    } else if (ch == QLatin1Char('p') || ch == QLatin1Char('m') || ch == QLatin1Char('b')) {
      MathArrayColumn column;
      column.type = MathArrayColumn::Type::Align;
      column.align = QLatin1Char('l');
      array.columns.push_back(column);
      array.columnAlignments += QLatin1Char('l');
      if (i + 1 < preamble.size() && preamble.at(i + 1) == QLatin1Char('{')) {
        ++i;
        int depth = 1;
        while (++i < preamble.size() && depth > 0) {
          if (preamble.at(i) == QLatin1Char('{')) {
            ++depth;
          } else if (preamble.at(i) == QLatin1Char('}')) {
            --depth;
          }
        }
      }
    }
  }
}

void MathParser::consumeArrayHLines(MathParseNode& array, int beforeRow) {
  while (lexer_.peek().text == QStringLiteral("\\hline") || lexer_.peek().text == QStringLiteral("\\hdashline")) {
    const QString token = lexer_.next().text;
    array.horizontalLines.push_back(beforeRow);
    MathArrayLine line;
    line.beforeRow = beforeRow;
    line.dashed = token == QStringLiteral("\\hdashline");
    array.arrayLines.push_back(line);
  }
}

void MathParser::configureArrayEnvironment(MathParseNode& array, const QString& name) {
  const QString baseName = QString(name).remove(QLatin1Char('*'));
  MathArrayColumn column;
  column.align = QLatin1Char('c');
  array.columns.push_back(column);
  array.columnAlignments = QStringLiteral("c");
  array.arrayCellStyle = baseName.startsWith(QLatin1Char('d')) ? QStringLiteral("display") : QStringLiteral("text");

  if (baseName == QStringLiteral("pmatrix")) {
    array.leftDelim = QStringLiteral("(");
    array.rightDelim = QStringLiteral(")");
  } else if (baseName == QStringLiteral("bmatrix")) {
    array.leftDelim = QStringLiteral("[");
    array.rightDelim = QStringLiteral("]");
  } else if (baseName == QStringLiteral("Bmatrix")) {
    array.leftDelim = QStringLiteral("\\lbrace");
    array.rightDelim = QStringLiteral("\\rbrace");
  } else if (baseName == QStringLiteral("vmatrix")) {
    array.leftDelim = QStringLiteral("|");
    array.rightDelim = QStringLiteral("|");
  } else if (baseName == QStringLiteral("Vmatrix")) {
    array.leftDelim = QStringLiteral("‖");
    array.rightDelim = QStringLiteral("‖");
  }

  if (baseName == QStringLiteral("array") || baseName == QStringLiteral("darray")) {
    array.hskipBeforeAndAfter = true;
  }

  if (baseName == QStringLiteral("smallmatrix")) {
    array.arrayStretch = 0.5;
    array.colSeparationType = QStringLiteral("small");
    array.arrayCellStyle = QStringLiteral("script");
  } else if (baseName == QStringLiteral("subarray")) {
    array.arrayStretch = 0.5;
    array.colSeparationType = QStringLiteral("small");
    array.arrayCellStyle = QStringLiteral("script");
    array.columns.clear();
    array.columnAlignments = QStringLiteral("c");
    MathArrayColumn column;
    column.align = QLatin1Char('c');
    column.pregap = 0.0;
    column.postgap = 0.0;
    array.columns.push_back(column);
  } else if (baseName == QStringLiteral("cases") || baseName == QStringLiteral("dcases") ||
             baseName == QStringLiteral("rcases") || baseName == QStringLiteral("drcases")) {
    array.arrayStretch = 1.2;
    array.leftDelim = baseName.contains(QLatin1Char('r')) ? QStringLiteral(".") : QStringLiteral("\\lbrace");
    array.rightDelim = baseName.contains(QLatin1Char('r')) ? QStringLiteral("\\rbrace") : QStringLiteral(".");
    array.columns.clear();
    array.columnAlignments = QStringLiteral("ll");
    MathArrayColumn first;
    first.align = QLatin1Char('l');
    first.pregap = 0.0;
    first.postgap = 1.0;
    MathArrayColumn second;
    second.align = QLatin1Char('l');
    second.pregap = 0.0;
    second.postgap = 0.0;
    array.columns.push_back(first);
    array.columns.push_back(second);
  } else if (baseName == QStringLiteral("aligned") || baseName == QStringLiteral("split") ||
             baseName == QStringLiteral("align") || baseName == QStringLiteral("alignat") ||
             baseName == QStringLiteral("alignedat")) {
    array.addJot = true;
    array.colSeparationType = baseName.contains(QStringLiteral("at")) ? QStringLiteral("alignat") : QStringLiteral("align");
    array.columns.clear();
    array.columnAlignments = QStringLiteral("rlrlrl");
    for (int i = 0; i < 6; ++i) {
      MathArrayColumn alignedColumn;
      alignedColumn.align = (i % 2 == 0) ? QLatin1Char('r') : QLatin1Char('l');
      alignedColumn.pregap = (i > 1 && i % 2 == 0 && array.colSeparationType == QStringLiteral("align")) ? 1.0 : 0.0;
      alignedColumn.postgap = 0.0;
      array.columns.push_back(alignedColumn);
    }
  } else if (baseName == QStringLiteral("gathered") || baseName == QStringLiteral("gather")) {
    array.addJot = true;
    array.colSeparationType = QStringLiteral("gather");
    array.columnAlignments = QStringLiteral("c");
  }
}

}  // namespace muffin::math

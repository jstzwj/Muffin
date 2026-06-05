#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>

namespace muffin {

class MarkdownNode;

struct TableCellSourceEdit {
  qsizetype replaceStart = 0;
  qsizetype replaceLength = 0;
  QString replacement;
  qsizetype nextSourceOffset = 0;
};

QString escapeTableCellInsertedText(QString text);
qsizetype tableCellSourceOffsetForVisibleOffset(const QString& content, qsizetype visibleOffset);
qsizetype tableCellVisibleOffsetForEditCursor(const MarkdownNode& cell, const QString& content, qsizetype sourceOffset);

std::optional<TableCellSourceEdit> buildTableCellInsertEdit(
    const MarkdownNode& cell,
    const QString& content,
    qsizetype sourceOffset,
    QString text);
std::optional<TableCellSourceEdit> buildTableCellDeleteBackwardEdit(
    const MarkdownNode& cell,
    const QString& content,
    qsizetype sourceOffset);
std::optional<TableCellSourceEdit> buildTableCellDeleteForwardEdit(
    const MarkdownNode& cell,
    const QString& content,
    qsizetype sourceOffset);

}  // namespace muffin

#pragma once

#include "document/InlineNode.h"
#include "document/TextSelection.h"

#include <QByteArray>
#include <QString>

namespace muffin {

class MarkdownDocument;
class MarkdownNode;

enum class SelectionExportFormat {
  Markdown,
  PlainText,
  Html
};

struct SelectionExportRequest {
  const MarkdownDocument* document = nullptr;
  SelectionRange selection;
  SelectionExportFormat format = SelectionExportFormat::Markdown;
};

struct SelectionExportResult {
  QString text;
  QString mimeType;
  QByteArray mimeData;

  bool isEmpty() const {
    return text.isEmpty() && mimeData.isEmpty();
  }
};

class SelectionSerializer {
public:
  SelectionExportResult exportSelection(const SelectionExportRequest& request) const;
  QString exportMarkdown(const MarkdownDocument& document, const SelectionRange& selection) const;
  QString exportPlainText(const MarkdownDocument& document, const SelectionRange& selection) const;
  QString exportHtml(const MarkdownDocument& document, const SelectionRange& selection) const;

private:
  struct EditableContext {
    const MarkdownNode* node = nullptr;
    const MarkdownNode* editableNode = nullptr;
    qsizetype sourceStart = -1;
    qsizetype sourceEnd = -1;
    QString sourceText;
  };

  bool selectionContext(const MarkdownDocument& document, const SelectionRange& selection, EditableContext& context, qsizetype& start,
                        qsizetype& end) const;
  bool editableContextFor(const MarkdownDocument& document, const MarkdownNode& displayNode, EditableContext& context) const;
  bool editableCursorSourceOffset(const MarkdownDocument& document, const CursorPosition& cursor, qsizetype& sourceOffset,
                                  qsizetype& contextSourceStart) const;
  bool selectionSourceRange(const MarkdownDocument& document, const SelectionRange& selection, qsizetype& start, qsizetype& end) const;
  bool blockSelectionSourceRange(const MarkdownDocument& document, const SelectionRange& selection, qsizetype& start, qsizetype& end) const;
  bool blockSourceRange(const MarkdownDocument& document, const MarkdownNode& node, qsizetype& start, qsizetype& end) const;
  qsizetype structuredLineStart(const QString& markdown, qsizetype contextSourceStart) const;
  qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column) const;
  qsizetype sourceOffsetForLineEnd(const QString& text, int line) const;
  const MarkdownNode* primaryParagraph(const MarkdownNode& node) const;
  QString listMarkerFor(const QString& line) const;
  bool listItemLineBounds(const QString& markdown, const EditableContext& context, qsizetype& lineStart, qsizetype& contentStart,
                          qsizetype& lineEnd) const;
  bool isPlainInlineEditable(const MarkdownNode& node, const QString& sourceText) const;
  QString plainTextForMarkdownRange(const QString& markdownText, qsizetype start, qsizetype end) const;
};

}  // namespace muffin

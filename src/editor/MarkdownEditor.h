#pragma once
#include "MarkdownPatch.h"
#include "parser/AstNode.h"
#include "theme/Theme.h"
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QStackedWidget>

class QTextBlock;

namespace Muffin {

class RenderedMarkdownView;

class MarkdownEditor : public QStackedWidget {
    Q_OBJECT

public:
    enum Mode { RenderedMode, SourceMode };

    explicit MarkdownEditor(QWidget* parent = nullptr);

    void setRenderedDocument(QTextDocument* doc);
    void setSourceText(const QString& text);
    QString sourceText() const;

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    void applyTheme(const Theme& theme);

    QTextDocument* renderedDocument() const;
    QPlainTextEdit* sourceEditor() const { return m_sourceEditor; }

    void highlightRenderedSourceRange(SourceRange range);
    void clearRenderedSourceRangeHighlight();
    int sourceLineForRenderedBlock(const QTextBlock& block) const;
    void setRenderedCursorPosition(int position);

signals:
    void modeChanged(Mode mode);
    void renderedSourceRangeClicked(SourceRange range);
    void renderedInlineTextSelected(SourceRange range, const QString& text);
    void renderedEditRequested(RenderedEdit edit);

private:
    RenderedMarkdownView* m_renderedView;
    QPlainTextEdit* m_sourceEditor;
    Mode m_mode = RenderedMode;
};

} // namespace Muffin

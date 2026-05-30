#pragma once
#include "editor/MarkdownCommand.h"
#include "editor/RenderedEdit.h"
#include "parser/AstNode.h"
#include "renderer/RenderFragment.h"
#include "renderer/RenderSourceMap.h"
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
    void setRenderedMarkdownSource(const QString& source);
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
    void setRenderedSelection(int start, int end);
    void setVisibleMarkerFragments(const QVector<RenderFragment>& fragments, const RenderSourceMap& sourceMap);
    void clearVisibleMarkerFragments();

signals:
    void modeChanged(Mode mode);
    void renderedSourceRangeClicked(SourceRange range);
    void renderedInlineTextSelected(SourceRange range, const QString& text);
    void renderedEditRequested(RenderedEdit edit);
    void renderedSelectionChanged(SourceSelection renderedSelection);
    void renderedMarkerClicked(RenderedMarkerHit hit);

private:
    RenderedMarkdownView* m_renderedView;
    QPlainTextEdit* m_sourceEditor;
    Mode m_mode = RenderedMode;
};

} // namespace Muffin

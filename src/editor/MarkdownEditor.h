#pragma once
#include "theme/Theme.h"
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QStackedWidget>

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

signals:
    void modeChanged(Mode mode);

private:
    RenderedMarkdownView* m_renderedView;
    QPlainTextEdit* m_sourceEditor;
    Mode m_mode = RenderedMode;
};

} // namespace Muffin

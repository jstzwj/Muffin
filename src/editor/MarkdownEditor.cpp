#include "MarkdownEditor.h"
#include <QPainter>
#include <QScrollBar>
#include <QVBoxLayout>

namespace Muffin {

class RenderedMarkdownView : public QTextBrowser {
public:
    explicit RenderedMarkdownView(QWidget* parent = nullptr)
        : QTextBrowser(parent)
    {
        setOpenExternalLinks(true);
        setReadOnly(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        document()->setDocumentMargin(0);
        updatePageLayout();
    }

    void setRenderedDocument(QTextDocument* doc)
    {
        setDocument(doc);
        document()->setDocumentMargin(0);
        updatePageLayout();
    }

    void applyTheme(const Theme& theme)
    {
        m_theme = theme;
        document()->setDocumentMargin(0);
        QPalette pal = palette();
        pal.setColor(QPalette::Base, theme.background);
        pal.setColor(QPalette::Text, theme.foreground);
        setPalette(pal);
        viewport()->setPalette(pal);
        setStyleSheet(QStringLiteral(
            "QTextBrowser { background: %1; color: %2; border: none; }"
            "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }"
            "QScrollBar::handle:vertical { background: rgba(120, 120, 120, 120); border-radius: 5px; min-height: 36px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
            .arg(theme.background.name(), theme.foreground.name()));
        updatePageLayout();
        viewport()->update();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QTextBrowser::resizeEvent(event);
        updatePageLayout();
    }

    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), m_theme.background);
        QTextBrowser::paintEvent(event);
    }

private:
    void updatePageLayout()
    {
        int available = qMax(320, width() - verticalScrollBar()->sizeHint().width());
        int pageWidth = qMin(800, qMax(320, available - 96));
        int sideMargin = qMax(24, (available - pageWidth) / 2);
        setViewportMargins(sideMargin, 42, sideMargin, 64);
        document()->setTextWidth(pageWidth);
    }

    Theme m_theme = Theme::preset(ThemePreset::Github);
};

MarkdownEditor::MarkdownEditor(QWidget* parent)
    : QStackedWidget(parent)
{
    m_renderedView = new RenderedMarkdownView(this);

    m_sourceEditor = new QPlainTextEdit(this);
    m_sourceEditor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont monoFont("Consolas", 10);
    m_sourceEditor->setFont(monoFont);

    addWidget(m_renderedView);  // index 0 = RenderedMode
    addWidget(m_sourceEditor);  // index 1 = SourceMode

    applyTheme(Theme::preset(ThemePreset::Github));
    setCurrentIndex(0);
}

void MarkdownEditor::setRenderedDocument(QTextDocument* doc) {
    // QTextBrowser takes ownership via setDocument
    m_renderedView->setRenderedDocument(doc);
}

void MarkdownEditor::setSourceText(const QString& text) {
    m_sourceEditor->setPlainText(text);
}

QString MarkdownEditor::sourceText() const {
    return m_sourceEditor->toPlainText();
}

void MarkdownEditor::setMode(Mode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    setCurrentIndex(mode == RenderedMode ? 0 : 1);
    emit modeChanged(mode);
}

void MarkdownEditor::applyTheme(const Theme& theme) {
    m_renderedView->applyTheme(theme);

    QPalette pal = m_sourceEditor->palette();
    pal.setColor(QPalette::Base, theme.pageBackground);
    pal.setColor(QPalette::Text, theme.foreground);
    pal.setColor(QPalette::Highlight, theme.accentColor);
    pal.setColor(QPalette::HighlightedText, theme.pageBackground);
    m_sourceEditor->setPalette(pal);
    m_sourceEditor->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: %1; color: %2; border: none; padding: 42px 96px 64px 96px; selection-background-color: %3; }")
        .arg(theme.pageBackground.name(), theme.foreground.name(), theme.accentColor.name()));
}

QTextDocument* MarkdownEditor::renderedDocument() const {
    return m_renderedView->document();
}

} // namespace Muffin

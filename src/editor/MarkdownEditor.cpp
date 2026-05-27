#include "MarkdownEditor.h"
#include "renderer/SourceBlockData.h"
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <functional>

namespace Muffin {

class RenderedMarkdownView : public QTextBrowser {
public:
    std::function<void(SourceRange)> sourceRangeClicked;
    std::function<void(SourceRange, const QString&)> inlineTextSelected;
    std::function<void(RenderedEdit)> editRequested;

    explicit RenderedMarkdownView(QWidget* parent = nullptr)
        : QTextBrowser(parent)
    {
        setOpenExternalLinks(true);
        setReadOnly(true);
        setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        setCursor(Qt::IBeamCursor);
        viewport()->setCursor(Qt::IBeamCursor);
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

    void highlightSourceRange(SourceRange range)
    {
        QList<QTextEdit::ExtraSelection> selections;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            SourceBlockData* sourceData = sourceBlockData(block);
            if (!sourceData || !sourceData->isValid()) {
                continue;
            }

            const SourceRange blockRange = sourceData->range();
            if (blockRange.startLine != range.startLine || blockRange.endLine != range.endLine) {
                continue;
            }

            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor(block);
            selection.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QColor highlight = m_theme.accentColor;
            highlight.setAlpha(36);
            selection.format.setBackground(highlight);
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selections.append(selection);
        }
        setExtraSelections(selections);
    }

    void clearSourceRangeHighlight()
    {
        setExtraSelections({});
    }

    void setRenderedCursorPosition(int position)
    {
        QTextCursor cursor(document());
        cursor.setPosition(qBound(0, position, document()->characterCount() - 1));
        setTextCursor(cursor);
        ensureCursorVisible();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QTextBrowser::resizeEvent(event);
        updatePageLayout();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressPosition = event->pos();
            QTextCursor cursor = cursorForPosition(event->pos());
            SourceBlockData* sourceData = sourceBlockData(cursor.block());
            m_pressedRange = sourceData && sourceData->isValid() ? sourceData->range() : SourceRange{};
        }

        QTextBrowser::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QTextBrowser::mouseReleaseEvent(event);

        if (event->button() != Qt::LeftButton || m_pressedRange.startLine <= 0) {
            return;
        }

        QTextCursor cursor = textCursor();
        const QString selectedText = cursor.selectedText().trimmed();
        if (!selectedText.isEmpty() && inlineTextSelected) {
            QTextCursor startCursor = cursor;
            startCursor.setPosition(cursor.selectionStart());
            QTextCursor endCursor = cursor;
            endCursor.setPosition(qMax(cursor.selectionStart(), cursor.selectionEnd() - 1));
            SourceBlockData* startData = sourceBlockData(startCursor.block());
            SourceBlockData* endData = sourceBlockData(endCursor.block());
            SourceRange range = m_pressedRange;
            if (startData && startData->isValid()) {
                range = startData->range();
                if (endData && endData->isValid()) {
                    SourceRange endRange = endData->range();
                    range.endLine = endRange.endLine;
                    range.endColumn = endRange.endColumn;
                }
            }
            inlineTextSelected(range, selectedText);
            return;
        }

        if ((event->pos() - m_pressPosition).manhattanLength() < QApplication::startDragDistance() && sourceRangeClicked) {
            sourceRangeClicked(m_pressedRange);
        }
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        QTextCursor cursor = textCursor();
        if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::SelectAll)) {
            QTextBrowser::keyPressEvent(event);
            return;
        }

        if (event->matches(QKeySequence::Paste)) {
            const QMimeData* mime = QApplication::clipboard()->mimeData();
            if (mime && mime->hasText()) {
                requestEdit(cursor.selectionStart(), cursor.selectionEnd(), mime->text());
            }
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
            event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
            event->key() == Qt::Key_Home || event->key() == Qt::Key_End ||
            event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
            QTextBrowser::keyPressEvent(event);
            return;
        }

        if (event->key() == Qt::Key_Backspace) {
            if (cursor.hasSelection()) {
                requestEdit(cursor.selectionStart(), cursor.selectionEnd(), {});
            } else if (cursor.position() > 0) {
                requestEdit(cursor.position() - 1, cursor.position(), {});
            }
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Delete) {
            if (cursor.hasSelection()) {
                requestEdit(cursor.selectionStart(), cursor.selectionEnd(), {});
            } else {
                requestEdit(cursor.position(), cursor.position() + 1, {});
            }
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            requestEdit(cursor.selectionStart(), cursor.selectionEnd(), QStringLiteral("\n"));
            event->accept();
            return;
        }

        const QString text = event->text();
        if (!text.isEmpty() && text != QStringLiteral("\r")) {
            requestEdit(cursor.selectionStart(), cursor.selectionEnd(), text);
            event->accept();
            return;
        }

        QTextBrowser::keyPressEvent(event);
    }

    void insertFromMimeData(const QMimeData* source) override
    {
        if (!source || !source->hasText()) {
            return;
        }

        QTextCursor cursor = textCursor();
        requestEdit(cursor.selectionStart(), cursor.selectionEnd(), source->text());
    }

    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), m_theme.background);
        QTextBrowser::paintEvent(event);
    }

private:
    SourceBlockData* sourceBlockData(const QTextBlock& block) const
    {
        return dynamic_cast<SourceBlockData*>(block.userData());
    }

    void requestEdit(int start, int end, const QString& replacement)
    {
        if (editRequested) {
            editRequested({qMin(start, end), qMax(start, end), replacement});
        }
    }

    void updatePageLayout()
    {
        int available = qMax(320, width() - verticalScrollBar()->sizeHint().width());
        int pageWidth = qMin(800, qMax(320, available - 96));
        int sideMargin = qMax(24, (available - pageWidth) / 2);
        setViewportMargins(sideMargin, 42, sideMargin, 64);
        document()->setTextWidth(pageWidth);
    }

    QPoint m_pressPosition;
    SourceRange m_pressedRange;
    Theme m_theme = Theme::preset(ThemePreset::Github);
};

MarkdownEditor::MarkdownEditor(QWidget* parent)
    : QStackedWidget(parent)
{
    m_renderedView = new RenderedMarkdownView(this);
    m_renderedView->sourceRangeClicked = [this](SourceRange range) {
        emit renderedSourceRangeClicked(range);
    };
    m_renderedView->inlineTextSelected = [this](SourceRange range, const QString& text) {
        emit renderedInlineTextSelected(range, text);
    };
    m_renderedView->editRequested = [this](RenderedEdit edit) {
        emit renderedEditRequested(edit);
    };

    m_sourceEditor = new QPlainTextEdit(this);
    m_sourceEditor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont monoFont("Consolas", 10);
    m_sourceEditor->setFont(monoFont);

    addWidget(m_renderedView);
    addWidget(m_sourceEditor);

    applyTheme(Theme::preset(ThemePreset::Github));
    setCurrentIndex(0);
}

void MarkdownEditor::setRenderedDocument(QTextDocument* doc) {
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

void MarkdownEditor::highlightRenderedSourceRange(SourceRange range)
{
    m_renderedView->highlightSourceRange(range);
}

void MarkdownEditor::clearRenderedSourceRangeHighlight()
{
    m_renderedView->clearSourceRangeHighlight();
}

int MarkdownEditor::sourceLineForRenderedBlock(const QTextBlock& block) const
{
    auto* sourceData = dynamic_cast<SourceBlockData*>(block.userData());
    return sourceData && sourceData->isValid() ? sourceData->range().startLine : 0;
}

void MarkdownEditor::setRenderedCursorPosition(int position)
{
    m_renderedView->setRenderedCursorPosition(position);
}

} // namespace Muffin

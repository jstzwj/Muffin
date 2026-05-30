#include "MarkdownEditor.h"
#include "editor/MarkerCaretController.h"
#include "editor/MarkerSelectionMapper.h"
#include "renderer/MarkerProjection.h"
#include "renderer/SourceBlockData.h"
#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
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
#include <optional>
#include <utility>

namespace Muffin {

class RenderedMarkdownView : public QTextBrowser {
public:
    std::function<void(SourceRange)> sourceRangeClicked;
    std::function<void(SourceRange, const QString&)> inlineTextSelected;
    std::function<void(RenderedEdit)> editRequested;
    std::function<void(RenderedMarkerHit)> markerClicked;

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
        clearMarkerEditCaret();
        document()->setDocumentMargin(0);
        updatePageLayout();
    }

    void setMarkdownSource(QString source)
    {
        m_markdownSource = std::move(source);
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

    void setVisibleMarkerFragments(const QVector<RenderFragment>& fragments, const RenderSourceMap& sourceMap)
    {
        m_markerProjection = MarkerProjection(fragments, sourceMap);
        viewport()->update();
    }

    void clearVisibleMarkerFragments()
    {
        if (m_markerProjection.markerSpans().isEmpty()) {
            return;
        }
        m_markerProjection = {};
        clearMarkerEditCaret();
        viewport()->update();
    }

    void setRenderedCursorPosition(int position)
    {
        QTextCursor cursor(document());
        cursor.setPosition(qBound(0, position, document()->characterCount() - 1));
        setTextCursor(cursor);
        ensureCursorVisible();
    }

    void setRenderedSelection(int start, int end)
    {
        QTextCursor cursor(document());
        const int maxPosition = qMax(0, document()->characterCount() - 1);
        cursor.setPosition(qBound(0, start, maxPosition));
        cursor.setPosition(qBound(0, end, maxPosition), QTextCursor::KeepAnchor);
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
            if (const std::optional<MarkerProjectionSpan> marker = markerSpanAtPoint(event->pos())) {
                m_markerSelectionAnchor.reset();
                m_markerSelection.reset();
                const QFontMetrics metrics(markerFont());
                const MarkerEditCaret caret = MarkerCaretController::caretForClick(*marker,
                                                                                   markerRectForSpan(*marker, metrics),
                                                                                   event->pos(),
                                                                                   metrics);
                m_markerSelectionAnchor = caret.projectedPosition;
                m_pressedRange = {1, 1, 1, 1};
                QTextCursor cleared = textCursor();
                cleared.clearSelection();
                setTextCursor(cleared);
                event->accept();
                return;
            }

            clearMarkerEditCaret();
            viewport()->update();
        }

        QTextBrowser::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) && m_markerSelectionAnchor) {
            if (const std::optional<int> projectedPosition = projectedPositionAtPoint(event->pos())) {
                m_markerSelection =
                    MarkerSelectionMapper::selectionForProjectedRange(m_markerProjection, *m_markerSelectionAnchor, *projectedPosition);
                viewport()->update();
                event->accept();
                return;
            }
        }

        QTextBrowser::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QTextBrowser::mouseReleaseEvent(event);

        if (event->button() != Qt::LeftButton || m_pressedRange.startLine <= 0) {
            return;
        }

        if (m_markerSelection && m_markerSelection->isValid()) {
            m_markerEditCaret.reset();
            viewport()->update();
            return;
        }

        if ((event->pos() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) {
            if (const std::optional<MarkerProjectionSpan> marker = markerSpanAtPoint(event->pos())) {
                const QFontMetrics metrics(markerFont());
                m_markerEditCaret = MarkerCaretController::caretForClick(*marker,
                                                                          markerRectForSpan(*marker, metrics),
                                                                          event->pos(),
                                                                          metrics);
                viewport()->update();
                if (markerClicked) {
                    markerClicked({marker->fragment.source,
                                   marker->fragment.nodeId,
                                   marker->fragment.markerKind,
                                   marker->text,
                                   marker->leadingMarker,
                                   marker->projectedStart,
                                   marker->projectedEnd});
                }
                return;
            }
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
        if (event->matches(QKeySequence::Copy)) {
            if (m_markerSelection && m_markerSelection->isValid()) {
                const QString text = markerSelectionText(*m_markerSelection);
                if (!text.isEmpty()) {
                    QApplication::clipboard()->setText(text);
                    event->accept();
                    return;
                }
            }
            QTextBrowser::keyPressEvent(event);
            return;
        }
        if (event->matches(QKeySequence::SelectAll)) {
            QTextBrowser::keyPressEvent(event);
            return;
        }

        if (event->matches(QKeySequence::Paste)) {
            const QMimeData* mime = QApplication::clipboard()->mimeData();
            if (mime && mime->hasText()) {
                requestEdit(RenderedEditOperation::Paste, cursor.selectionStart(), cursor.selectionEnd(), mime->text());
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
                requestEdit(RenderedEditOperation::ReplaceSelection, cursor.selectionStart(), cursor.selectionEnd(), {});
            } else if (cursor.position() > 0) {
                requestEdit(RenderedEditOperation::Backspace, cursor.position(), cursor.position(), {});
            }
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Delete) {
            if (cursor.hasSelection()) {
                requestEdit(RenderedEditOperation::ReplaceSelection, cursor.selectionStart(), cursor.selectionEnd(), {});
            } else {
                requestEdit(RenderedEditOperation::Delete, cursor.position(), cursor.position(), {});
            }
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            requestEdit(RenderedEditOperation::Enter, cursor.selectionStart(), cursor.selectionEnd(), QStringLiteral("\n"));
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
            const bool outdent = event->key() == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier);
            requestEdit(outdent ? RenderedEditOperation::Outdent : RenderedEditOperation::Indent,
                        cursor.selectionStart(), cursor.selectionEnd(), {});
            event->accept();
            return;
        }

        const QString text = event->text();
        if (!text.isEmpty() && text != QStringLiteral("\r")) {
            requestEdit(cursor.hasSelection() ? RenderedEditOperation::ReplaceSelection : RenderedEditOperation::InsertText,
                        cursor.selectionStart(), cursor.selectionEnd(), text);
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
        requestEdit(RenderedEditOperation::Paste, cursor.selectionStart(), cursor.selectionEnd(), source->text());
    }

    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), m_theme.background);
        QTextBrowser::paintEvent(event);
        paintVisibleMarkers();
        paintMarkerSelection();
        paintMarkerCaret();
    }

private:
    SourceBlockData* sourceBlockData(const QTextBlock& block) const
    {
        return dynamic_cast<SourceBlockData*>(block.userData());
    }

    void requestEdit(RenderedEditOperation operation, int start, int end, const QString& replacement)
    {
        if (editRequested) {
            if (m_markerSelection && m_markerSelection->isValid()
                && operation != RenderedEditOperation::Enter
                && operation != RenderedEditOperation::Indent
                && operation != RenderedEditOperation::Outdent) {
                const QString normalizedReplacement = operation == RenderedEditOperation::Backspace
                    || operation == RenderedEditOperation::Delete ? QString{} : replacement;
                const SourceSpan sourceSpan = m_markerSelection->source;
                editRequested({operation,
                               qMin(start, end),
                               qMax(start, end),
                               normalizedReplacement,
                               RenderedEdit::TargetKind::SourceSpan,
                               sourceSpan});
                m_markerEditCaret = MarkerEditCaret{sourceSpan,
                                                    sourceSpan.start + static_cast<int>(normalizedReplacement.size()),
                                                    m_markerSelection->projectedStart + static_cast<int>(normalizedReplacement.size()),
                                                    true};
                clearMarkerEditCaret();
                viewport()->update();
                return;
            }

            if (m_markerEditCaret && operation != RenderedEditOperation::Enter
                && operation != RenderedEditOperation::Indent && operation != RenderedEditOperation::Outdent) {
                if (const std::optional<SourceSpan> sourceSpan =
                        MarkerCaretController::sourceSpanForEdit(*m_markerEditCaret, operation)) {
                    const QString normalizedReplacement = operation == RenderedEditOperation::Backspace
                        || operation == RenderedEditOperation::Delete ? QString{} : replacement;
                    editRequested({operation,
                                   qMin(start, end),
                                   qMax(start, end),
                                   normalizedReplacement,
                                   RenderedEdit::TargetKind::SourceSpan,
                                   *sourceSpan});
                    clearMarkerEditCaret();
                    viewport()->update();
                    return;
                }
            }
            editRequested({operation, qMin(start, end), qMax(start, end), replacement});
        }
    }

    void clearMarkerEditCaret()
    {
        m_markerEditCaret.reset();
        m_markerSelection.reset();
        m_markerSelectionAnchor.reset();
    }

    QFont markerFont() const
    {
        QFont result = font();
        result.setPointSizeF(qMax(1.0, result.pointSizeF() * 0.9));
        return result;
    }

    void updatePageLayout()
    {
        int available = qMax(320, width() - verticalScrollBar()->sizeHint().width());
        int pageWidth = qMin(800, qMax(320, available - 96));
        int sideMargin = qMax(24, (available - pageWidth) / 2);
        setViewportMargins(sideMargin, 42, sideMargin, 64);
        document()->setTextWidth(pageWidth);
    }

    QRect markerRectForSpan(const MarkerProjectionSpan& marker, const QFontMetrics& metrics) const
    {
        QTextCursor cursor(document());
        cursor.setPosition(qBound(0, marker.baseRenderedAnchor, document()->characterCount() - 1));
        const QRect cursorRectangle = cursorRect(cursor);
        const int markerWidth = metrics.horizontalAdvance(marker.text);
        const int markerHeight = metrics.height();
        const int x = marker.leadingMarker
            ? cursorRectangle.left() - markerWidth - 2
            : cursorRectangle.left() + 2;
        const int y = cursorRectangle.bottom() - markerHeight + metrics.descent();
        return {x, y, markerWidth, markerHeight};
    }

    std::optional<MarkerProjectionSpan> markerSpanAtPoint(const QPoint& point) const
    {
        if (m_markerProjection.markerSpans().isEmpty()) {
            return std::nullopt;
        }

        const QFontMetrics metrics(markerFont());
        for (const MarkerProjectionSpan& marker : m_markerProjection.markerSpans()) {
            if (markerRectForSpan(marker, metrics).contains(point)) {
                return marker;
            }
        }
        return std::nullopt;
    }

    std::optional<int> projectedPositionAtPoint(const QPoint& point) const
    {
        if (const std::optional<MarkerProjectionSpan> marker = markerSpanAtPoint(point)) {
            const QFontMetrics metrics(markerFont());
            return MarkerCaretController::caretForClick(*marker,
                                                        markerRectForSpan(*marker, metrics),
                                                        point,
                                                        metrics).projectedPosition;
        }

        const QTextCursor cursor = cursorForPosition(point);
        return m_markerProjection.projectedPositionForBasePosition(cursor.position());
    }

    QString markerSelectionText(const MarkerSelection& selection) const
    {
        if (!selection.isValid()) {
            return {};
        }
        if (selection.kind == MarkerSelection::Kind::MarkerPairWithContent) {
            if (selection.source.start < 0 || selection.source.end < selection.source.start
                || selection.source.end > m_markdownSource.size()) {
                return {};
            }
            return m_markdownSource.mid(selection.source.start, selection.source.end - selection.source.start);
        }
        const int start = qBound(0, selection.source.start - selection.marker.fragment.source.start, selection.marker.text.size());
        const int end = qBound(start, selection.source.end - selection.marker.fragment.source.start, selection.marker.text.size());
        return selection.marker.text.mid(start, end - start);
    }

    void paintVisibleMarkers()
    {
        if (m_markerProjection.markerSpans().isEmpty()) {
            return;
        }

        QPainter painter(viewport());
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        const QFont font = markerFont();
        painter.setFont(font);
        QColor color = m_theme.foreground;
        color.setAlpha(128);
        painter.setPen(color);
        const QFontMetrics metrics(font);

        for (const MarkerProjectionSpan& marker : m_markerProjection.markerSpans()) {
            const QRect markerRect = markerRectForSpan(marker, metrics);
            const QPoint point(markerRect.left(), markerRect.bottom() - metrics.descent());
            painter.drawText(point, marker.text);
        }
    }

    void paintMarkerSelection()
    {
        if (!m_markerSelection || !m_markerSelection->isValid()) {
            return;
        }

        const QFont font = markerFont();
        const QFontMetrics metrics(font);
        for (const MarkerProjectionSpan& marker : m_markerProjection.markerSpans()) {
            if (m_markerSelection->kind == MarkerSelection::Kind::SingleMarker) {
                if (marker.fragment.source.start != m_markerSelection->marker.fragment.source.start
                    || marker.fragment.source.end != m_markerSelection->marker.fragment.source.end) {
                    continue;
                }

                const QRect markerRect = markerRectForSpan(marker, metrics);
                const int startOffset = qBound(0, m_markerSelection->source.start - marker.fragment.source.start, marker.text.size());
                const int endOffset = qBound(startOffset, m_markerSelection->source.end - marker.fragment.source.start, marker.text.size());
                const int x = markerRect.left() + metrics.horizontalAdvance(marker.text.left(startOffset));
                const int width = qMax(1, metrics.horizontalAdvance(marker.text.mid(startOffset, endOffset - startOffset)));

                QPainter painter(viewport());
                QColor color = m_theme.accentColor;
                color.setAlpha(80);
                painter.fillRect(QRect(x, markerRect.top(), width, markerRect.height()), color);
                return;
            }

            if (m_markerSelection->kind == MarkerSelection::Kind::MarkerPairWithContent
                && marker.fragment.nodeId == m_markerSelection->marker.fragment.nodeId
                && marker.fragment.markerKind == m_markerSelection->marker.fragment.markerKind) {
                QPainter painter(viewport());
                QColor color = m_theme.accentColor;
                color.setAlpha(80);
                painter.fillRect(markerRectForSpan(marker, metrics), color);
            }
        }
    }

    void paintMarkerCaret()
    {
        if (!m_markerEditCaret || !m_markerEditCaret->visible) {
            return;
        }

        const QFont font = markerFont();
        const QFontMetrics metrics(font);
        for (const MarkerProjectionSpan& marker : m_markerProjection.markerSpans()) {
            const QRect markerRect = markerRectForSpan(marker, metrics);
            const QRect caretRect = MarkerCaretController::caretRect(*m_markerEditCaret, marker, markerRect, metrics);
            if (!caretRect.isValid()) {
                continue;
            }

            QPainter painter(viewport());
            QColor color = m_theme.accentColor;
            color.setAlpha(220);
            painter.fillRect(caretRect.adjusted(0, 1, 1, -1), color);
            return;
        }
    }

    QPoint m_pressPosition;
    SourceRange m_pressedRange;
    Theme m_theme = Theme::preset(ThemePreset::Github);
    MarkerProjection m_markerProjection;
    std::optional<MarkerEditCaret> m_markerEditCaret;
    std::optional<int> m_markerSelectionAnchor;
    std::optional<MarkerSelection> m_markerSelection;
    QString m_markdownSource;
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
    m_renderedView->markerClicked = [this](RenderedMarkerHit hit) {
        emit renderedMarkerClicked(hit);
    };
    connect(m_renderedView, &QTextBrowser::cursorPositionChanged, this, [this]() {
        const QTextCursor cursor = m_renderedView->textCursor();
        emit renderedSelectionChanged({cursor.selectionStart(), cursor.selectionEnd()});
    });

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
    m_renderedView->clearVisibleMarkerFragments();
}

void MarkdownEditor::setSourceText(const QString& text) {
    m_sourceEditor->setPlainText(text);
    m_renderedView->setMarkdownSource(text);
}

void MarkdownEditor::setRenderedMarkdownSource(const QString& source)
{
    m_renderedView->setMarkdownSource(source);
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

void MarkdownEditor::setRenderedSelection(int start, int end)
{
    m_renderedView->setRenderedSelection(start, end);
}

void MarkdownEditor::setVisibleMarkerFragments(const QVector<RenderFragment>& fragments, const RenderSourceMap& sourceMap)
{
    m_renderedView->setVisibleMarkerFragments(fragments, sourceMap);
}

void MarkdownEditor::clearVisibleMarkerFragments()
{
    m_renderedView->clearVisibleMarkerFragments();
}

} // namespace Muffin

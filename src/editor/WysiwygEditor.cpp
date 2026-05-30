#include "WysiwygEditor.h"
#include "core/MdDocument.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/Theme.h"

#include <cmark-gfm.h>
#include <QResizeEvent>

namespace Md {

WysiwygEditor::WysiwygEditor(QWidget *parent)
    : QTextEdit(parent) {
    setReadOnly(true);
    setFrameShape(QFrame::NoFrame);
    setAcceptRichText(false);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setViewportMargins(0, 0, 0, 0);
    document()->setDocumentMargin(0);

    m_doc = std::make_unique<MdDocument>(this);
    m_renderer = std::make_unique<DocumentRenderer>();

    connect(m_doc.get(), &MdDocument::documentReset,
            this, &WysiwygEditor::onDocumentReset);
}

WysiwygEditor::~WysiwygEditor() = default;

bool WysiwygEditor::loadMarkdown(const QString &markdown) {
    return m_doc->loadFromMarkdown(markdown);
}

void WysiwygEditor::setTheme(const Theme &theme) {
    m_renderer->setTheme(theme);
    if (m_doc->cmarkRoot()) {
        onDocumentReset();
    }
}

void WysiwygEditor::onDocumentReset() {
    cmark_node *cmarkRoot = m_doc->cmarkRoot();
    if (cmarkRoot) {
        m_renderer->renderDocument(cmarkRoot, document());
        updateDocumentWidth();
    }
}

void WysiwygEditor::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    updateDocumentWidth();
}

void WysiwygEditor::updateDocumentWidth() {
    const int viewportWidth = viewport()->width();
    const int pageWidth = qMin(800, qMax(320, viewportWidth - 140));
    const int side = qMax(28, (viewportWidth - pageWidth) / 2);
    setViewportMargins(0, 0, 0, 0);
    document()->setDocumentMargin(side);
    document()->setTextWidth(pageWidth + side * 2);
}

} // namespace Md

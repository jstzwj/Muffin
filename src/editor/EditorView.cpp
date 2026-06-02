#include "editor/EditorView.h"

#include "document/MarkdownDocument.h"

#include <QCompleter>
#include <QByteArray>
#include <QElapsedTimer>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QLineEdit>
#include <QListView>
#include <QLoggingCategory>
#include <QScrollBar>
#include <QStringListModel>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <QFontMetricsF>

#include <cmath>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(viewPerf, "muffin.perf", QtWarningMsg)

InlineLayout::InlineGeometryBackend configuredInlineGeometryBackend() {
  const QByteArray value = qgetenv("MUFFIN_INLINE_GEOMETRY_BACKEND").trimmed().toLower();
  if (value == "qtextdocument" || value == "document" || value == "html") {
    return InlineLayout::InlineGeometryBackend::QTextDocument;
  }
  return InlineLayout::InlineGeometryBackend::QTextLayout;
}

const char* inlineGeometryBackendName(InlineLayout::InlineGeometryBackend backend) {
  switch (backend) {
    case InlineLayout::InlineGeometryBackend::QTextLayout:
      return "QTextLayout";
    case InlineLayout::InlineGeometryBackend::QTextDocument:
    default:
      return "QTextDocument";
  }
}

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(viewPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(viewPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin) {
  const QFontMetricsF metrics(font);
  const qreal lineHeight = qMax<qreal>(14.0, metrics.height());
  offset = qBound<qsizetype>(0, offset, literal.size());

  qsizetype lineStart = 0;
  int line = 0;
  for (qsizetype i = 0; i < offset && i < literal.size(); ++i) {
    if (literal.at(i) == QLatin1Char('\n')) {
      ++line;
      lineStart = i + 1;
    }
  }

  const qreal x = metrics.horizontalAdvance(literal.mid(lineStart, offset - lineStart));
  return QRectF(origin.x() + x, origin.y() + line * lineHeight, 1.0, lineHeight);
}

bool isSelectableZone(HitTestResult::Zone zone) {
  return zone == HitTestResult::Zone::Text || zone == HitTestResult::Zone::Code || zone == HitTestResult::Zone::Math ||
         zone == HitTestResult::Zone::Html || zone == HitTestResult::Zone::TableCell;
}

qsizetype selectableLength(const BlockLayout* block) {
  if (!block) {
    return 0;
  }
  if (const InlineLayout* inlineLayout = block->inlineLayout()) {
    return inlineLayout->plainText().size();
  }
  switch (block->type()) {
    case BlockType::CodeFence:
    case BlockType::MathBlock:
    case BlockType::HtmlBlock:
      return block->literal().size();
    case BlockType::Table:
      return 1;
    default:
      return 0;
  }
}

QRect viewportUpdateRect(QRectF documentRect, qreal scrollY, const QSize& viewportSize) {
  if (documentRect.isNull() || documentRect.isEmpty()) {
    return {};
  }
  documentRect.translate(0, -scrollY);
  return documentRect.adjusted(-4, -4, 4, 4).toAlignedRect().intersected(QRect(QPoint(0, 0), viewportSize));
}

void addRebuildDirtyRect(
    QRect& dirty,
    const DocumentLayout::BlockRebuildResult& result,
    QRectF documentViewport,
    qreal scrollY,
    const QSize& viewportSize) {
  dirty = dirty.united(viewportUpdateRect(result.oldRect.united(result.newRect), scrollY, viewportSize));
  if (!result.shiftedRect.isEmpty()) {
    dirty = dirty.united(viewportUpdateRect(result.shiftedRect.intersected(documentViewport), scrollY, viewportSize));
  }
}

}  // namespace

EditorView::EditorView(QWidget* parent) : QAbstractScrollArea(parent), layout_(std::make_unique<DocumentLayout>()) {
  inlineGeometryBackend_ = configuredInlineGeometryBackend();
  layout_->setInlineGeometryBackend(inlineGeometryBackend_);
  qCDebug(viewPerf).nospace() << "view.inlineGeometryBackend " << inlineGeometryBackendName(inlineGeometryBackend_);
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  viewport()->setMouseTracking(true);
  viewport()->setAutoFillBackground(false);
  setBackgroundRole(QPalette::Base);
  applyScrollBarStyle();
  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateCodeLanguageEditor(); });
  setCodeLanguageSuggestions({
      QStringLiteral("bash"),
      QStringLiteral("c"),
      QStringLiteral("cpp"),
      QStringLiteral("csharp"),
      QStringLiteral("css"),
      QStringLiteral("go"),
      QStringLiteral("html"),
      QStringLiteral("ini"),
      QStringLiteral("java"),
      QStringLiteral("javascript"),
      QStringLiteral("json"),
      QStringLiteral("kotlin"),
      QStringLiteral("lua"),
      QStringLiteral("markdown"),
      QStringLiteral("mermaid"),
      QStringLiteral("objective-c"),
      QStringLiteral("pascal"),
      QStringLiteral("pegjs"),
      QStringLiteral("perl"),
      QStringLiteral("perl6"),
      QStringLiteral("pgp"),
      QStringLiteral("php"),
      QStringLiteral("powershell"),
      QStringLiteral("python"),
      QStringLiteral("qml"),
      QStringLiteral("r"),
      QStringLiteral("ruby"),
      QStringLiteral("rust"),
      QStringLiteral("sql"),
      QStringLiteral("swift"),
      QStringLiteral("text"),
      QStringLiteral("toml"),
      QStringLiteral("typescript"),
      QStringLiteral("xml"),
      QStringLiteral("yaml"),
  });
}

void EditorView::setDocument(const MarkdownDocument& document) {
  document_ = &document;
  rebuildLayout();
}

bool EditorView::refreshBlock(NodeId blockId, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshBlock");
  if (!layout_ || document_ != &document) {
    return false;
  }

  const DocumentLayout::BlockRebuildResult result = layout_->rebuildBlock(blockId, document, theme_, selection_);
  if (!result.rebuilt) {
    return false;
  }
  if (!qFuzzyIsNull(result.heightDelta)) {
    updateScrollBars();
  }
  updateCursorHitFromPosition();
  QRect dirty;
  addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  if (dirty.isEmpty()) {
    dirty = viewport()->rect();
  }
  viewport()->update(dirty);
  return true;
}

bool EditorView::refreshBlocks(const QVector<NodeId>& blockIds, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshBlocks");
  if (!layout_ || document_ != &document) {
    return false;
  }

  QRect dirty;
  bool scrollbarDirty = false;
  for (NodeId blockId : blockIds) {
    const DocumentLayout::BlockRebuildResult result = layout_->rebuildBlock(blockId, document, theme_, selection_);
    if (!result.rebuilt) {
      return false;
    }
    scrollbarDirty = scrollbarDirty || !qFuzzyIsNull(result.heightDelta);
    addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  }
  if (scrollbarDirty) {
    updateScrollBars();
  }
  updateCursorHitFromPosition();
  viewport()->update(dirty.isEmpty() ? viewport()->rect() : dirty);
  return true;
}

void EditorView::setZoomPercent(int percent) {
  theme_.setZoomPercent(percent);
  applyScrollBarStyle();
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  rebuildLayout();
}

void EditorView::setTheme(RenderTheme theme) {
  const int zoom = theme_.zoomPercent();
  theme_ = std::move(theme);
  theme_.setZoomPercent(zoom);
  applyScrollBarStyle();
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  rebuildLayout();
}

void EditorView::setCursorHit(HitTestResult hit) {
  const SelectionRange previousSelection = selection_;
  cursorHit_ = hit;
  cursorPosition_ = hit.cursorPosition();
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  refreshInlineProjectionForSelectionChange(previousSelection);
}

void EditorView::setCursorPosition(CursorPosition position) {
  const SelectionRange previousSelection = selection_;
  cursorPosition_ = position;
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  refreshInlineProjectionForSelectionChange(previousSelection);
}

void EditorView::setSelectionRange(SelectionRange selection) {
  const SelectionRange previousSelection = selection_;
  selection_ = selection;
  cursorPosition_ = selection.focus;
  refreshInlineProjectionForSelectionChange(previousSelection);
}

void EditorView::clearCursor() {
  cursorPosition_ = {};
  selection_ = {};
  cursorHit_ = {};
  cursorVisible_ = false;
  updateCodeLanguageEditor();
  viewport()->update();
}

void EditorView::setCodeLanguageSuggestions(QStringList languages) {
  languages.removeDuplicates();
  languages.sort(Qt::CaseInsensitive);
  codeLanguageSuggestions_ = std::move(languages);
  if (codeLanguageCompleter_) {
    codeLanguageCompleter_->setModel(new QStringListModel(codeLanguageSuggestions_, codeLanguageCompleter_));
  }
}

void EditorView::setInlineGeometryBackend(InlineLayout::InlineGeometryBackend backend) {
  if (inlineGeometryBackend_ == backend) {
    return;
  }
  inlineGeometryBackend_ = backend;
  if (layout_) {
    layout_->setInlineGeometryBackend(backend);
  }
  rebuildLayout();
}

InlineLayout::InlineGeometryBackend EditorView::inlineGeometryBackend() const {
  return inlineGeometryBackend_;
}

QRectF EditorView::nodeRect(NodeId id) const {
  if (!layout_) {
    return {};
  }
  const BlockLayout* block = layout_->block(id);
  return block ? block->rect() : QRectF();
}

const BlockLayout* EditorView::blockAtViewportPos(QPointF viewportPos) const {
  if (!layout_) {
    return nullptr;
  }
  return layout_->blockAt(QPointF(viewportPos.x(), viewportPos.y() + scrollY()));
}

HitTestResult EditorView::hitTest(QPointF viewportPos) const {
  if (!layout_) {
    return {};
  }
  return layout_->hitTest(QPointF(viewportPos.x(), viewportPos.y() + scrollY()), theme_);
}

void EditorView::paintEvent(QPaintEvent* event) {
  PerfTimer perf("view.paint");
  Q_UNUSED(event);

  QPainter painter(viewport());
  painter.fillRect(viewport()->rect(), theme_.backgroundColor());

  if (!layout_) {
    return;
  }

  const QRectF visible = documentViewportRect();
  const QVector<const BlockLayout*> blocks = layout_->visibleBlocks(visible.adjusted(0, -80, 0, 80));
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  for (const BlockLayout* block : blocks) {
    block->paint(painter, theme_, scrollY());
  }
  paintSelection(painter);
  paintCurrentTableCell(painter);
  paintInsertionCursor(painter);
}

void EditorView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  rebuildLayout();
  updateCodeLanguageEditor();
}

void EditorView::wheelEvent(QWheelEvent* event) {
  QAbstractScrollArea::wheelEvent(event);
  updateCodeLanguageEditor();
}

void EditorView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    setFocus(Qt::MouseFocusReason);
    const HitTestResult hit = hitTest(event->position());
    if (event->modifiers().testFlag(Qt::ShiftModifier) && hit.isValid() && isSelectableZone(hit.zone) && cursorPosition_.isValid()) {
      SelectionRange range;
      range.anchor = selection_.anchor.isValid() ? selection_.anchor : cursorPosition_;
      range.focus = hit.cursorPosition();
      setSelectionRange(range);
      emit selectionChanged(range, hit);
      event->accept();
      return;
    }
    setCursorHit(hit);
    emit blockClicked(hit);
    updateCodeLanguageEditor();
    if (hit.isValid() && isSelectableZone(hit.zone)) {
      draggingSelection_ = true;
      dragAnchorHit_ = hit;
    }
  }
  QAbstractScrollArea::mousePressEvent(event);
}

void EditorView::mouseMoveEvent(QMouseEvent* event) {
  if (draggingSelection_ && (event->buttons() & Qt::LeftButton)) {
    updateDragSelection(event->position());
    event->accept();
    return;
  }
  updateMouseCursor(event->position());
  QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && draggingSelection_) {
    updateDragSelection(event->position());
    draggingSelection_ = false;
    event->accept();
    return;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

void EditorView::inputMethodEvent(QInputMethodEvent* event) {
  if (!event->commitString().isEmpty()) {
    emit textCommitted(event->commitString());
  }
  event->accept();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle) {
    QRectF cursor = cursorHit_.cursorRect;
    if (cursor.isEmpty()) {
      cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
    }
    cursor.translate(0, -scrollY());
    return cursor.toRect();
  }
  return QAbstractScrollArea::inputMethodQuery(query);
}

void EditorView::rebuildLayout() {
  PerfTimer perf("view.rebuildLayout");
  if (!layout_) {
    layout_ = std::make_unique<DocumentLayout>();
  }
  layout_->setInlineGeometryBackend(inlineGeometryBackend_);

  if (document_) {
    const int oldValue = verticalScrollBar()->value();
    layout_->rebuild(*document_, theme_, viewport()->width(), selection_);
    updateScrollBars();
    verticalScrollBar()->setValue(qMin(oldValue, verticalScrollBar()->maximum()));
    if (cursorPosition_.isValid()) {
      cursorHit_ = hitForCursorPosition(cursorPosition_);
      cursorVisible_ = cursorHit_.isValid();
    }
  } else {
    layout_ = std::make_unique<DocumentLayout>();
    updateScrollBars();
  }
  updateCodeLanguageEditor();
  viewport()->update();
}

void EditorView::updateScrollBars() {
  const int pageStep = qMax(1, viewport()->height());
  const int maximum = layout_ ? qMax(0, static_cast<int>(std::ceil(layout_->totalHeight() - viewport()->height()))) : 0;
  verticalScrollBar()->setPageStep(pageStep);
  verticalScrollBar()->setSingleStep(qMax(16, pageStep / 12));
  verticalScrollBar()->setRange(0, maximum);
}

QRectF EditorView::documentViewportRect() const {
  return QRectF(0, scrollY(), viewport()->width(), viewport()->height());
}

qreal EditorView::scrollY() const {
  return static_cast<qreal>(verticalScrollBar()->value());
}

void EditorView::applyScrollBarStyle() {
  const QString background = theme_.backgroundColor().name(QColor::HexRgb);
  setStyleSheet(QStringLiteral(
      "EditorView { background:%1; border:0; }"
      "QScrollBar:vertical { background:%1; width:8px; margin:0; }"
      "QScrollBar::handle:vertical { background:#b7b7b7; min-height:54px; border-radius:3px; margin:1px 2px; }"
      "QScrollBar::handle:vertical:hover { background:#999999; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; border:0; background:transparent; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
      "QScrollBar:horizontal { background:%1; height:8px; margin:0; }"
      "QScrollBar::handle:horizontal { background:#b7b7b7; min-width:54px; border-radius:3px; margin:2px 1px; }"
      "QScrollBar::handle:horizontal:hover { background:#999999; }"
      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; border:0; background:transparent; }"
      "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }")
                    .arg(background));
}

void EditorView::ensureCodeLanguageEditor() {
  if (codeLanguageEditor_) {
    return;
  }

  codeLanguageEditor_ = new QLineEdit(viewport());
  codeLanguageEditor_->setObjectName(QStringLiteral("codeLanguageEditor"));
  codeLanguageEditor_->setPlaceholderText(QStringLiteral("text"));
  codeLanguageEditor_->setClearButtonEnabled(false);
  codeLanguageEditor_->setFrame(false);
  codeLanguageEditor_->setFixedWidth(116);
  codeLanguageEditor_->hide();
  codeLanguageEditor_->setStyleSheet(QStringLiteral(
      "QLineEdit#codeLanguageEditor {"
      "  background:rgba(255,255,255,235);"
      "  color:#222222;"
      "  border:1px solid #d7dce2;"
      "  border-radius:4px;"
      "  padding:2px 8px;"
      "  selection-background-color:#2f80ed;"
      "  selection-color:#ffffff;"
      "  font-size:12px;"
      "}"));
  auto* shadow = new QGraphicsDropShadowEffect(codeLanguageEditor_);
  shadow->setBlurRadius(14.0);
  shadow->setOffset(0.0, 3.0);
  shadow->setColor(QColor(15, 23, 42, 35));
  codeLanguageEditor_->setGraphicsEffect(shadow);

  codeLanguageCompleter_ = new QCompleter(codeLanguageSuggestions_, codeLanguageEditor_);
  codeLanguageCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
  codeLanguageCompleter_->setFilterMode(Qt::MatchContains);
  codeLanguageCompleter_->setCompletionMode(QCompleter::PopupCompletion);
  auto* popup = new QListView(codeLanguageEditor_);
  popup->setObjectName(QStringLiteral("codeLanguagePopup"));
  popup->setUniformItemSizes(true);
  popup->setAlternatingRowColors(false);
  popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  popup->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  popup->setStyleSheet(QStringLiteral(
      "QListView#codeLanguagePopup {"
      "  background:#ffffff;"
      "  color:#333333;"
      "  border:1px solid #e1e4e8;"
      "  border-radius:5px;"
      "  padding:6px 0;"
      "  outline:0;"
      "  font-size:13px;"
      "}"
      "QListView#codeLanguagePopup::item {"
      "  min-height:28px;"
      "  padding:4px 12px;"
      "}"
      "QListView#codeLanguagePopup::item:selected {"
      "  background:#f1f6ff;"
      "  color:#111111;"
      "}"));
  codeLanguageCompleter_->setPopup(popup);
  codeLanguageEditor_->setCompleter(codeLanguageCompleter_);

  connect(codeLanguageEditor_, &QLineEdit::editingFinished, this, &EditorView::commitCodeLanguageEditor);
  connect(codeLanguageEditor_, &QLineEdit::returnPressed, this, &EditorView::commitCodeLanguageEditor);
  connect(codeLanguageEditor_, &QLineEdit::textEdited, this, [this] {
    if (codeLanguageCompleter_) {
      QRect rect = codeLanguageEditor_->rect();
      rect.setWidth(140);
      codeLanguageCompleter_->complete(rect);
    }
  });
}

void EditorView::updateCodeLanguageEditor() {
  if (updatingCodeLanguageEditor_) {
    return;
  }

  if (!layout_ || !document_ || !cursorPosition_.isValid() || cursorPosition_.blockId != cursorHit_.blockId ||
      cursorHit_.zone != HitTestResult::Zone::Code) {
    hideCodeLanguageEditor();
    return;
  }

  const BlockLayout* block = layout_->block(cursorPosition_.blockId);
  if (!block || block->type() != BlockType::CodeFence) {
    hideCodeLanguageEditor();
    return;
  }
  showCodeLanguageEditor(*block);
}

void EditorView::showCodeLanguageEditor(const BlockLayout& block) {
  ensureCodeLanguageEditor();
  if (!codeLanguageEditor_ || !document_) {
    return;
  }

  const MarkdownNode* node = document_->node(block.nodeId());
  if (!node || node->type() != BlockType::CodeFence) {
    hideCodeLanguageEditor();
    return;
  }

  updatingCodeLanguageEditor_ = true;
  codeLanguageNodeId_ = block.nodeId();
  if (!codeLanguageEditor_->hasFocus()) {
    const QString language = node->codeLanguage().isEmpty() ? QStringLiteral("text") : node->codeLanguage();
    if (codeLanguageEditor_->text() != language) {
      codeLanguageEditor_->setText(language);
    }
    codeLanguageEditor_->deselect();
    codeLanguageEditor_->setCursorPosition(codeLanguageEditor_->text().size());
  }

  const QRectF blockRect = block.rect().translated(0, -scrollY());
  codeLanguageEditor_->resize(116, 26);
  const int margin = 10;
  const int x = qRound(blockRect.right()) - codeLanguageEditor_->width() - margin;
  int y = qRound(blockRect.bottom()) - codeLanguageEditor_->height() / 2;
  if (y + codeLanguageEditor_->height() + margin > viewport()->height()) {
    y = qRound(blockRect.bottom()) - codeLanguageEditor_->height() - margin;
  }
  codeLanguageEditor_->move(qMax(0, x), qMax(0, y));
  codeLanguageEditor_->show();
  codeLanguageEditor_->raise();
  updatingCodeLanguageEditor_ = false;
}

void EditorView::hideCodeLanguageEditor() {
  codeLanguageNodeId_ = {};
  if (codeLanguageEditor_) {
    codeLanguageEditor_->hide();
  }
}

void EditorView::commitCodeLanguageEditor() {
  if (updatingCodeLanguageEditor_ || !codeLanguageEditor_ || !codeLanguageEditor_->isVisible() || !codeLanguageNodeId_.isValid()) {
    return;
  }
  QString language = codeLanguageEditor_->text().trimmed();
  if (language == QStringLiteral("text")) {
    language.clear();
  }
  if (document_) {
    const MarkdownNode* node = document_->node(codeLanguageNodeId_);
    if (node && node->codeLanguage() == language) {
      return;
    }
  }
  emit codeLanguageCommitted(codeLanguageNodeId_, language);
}

void EditorView::updateCursorHitFromPosition() {
  cursorHit_ = hitForCursorPosition(cursorPosition_);
  cursorVisible_ = cursorHit_.isValid();
  updateCodeLanguageEditor();
}

void EditorView::refreshInlineProjectionForSelectionChange(SelectionRange previousSelection) {
  QVector<NodeId> blockIds;
  addSelectionBlocks(blockIds, previousSelection);
  addSelectionBlocks(blockIds, selection_);

  bool refreshed = false;
  if (document_ && layout_ && !blockIds.isEmpty()) {
    refreshed = refreshBlocks(blockIds, *document_);
  }
  if (!refreshed) {
    updateCursorHitFromPosition();
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
    viewport()->update();
  } else {
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
  }
}

void EditorView::addSelectionBlocks(QVector<NodeId>& blockIds, const SelectionRange& selection) const {
  auto add = [&blockIds](NodeId id) {
    if (id.isValid() && !blockIds.contains(id)) {
      blockIds.push_back(id);
    }
  };
  add(selection.anchor.blockId);
  add(selection.focus.blockId);
}

void EditorView::paintSelection(QPainter& painter) const {
  if (!layout_ || selection_.isCollapsed()) {
    return;
  }

  const QColor color(79, 143, 247, 72);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);

  if (selection_.isSingleBlock()) {
    const BlockLayout* block = layout_->block(selection_.focus.blockId);
    if (block) {
      for (QRectF rect : block->selectionRects(selection_, theme_)) {
        rect.translate(0, -scrollY());
        painter.drawRoundedRect(rect, 2, 2);
      }
    }
  } else {
    const bool anchorFirst = blockComesBefore(selection_.anchor.blockId, selection_.focus.blockId);
    const QVector<const BlockLayout*> selectedBlocks = blocksBetween(selection_.anchor.blockId, selection_.focus.blockId);
    for (qsizetype i = 0; i < selectedBlocks.size(); ++i) {
      const BlockLayout* block = selectedBlocks.at(i);
      if (!block) {
        continue;
      }
      const bool isAnchor = block->nodeId() == selection_.anchor.blockId;
      const bool isFocus = block->nodeId() == selection_.focus.blockId;
      qsizetype start = 0;
      qsizetype end = selectableLength(block);
      if (isAnchor) {
        if (anchorFirst) {
          start = block->type() == BlockType::Table ? 0 : selection_.anchor.text.textOffset;
        } else {
          end = block->type() == BlockType::Table ? selectableLength(block) : selection_.anchor.text.textOffset;
        }
      }
      if (isFocus) {
        if (anchorFirst) {
          end = block->type() == BlockType::Table ? selectableLength(block) : selection_.focus.text.textOffset;
        } else {
          start = block->type() == BlockType::Table ? 0 : selection_.focus.text.textOffset;
        }
      }
      for (QRectF rect : block->selectionRectsForOffsets(start, end, theme_)) {
        rect.translate(0, -scrollY());
        painter.drawRoundedRect(rect, 2, 2);
      }
    }
  }
  painter.restore();
}

void EditorView::paintCurrentTableCell(QPainter& painter) const {
  if (!layout_ || cursorHit_.zone != HitTestResult::Zone::TableCell || cursorHit_.tableRow < 0 || cursorHit_.tableColumn < 0) {
    return;
  }

  const BlockLayout* table = layout_->block(cursorHit_.blockId);
  if (!table || table->type() != BlockType::Table) {
    return;
  }

  QRectF rect = table->tableCellRect(cursorHit_.tableRow, cursorHit_.tableColumn);
  if (rect.isEmpty()) {
    return;
  }
  rect.translate(0, -scrollY());

  painter.save();
  painter.setPen(QPen(theme_.linkColor(), 1.4));
  painter.setBrush(QColor(79, 143, 247, 28));
  painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
  painter.restore();
}

void EditorView::paintInsertionCursor(QPainter& painter) const {
  if (!cursorVisible_ || !cursorHit_.isValid()) {
    return;
  }

  QRectF cursor = cursorHit_.cursorRect;
  if (cursor.isEmpty()) {
    cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
  }
  cursor.translate(0, -scrollY());

  if (!viewport()->rect().adjusted(-4, -4, 4, 4).intersects(cursor.toAlignedRect())) {
    return;
  }

  const qreal height = qBound<qreal>(14.0, cursor.height(), 34.0);
  QRectF visibleCursor(cursor.left(), cursor.top(), 1.5, height);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(theme_.linkColor());
  painter.drawRect(visibleCursor);
  painter.restore();
}

HitTestResult EditorView::hitForCursorPosition(CursorPosition position) const {
  if (!layout_ || !position.isValid()) {
    return {};
  }

  const BlockLayout* block = layout_->block(position.blockId);
  if (!block) {
    return {};
  }

  HitTestResult hit;
  hit.blockId = position.blockId;
  hit.textNodeId = position.text.nodeId.isValid() ? position.text.nodeId : position.blockId;
  hit.textOffset = position.text.textOffset;
  hit.sourceOffset = position.text.sourceOffset;
  hit.blockRect = block->rect();
  hit.zone = HitTestResult::Zone::Block;

  switch (block->type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::ListItem:
      hit.zone = position.text.inMeta ? HitTestResult::Zone::Marker : HitTestResult::Zone::Text;
      if (const InlineLayout* inlineLayout = block->inlineLayout()) {
        const qreal textLeft = !block->listMarker().isEmpty() ? block->rect().left() + theme_.listIndent() : block->rect().left();
        const qsizetype localSourceOffset =
            position.text.sourceOffset >= 0 && block->contentSourceStart() >= 0 ? position.text.sourceOffset - block->contentSourceStart() : -1;
        hit.cursorRect = localSourceOffset >= 0
                             ? inlineLayout->cursorRectForSourceOffset(localSourceOffset).translated(QPointF(textLeft, block->rect().top()))
                             : inlineLayout->cursorRect(position.text.textOffset).translated(QPointF(textLeft, block->rect().top()));
      }
      break;
    case BlockType::CodeFence:
      hit.zone = HitTestResult::Zone::Code;
      hit.cursorRect = literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(),
                                                  block->rect().marginsRemoved(theme_.codePadding()).topLeft());
      break;
    case BlockType::MathBlock:
      hit.zone = HitTestResult::Zone::Math;
      hit.cursorRect = literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.mathFont(),
                                                  block->rect().marginsRemoved(theme_.codePadding()).topLeft());
      break;
    case BlockType::HtmlBlock:
      hit.zone = HitTestResult::Zone::Html;
      hit.cursorRect = literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(),
                                                  block->rect().marginsRemoved(theme_.codePadding()).topLeft());
      break;
    case BlockType::Table:
      hit.zone = HitTestResult::Zone::TableCell;
      hit.tableRow = -1;
      hit.tableColumn = -1;
      for (int row = 0; row < static_cast<int>(block->tableRows().size()); ++row) {
        const auto& tableRow = block->tableRows().at(static_cast<size_t>(row));
        for (int column = 0; column < static_cast<int>(tableRow.cells.size()); ++column) {
          if (tableRow.cells.at(static_cast<size_t>(column)).nodeId == hit.textNodeId) {
            hit.tableRow = row;
            hit.tableColumn = column;
            break;
          }
        }
        if (hit.tableRow >= 0) {
          break;
        }
      }
      if (hit.tableRow >= 0 && hit.tableColumn >= 0) {
        const QRectF cellRect = block->tableCellRect(hit.tableRow, hit.tableColumn);
        for (const auto& tableRow : block->tableRows()) {
          for (const auto& cell : tableRow.cells) {
            if (cell.nodeId == hit.textNodeId) {
              const QRectF contentRect = cell.rect.marginsRemoved(theme_.tableCellPadding());
              const qsizetype localSourceOffset =
                  position.text.sourceOffset >= 0 && cell.contentSourceStart >= 0 ? position.text.sourceOffset - cell.contentSourceStart : -1;
              hit.cursorRect = localSourceOffset >= 0
                                   ? cell.text.cursorRectForSourceOffset(localSourceOffset).translated(contentRect.topLeft())
                                   : cell.text.cursorRect(position.text.textOffset).translated(contentRect.topLeft());
              break;
            }
          }
        }
        if (hit.cursorRect.isEmpty()) {
          hit.cursorRect = QRectF(cellRect.left() + 6.0, cellRect.top() + 4.0, 1.0, qMax<qreal>(14.0, cellRect.height() - 8.0));
        }
      }
      break;
    default:
      break;
  }

  if (hit.cursorRect.isEmpty()) {
    hit.cursorRect = QRectF(hit.blockRect.left(), hit.blockRect.top(), 1.0, hit.blockRect.height());
  }
  return hit;
}

QVector<const BlockLayout*> EditorView::blocksBetween(NodeId first, NodeId last) const {
  QVector<const BlockLayout*> result;
  if (!layout_ || !first.isValid() || !last.isValid()) {
    return result;
  }

  const BlockLayout* firstBlock = layout_->block(first);
  const BlockLayout* lastBlock = layout_->block(last);
  if (!firstBlock || !lastBlock) {
    return result;
  }

  NodeId startId = first;
  NodeId endId = last;
  if (firstBlock->rect().top() > lastBlock->rect().top() ||
      (qFuzzyCompare(firstBlock->rect().top(), lastBlock->rect().top()) && firstBlock->rect().left() > lastBlock->rect().left())) {
    qSwap(startId, endId);
  }

  bool collecting = false;
  const auto collect = [&](const auto& self, const BlockLayout& block) -> void {
    if (block.nodeId() == startId) {
      collecting = true;
    }
    if (collecting) {
      result.push_back(&block);
    }
    if (block.nodeId() == endId) {
      collecting = false;
      return;
    }
    for (const auto& child : block.children()) {
      self(self, *child);
      if (!collecting && !result.isEmpty() && result.last()->nodeId() == endId) {
        return;
      }
    }
  };

  for (const auto& block : layout_->blocks()) {
    collect(collect, *block);
    if (!result.isEmpty() && result.last()->nodeId() == endId) {
      break;
    }
  }
  return result;
}

bool EditorView::blockComesBefore(NodeId first, NodeId second) const {
  if (first == second) {
    return true;
  }

  const QVector<const BlockLayout*> range = blocksBetween(first, second);
  return !range.isEmpty() && range.first()->nodeId() == first;
}

void EditorView::updateDragSelection(QPointF viewportPos) {
  if (!dragAnchorHit_.isValid()) {
    return;
  }

  const HitTestResult focusHit = hitTest(viewportPos);
  if (!focusHit.isValid() || !isSelectableZone(focusHit.zone)) {
    return;
  }

  SelectionRange range;
  range.anchor = dragAnchorHit_.cursorPosition();
  range.focus = focusHit.cursorPosition();
  setSelectionRange(range);
  emit selectionChanged(range, focusHit);
}

void EditorView::updateMouseCursor(QPointF viewportPos) {
  const HitTestResult hit = hitTest(viewportPos);
  if (hit.isValid() &&
      (hit.zone == HitTestResult::Zone::Text || hit.zone == HitTestResult::Zone::Code || hit.zone == HitTestResult::Zone::Math ||
       hit.zone == HitTestResult::Zone::Html)) {
    viewport()->setCursor(Qt::IBeamCursor);
  } else {
    viewport()->unsetCursor();
  }
}

}  // namespace muffin

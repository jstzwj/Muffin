#include "editor/SourceEditorWidget.h"

#include "io/FilePathOps.h"
#include "io/MuffinMime.h"
#include "spellcheck/SpellChecker.h"
#include "theme/RenderTheme.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QList>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPair>
#include <QPlainTextEdit>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <cmath>
#include <utility>

namespace {

constexpr int kContentWidth = 860;
constexpr int kHorizontalInset = 64;
constexpr int kLineNumberWidth = 48;
constexpr qreal kSourceLineSpacingScale = 1.7;
constexpr QChar kZeroWidthSpace(0x200b);

QTextCharFormat sourceFormat(QColor foreground, bool bold = false, double pointSize = 0.0) {
  QTextCharFormat result;
  result.setForeground(foreground);
  if (bold) {
    result.setFontWeight(QFont::Bold);
  }
  if (pointSize > 0.0) {
    result.setFontPointSize(pointSize);
  }
  return result;
}

// Every color the source page needs, derived in one place from the active RenderTheme. The widget
// stylesheet, the syntax highlighter, the line-number gutter, the current-line highlight and the
// zero-width-space markers all read from here, so a theme switch restyles the source page the same
// way it restyles the rendered view — instead of freezing it on a fixed light appearance.
struct SourceColors {
  QColor background;
  QColor gutterBackground;
  QColor text;
  QColor currentLine;
  QColor lineNumber;
  QColor selection;

  QColor heading;
  QColor listMarker;
  QColor linkLabel;
  QColor linkTarget;
  QColor inlineCodeText;
  QColor inlineCodeBackground;
  QColor fence;
  QColor quote;
  QColor emphasis;
  QColor table;
  QColor zeroWidthText;
  QColor zeroWidthBackground;
  QColor spell;

  static SourceColors fromTheme(const muffin::RenderTheme& theme) {
    // Dark test mirrors RenderTheme::codeHighlightColor so the source page flips on the same
    // boundary as every other theme-driven surface.
    const bool dark = theme.backgroundColor().lightness() < 128;
    SourceColors c;
    // Structural colors come straight from the theme so the source page tracks it exactly.
    c.background = theme.backgroundColor();
    c.text = theme.textColor();
    c.selection = theme.selectionColor();
    c.inlineCodeText = theme.textColor();
    c.inlineCodeBackground = theme.codeBackgroundColor();
    c.spell = theme.spellCheckColor();
    c.linkTarget = theme.linkColor();
    // Decorative accents have no theme field: keep the hand-tuned light values (zero regression on
    // light themes) and supply brighter dark counterparts that read on a #1f2328 page.
    c.heading = dark ? QColor(QStringLiteral("#ff8fb3")) : QColor(QStringLiteral("#e34f8b"));
    c.emphasis = dark ? QColor(QStringLiteral("#ff7ad9")) : QColor(QStringLiteral("#a00070"));
    c.listMarker = dark ? QColor(QStringLiteral("#ffb347")) : QColor(QStringLiteral("#c27a00"));
    c.linkLabel = dark ? QColor(QStringLiteral("#ffc070")) : QColor(QStringLiteral("#c77700"));
    c.fence = dark ? QColor(QStringLiteral("#e0a85a")) : QColor(QStringLiteral("#8a5a00"));
    c.quote = dark ? QColor(QStringLiteral("#8b949e")) : QColor(QStringLiteral("#7a7a7a"));
    c.table = dark ? QColor(QStringLiteral("#6cb6ff")) : QColor(QStringLiteral("#1a60a8"));
    c.lineNumber = dark ? QColor(QStringLiteral("#6e7681")) : QColor(QStringLiteral("#c9cdd3"));
    c.gutterBackground = dark ? QColor(QStringLiteral("#191c21")) : QColor(QStringLiteral("#fafbfc"));
    c.currentLine = dark ? QColor(QStringLiteral("#2a3038")) : QColor(QStringLiteral("#f3f3f3"));
    c.zeroWidthText = c.heading;
    c.zeroWidthBackground = dark ? QColor(QStringLiteral("#3a2230")) : QColor(QStringLiteral("#fff0f6"));
    return c;
  }
};

class MarkdownSourceHighlighter final : public QSyntaxHighlighter {
public:
  explicit MarkdownSourceHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
    rebuildFormats();
  }

  void setBasePointSize(double pointSize) {
    basePointSize_ = pointSize;
    rebuildFormats();
    rehighlight();
  }

  void setColors(const SourceColors& colors) {
    colors_ = colors;
    rebuildFormats();
    rehighlight();
  }

protected:
  void highlightBlock(const QString& text) override {
    if (text.startsWith(QStringLiteral("```")) || text.startsWith(QStringLiteral("~~~"))) {
      setFormat(0, text.size(), fenceFormat_);
      applyZeroWidthSpaces(text);
      return;  // fenced code block: syntax only, no spell checking
    }

    static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s.*)?$"));
    QRegularExpressionMatch heading = headingRe.match(text);
    if (heading.hasMatch()) {
      const int level = qBound(1, heading.capturedLength(1), 6);
      setFormat(0, text.size(), headingFormats_[level - 1]);
      applyZeroWidthSpaces(text);
    } else {
      static const QRegularExpression quoteRe(QStringLiteral("^\\s*>+"));
      QRegularExpressionMatch quote = quoteRe.match(text);
      if (quote.hasMatch()) {
        setFormat(quote.capturedStart(0), quote.capturedLength(0), quoteFormat_);
      }

      static const QRegularExpression listRe(QStringLiteral("^\\s*(?:[-+*]|\\d+[.)])\\s+"));
      QRegularExpressionMatch list = listRe.match(text);
      if (list.hasMatch()) {
        setFormat(list.capturedStart(0), list.capturedLength(0), markerFormat_);
      }

      static const QRegularExpression codeRe(QStringLiteral("`[^`]*`"));
      applyRegex(text, codeRe, codeFormat_);
      applyLinks(text);
      static const QRegularExpression emphasisRe(QStringLiteral("(\\*\\*|__|\\*|_|~~)"));
      applyRegex(text, emphasisRe, emphasisFormat_);
      static const QRegularExpression tableRe(QStringLiteral("\\|"));
      applyRegex(text, tableRe, tableFormat_);
      applyZeroWidthSpaces(text);
    }
    applySpellCheck(text);
  }

  // Underlines misspelled prose words, skipping inline code spans, URLs and email
  // addresses. A no-op while spell checking is disabled (SpellChecker::isCorrect then
  // reports every word as correct), so toggling is handled by a rehighlight only.
  void applySpellCheck(const QString& text) {
    auto& checker = muffin::SpellChecker::instance();
    if (!checker.isEnabled()) {
      return;
    }
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}][\\p{L}'\\x{2019}]*"),
                                           QRegularExpression::UseUnicodePropertiesOption);
    // Ranges that must never be spell-checked: inline code, URLs and emails.
    static const QRegularExpression codeRe(QStringLiteral("`[^`]*`"));
    static const QRegularExpression urlRe(QStringLiteral("\\b(?:https?|ftp|file)://\\S+|\\bwww\\.[^\\s]+"));
    static const QRegularExpression emailRe(QStringLiteral("[\\w.+-]+@[\\w-]+(?:\\.[\\w-]+)+"));
    QList<QPair<int, int>> skip;
    for (const QRegularExpression* re : {&codeRe, &urlRe, &emailRe}) {
      QRegularExpressionMatchIterator it = re->globalMatch(text);
      while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        skip.append({static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedEnd())});
      }
    }
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch m = it.next();
      const int start = static_cast<int>(m.capturedStart());
      const int end = static_cast<int>(m.capturedEnd());
      if (end - start <= 1) {
        continue;
      }
      bool inSkip = false;
      for (const QPair<int, int>& s : skip) {
        if (start < s.second && end > s.first) {
          inSkip = true;
          break;
        }
      }
      if (inSkip) {
        continue;
      }
      if (!checker.isCorrect(m.captured())) {
        setFormat(start, end - start, spellFormat_);
      }
    }
  }

private:
  void rebuildFormats() {
    headingFormats_[0] = sourceFormat(colors_.heading, true, basePointSize_ * 1.78);
    headingFormats_[1] = sourceFormat(colors_.heading, true, basePointSize_ * 1.36);
    headingFormats_[2] = sourceFormat(colors_.heading, true, basePointSize_ * 1.18);
    headingFormats_[3] = sourceFormat(colors_.heading, true, basePointSize_);
    headingFormats_[4] = sourceFormat(colors_.heading, true, basePointSize_);
    headingFormats_[5] = sourceFormat(colors_.heading, true, basePointSize_);
    markerFormat_ = sourceFormat(colors_.listMarker);
    linkLabelFormat_ = sourceFormat(colors_.linkLabel);
    linkTargetFormat_ = sourceFormat(colors_.linkTarget);
    codeFormat_ = sourceFormat(colors_.inlineCodeText);
    codeFormat_.setBackground(colors_.inlineCodeBackground);
    fenceFormat_ = sourceFormat(colors_.fence);
    quoteFormat_ = sourceFormat(colors_.quote);
    emphasisFormat_ = sourceFormat(colors_.emphasis);
    tableFormat_ = sourceFormat(colors_.table);
    zeroWidthFormat_ = sourceFormat(colors_.zeroWidthText, true);
    zeroWidthFormat_.setBackground(colors_.zeroWidthBackground);
    spellFormat_.setUnderlineColor(colors_.spell);
    spellFormat_.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
  }

  void applyZeroWidthSpaces(const QString& text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
      if (text.at(i) == kZeroWidthSpace) {
        setFormat(static_cast<int>(i), 1, zeroWidthFormat_);
      }
    }
  }

private:
  void applyRegex(const QString& text, const QRegularExpression& regex, const QTextCharFormat& textFormat) {
    QRegularExpressionMatchIterator it = regex.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch match = it.next();
      setFormat(match.capturedStart(), match.capturedLength(), textFormat);
    }
  }

  void applyLinks(const QString& text) {
    static const QRegularExpression linkRe(QStringLiteral("!?\\[([^\\]]*)\\]\\(([^\\)]*)\\)"));
    QRegularExpressionMatchIterator it = linkRe.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch match = it.next();
      setFormat(match.capturedStart(), match.capturedLength(), linkTargetFormat_);
      if (match.capturedStart(1) >= 0) {
        setFormat(match.capturedStart(1) - 1, match.capturedLength(1) + 2, linkLabelFormat_);
      }
      if (match.capturedStart(2) >= 0) {
        setFormat(match.capturedStart(2), match.capturedLength(2), linkTargetFormat_);
      }
    }
  }

  SourceColors colors_ = SourceColors::fromTheme(muffin::RenderTheme::github());
  double basePointSize_ = 12.5;
  QTextCharFormat headingFormats_[6];
  QTextCharFormat markerFormat_;
  QTextCharFormat linkLabelFormat_;
  QTextCharFormat linkTargetFormat_;
  QTextCharFormat codeFormat_;
  QTextCharFormat fenceFormat_;
  QTextCharFormat quoteFormat_;
  QTextCharFormat emphasisFormat_;
  QTextCharFormat tableFormat_;
  QTextCharFormat zeroWidthFormat_;
  QTextCharFormat spellFormat_;
};

}  // namespace

namespace muffin {

class LineNumberArea final : public QWidget {
public:
  explicit LineNumberArea(MarkdownSourceEdit* editor);

  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  MarkdownSourceEdit* editor_ = nullptr;
};

class MarkdownSourceEdit final : public QPlainTextEdit {
public:
  explicit MarkdownSourceEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent) {
    lineNumberFont_ = font();
    lineNumberFont_.setPointSizeF(10.0);
    setBackgroundVisible(false);
    document()->setDocumentMargin(0.0);
    highlighter_ = new MarkdownSourceHighlighter(document());
    lineNumberArea_ = new LineNumberArea(this);
    updateLineNumberAreaWidth();
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] {
      updateLineNumberAreaWidth();
      lineNumberArea_->update();
    });
    connect(this, &QPlainTextEdit::updateRequest, this, &MarkdownSourceEdit::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
      updateCurrentLineSelection();
      viewport()->update();
      lineNumberArea_->update();
    });
    // Re-scan for squiggles when spell checking is toggled or the dictionary changes.
    connect(&SpellChecker::instance(), &SpellChecker::enabledChanged, this, [this] {
      if (highlighter_) {
        highlighter_->rehighlight();
      }
    });
    connect(&SpellChecker::instance(), &SpellChecker::languageChanged, this, [this] {
      if (highlighter_) {
        highlighter_->rehighlight();
      }
    });
  }

  void applySourceColors(const SourceColors& colors) {
    colors_ = colors;
    if (highlighter_) {
      highlighter_->setColors(colors_);
    }
    updateCurrentLineSelection();
    lineNumberArea_->update();
    viewport()->update();
  }

  void setLineNumberFont(QFont font) {
    lineNumberFont_ = std::move(font);
    lineNumberArea_->update();
  }

  void setSourceFont(QFont font) {
    setFont(font);
    if (highlighter_) {
      highlighter_->setBasePointSize(font.pointSizeF());
    }
    setLineSpacingForFont(font);
  }

  void setDocumentPath(QString path) { documentPath_ = std::move(path); }

  int lineNumberAreaWidth() const {
    return kLineNumberWidth;
  }

  void paintLineNumberArea(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), colors_.gutterBackground);
    painter.setFont(lineNumberFont_);
    painter.setPen(colors_.lineNumber);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    while (block.isValid()) {
      const QRectF blockRect = blockBoundingGeometry(block).translated(contentOffset());
      if (blockRect.top() > viewport()->height()) {
        break;
      }
      if (blockRect.bottom() >= 0) {
        const int visibleLineNumber = blockNumber + 1;
        painter.drawText(QRectF(0, blockRect.top(), lineNumberAreaWidth() - 14, blockRect.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(visibleLineNumber));
      }
      block = block.next();
      ++blockNumber;
    }
  }

private:
  void updateCurrentLineSelection() {
    QTextEdit::ExtraSelection currentLine;
    currentLine.format.setBackground(colors_.currentLine);
    currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
    currentLine.cursor = textCursor();
    currentLine.cursor.clearSelection();
    setExtraSelections({currentLine});
  }

  // Directory of the document currently shown in the source editor (empty for an unsaved
  // buffer). Used to resolve a dropped file-tree path relative to the document when it lives
  // inside the document dir, so the inserted markdown link stays portable.
  QString documentPath_;

  void setLineSpacingForFont(const QFont& font) {
    const qreal lineHeight = qMax<qreal>(14.0, std::ceil(QFontMetricsF(font).lineSpacing() * kSourceLineSpacingScale));
    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(lineHeight, QTextBlockFormat::MinimumHeight);
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(blockFormat);
  }

protected:
  // Accept an in-app file-tree drag so QPlainTextEdit delivers it to insertFromMimeData
  // (its default canInsertFromMimeData already accepts hasUrls(), but being explicit about
  // the file-tree marker keeps the source-mode link insertion localized to that gesture).
  bool canInsertFromMimeData(const QMimeData* mimeData) const override {
    if (mimeData && mimeData->hasFormat(muffin::kMuffinFileTreeDragMime) && mimeData->hasUrls()) {
      return true;
    }
    return QPlainTextEdit::canInsertFromMimeData(mimeData);
  }

  // A file dragged from the sidebar into the source editor inserts a markdown link at the
  // drop position (QPlainTextEdit has already moved the cursor there). External file://
  // drops fall through to the base behavior.
  void insertFromMimeData(const QMimeData* mimeData) override {
    if (mimeData && mimeData->hasFormat(muffin::kMuffinFileTreeDragMime) && mimeData->hasUrls()) {
      const QList<QUrl> urls = mimeData->urls();
      if (!urls.isEmpty()) {
        const QUrl url = urls.first();
        if (url.isLocalFile()) {
          const QString filePath = url.toLocalFile();
          if (!QFileInfo(filePath).isDir()) {
            textCursor().insertText(muffin::FilePathOps::markdownLinkForFile(filePath, documentPath_));
            return;
          }
        }
      }
    }
    QPlainTextEdit::insertFromMimeData(mimeData);
  }

  void contextMenuEvent(QContextMenuEvent* event) override {
    QTextCursor cursor = cursorForPosition(event->pos());
    cursor.select(QTextCursor::WordUnderCursor);
    const QString word = cursor.selectedText();
    QMenu* menu = createStandardContextMenu(event->pos());
    menu->setParent(this);

    muffin::SpellChecker& checker = muffin::SpellChecker::instance();
    if (checker.isEnabled() && word.size() > 1 && !checker.isCorrect(word)) {
      const QStringList suggestions = checker.suggestions(word);
      QList<QAction*> spellActions;
      const int cap = 8;
      for (int i = 0; i < qMin(suggestions.size(), cap); ++i) {
        const QString suggestion = suggestions.at(i);
        QAction* replaceAction = new QAction(suggestion, menu);
        connect(replaceAction, &QAction::triggered, this, [cursor, suggestion]() {
          QTextCursor replaceCursor = cursor;
          replaceCursor.insertText(suggestion);
        });
        spellActions.append(replaceAction);
      }
      if (spellActions.isEmpty()) {
        QAction* none = new QAction(
            QCoreApplication::translate("muffin::MarkdownSourceEdit", "(no spelling suggestions)"), menu);
        none->setEnabled(false);
        spellActions.append(none);
      }
      QAction* ignoreAction = new QAction(
          QCoreApplication::translate("muffin::MarkdownSourceEdit", "Ignore \"%1\"").arg(word), menu);
      connect(ignoreAction, &QAction::triggered, this, [this, word]() {
        muffin::SpellChecker::instance().ignoreWord(word);
        if (highlighter_) {
          highlighter_->rehighlight();
        }
      });
      spellActions.append(ignoreAction);
      QAction* separator = new QAction(menu);
      separator->setSeparator(true);
      spellActions.append(separator);

      const QList<QAction*> existing = menu->actions();
      menu->insertActions(existing.isEmpty() ? nullptr : existing.first(), spellActions);
    }

    menu->exec(event->globalPos());
    delete menu;
  }

  void resizeEvent(QResizeEvent* event) override {
    QPlainTextEdit::resizeEvent(event);
    const QRect contentRect = contentsRect();
    lineNumberArea_->setGeometry(QRect(contentRect.left(), contentRect.top(), lineNumberAreaWidth(), contentRect.height()));
  }

  void paintEvent(QPaintEvent* event) override {
    QPlainTextEdit::paintEvent(event);
    QPainter painter(viewport());
    paintZeroWidthSpaces(painter);
  }

  void paintZeroWidthSpaces(QPainter& painter) {
    painter.save();
    painter.setPen(colors_.zeroWidthText);
    painter.setBrush(colors_.zeroWidthBackground);
    QTextBlock block = firstVisibleBlock();
    while (block.isValid()) {
      const QRectF blockRect = blockBoundingGeometry(block).translated(contentOffset());
      if (blockRect.top() > viewport()->height()) {
        break;
      }
      const QString text = block.text();
      for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) != kZeroWidthSpace) {
          continue;
        }
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + static_cast<int>(i));
        const QRect rect = cursorRect(cursor);
        const QRectF marker(rect.left() + 1.0, rect.center().y() - 5.0, 10.0, 10.0);
        painter.drawRoundedRect(marker, 3.0, 3.0);
        painter.drawText(marker, Qt::AlignCenter, QStringLiteral("Z"));
      }
      block = block.next();
    }
    painter.restore();
  }

  void updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
  }

  void updateLineNumberArea(const QRect& rect, int dy) {
    if (dy != 0) {
      lineNumberArea_->scroll(0, dy);
    } else {
      lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
      updateLineNumberAreaWidth();
    }
  }

  SourceColors colors_ = SourceColors::fromTheme(RenderTheme::github());
  MarkdownSourceHighlighter* highlighter_ = nullptr;
  LineNumberArea* lineNumberArea_ = nullptr;
  QFont lineNumberFont_;
};

LineNumberArea::LineNumberArea(MarkdownSourceEdit* editor) : QWidget(editor), editor_(editor) {
}

QSize LineNumberArea::sizeHint() const {
  return QSize(editor_->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
  editor_->paintLineNumberArea(event);
}

}  // namespace muffin

muffin::SourceEditorWidget::SourceEditorWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 30, 0, 0);
  layout->setSpacing(0);

  editor_ = new MarkdownSourceEdit(this);
  editor_->setFrameShape(QFrame::NoFrame);
  editor_->setMinimumWidth(0);
  retranslateUi();
  editor_->setTabStopDistance(32);
  editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  layout->addStretch(1);
  layout->addWidget(editor_, 0);
  layout->addStretch(1);

  setupStyle();
  updateEditorWidth();

  connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
    emit textEdited(editor_->toPlainText());
  });
  connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &SourceEditorWidget::emitCursorPosition);
}

QString muffin::SourceEditorWidget::text() const {
  return editor_->toPlainText();
}

void muffin::SourceEditorWidget::setText(const QString& text) {
  const QSignalBlocker blocker(editor_);
  editor_->setPlainText(text);
  editor_->setSourceFont(editor_->font());
  emitCursorPosition();
}

void muffin::SourceEditorWidget::setDocumentPath(const QString& path) {
  editor_->setDocumentPath(path);
}

void muffin::SourceEditorWidget::setWordWrapEnabled(bool enabled) {
  editor_->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void muffin::SourceEditorWidget::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
  applyFontSize();
}

void muffin::SourceEditorWidget::setFontSizePx(int px) {
  fontSizePx_ = qBound(12, px, 24);
  applyFontSize();
}

void muffin::SourceEditorWidget::setTheme(const RenderTheme& theme) {
  const SourceColors colors = SourceColors::fromTheme(theme);
  editor_->applySourceColors(colors);
  setStyleSheet(QStringLiteral(
                    "SourceEditorWidget { background:%1; }"
                    "QPlainTextEdit {"
                    "  background:%1;"
                    "  color:%2;"
                    "  selection-background-color:%3;"
                    "  padding:0 0 56px 0;"
                    "}")
                    .arg(colors.background.name(QColor::HexRgb),
                         colors.text.name(QColor::HexRgb),
                         colors.selection.name(QColor::HexRgb)));
}

QPlainTextEdit* muffin::SourceEditorWidget::editor() {
  return editor_;
}

const QPlainTextEdit* muffin::SourceEditorWidget::editor() const {
  return editor_;
}

void muffin::SourceEditorWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  updateEditorWidth();
}

void muffin::SourceEditorWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QWidget::changeEvent(event);
}

void muffin::SourceEditorWidget::setupStyle() {
  applyFontSize();
  setTheme(RenderTheme::github());
}

void muffin::SourceEditorWidget::retranslateUi() {
  if (editor_) {
    editor_->setPlaceholderText(QCoreApplication::translate("muffin::SourceEditorWidget", "Start writing..."));
  }
}

void muffin::SourceEditorWidget::applyFontSize() {
  const QSignalBlocker blocker(editor_);
  QFont font = editor_->font();
  const qreal scale = static_cast<qreal>(zoomPercent_) / 100.0 * static_cast<qreal>(fontSizePx_) / 16.0;
  font.setPointSizeF(qMax(8.0, 13.0 * scale));
  editor_->setSourceFont(font);
  QFont lineNumberFont = font;
  lineNumberFont.setPointSizeF(qMax(8.0, 10.0 * scale));
  editor_->setLineNumberFont(lineNumberFont);
}

void muffin::SourceEditorWidget::updateEditorWidth() {
  if (!editor_) {
    return;
  }
  const int availableWidth = qMax(0, width() - kHorizontalInset * 2);
  const int targetWidth = qMin(kContentWidth, availableWidth > 0 ? availableWidth : kContentWidth);
  editor_->setFixedWidth(targetWidth);
}

void muffin::SourceEditorWidget::emitCursorPosition() {
  const QTextCursor cursor = editor_->textCursor();
  emit cursorPositionChanged(cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
}

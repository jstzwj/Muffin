#include "editor/VirtualSourceEdit.h"

#include "document/DocumentSession.h"
#include "io/FilePathOps.h"
#include "io/MuffinMime.h"
#include "projection/SelectionSerializer.h"
#include "spellcheck/SpellChecker.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin {
namespace {

constexpr int kGutterWidth = 48;
constexpr int kTextInset = 16;
constexpr qreal kLineSpacingScale = 1.7;
constexpr qsizetype kLongLineThreshold = 16 * 1024;
constexpr int kMaxCachedLayouts = 256;
constexpr QChar kZeroWidthSpace(0x200b);

QTextCharFormat sourceFormat(QColor foreground, bool bold = false, qreal pointSize = 0.0) {
  QTextCharFormat format;
  format.setForeground(foreground);
  if (bold) format.setFontWeight(QFont::Bold);
  if (pointSize > 0.0) format.setFontPointSize(pointSize);
  return format;
}

void appendRegexFormats(
    const QString& text,
    const QRegularExpression& regex,
    const QTextCharFormat& format,
    QVector<QTextLayout::FormatRange>& ranges) {
  auto iterator = regex.globalMatch(text);
  while (iterator.hasNext()) {
    const QRegularExpressionMatch match = iterator.next();
    ranges.push_back(QTextLayout::FormatRange{
        static_cast<int>(match.capturedStart()),
        static_cast<int>(match.capturedLength()),
        format});
  }
}

QVector<QTextLayout::FormatRange> sourceFormats(
    const QString& text,
    const SourceEditorColors& colors,
    const QFont& baseFont) {
  QVector<QTextLayout::FormatRange> ranges;
  const auto whole = [&](const QTextCharFormat& format) {
    ranges.push_back(QTextLayout::FormatRange{0, static_cast<int>(text.size()), format});
  };

  static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s.*)?$"));
  const QRegularExpressionMatch heading = headingRe.match(text);
  if (text.startsWith(QStringLiteral("```")) || text.startsWith(QStringLiteral("~~~"))) {
    whole(sourceFormat(colors.fence));
  } else if (heading.hasMatch()) {
    static const qreal scales[6] = {1.78, 1.36, 1.18, 1.0, 1.0, 1.0};
    const int level = qBound(1, static_cast<int>(heading.capturedLength(1)), 6);
    whole(sourceFormat(colors.heading, true, baseFont.pointSizeF() * scales[level - 1]));
  } else {
    static const QRegularExpression quoteRe(QStringLiteral("^\\s*>+"));
    static const QRegularExpression listRe(QStringLiteral("^\\s*(?:[-+*]|\\d+[.)])\\s+"));
    static const QRegularExpression codeRe(QStringLiteral("`[^`]*`"));
    static const QRegularExpression linkRe(QStringLiteral("!?\\[([^\\]]*)\\]\\(([^\\)]*)\\)"));
    static const QRegularExpression emphasisRe(QStringLiteral("(\\*\\*|__|\\*|_|~~)"));
    static const QRegularExpression tableRe(QStringLiteral("\\|"));
    appendRegexFormats(text, quoteRe, sourceFormat(colors.quote), ranges);
    appendRegexFormats(text, listRe, sourceFormat(colors.listMarker), ranges);
    QTextCharFormat code = sourceFormat(colors.inlineCodeText);
    code.setBackground(colors.inlineCodeBackground);
    appendRegexFormats(text, codeRe, code, ranges);
    appendRegexFormats(text, emphasisRe, sourceFormat(colors.emphasis), ranges);
    appendRegexFormats(text, tableRe, sourceFormat(colors.table), ranges);

    auto links = linkRe.globalMatch(text);
    while (links.hasNext()) {
      const QRegularExpressionMatch match = links.next();
      ranges.push_back({static_cast<int>(match.capturedStart()),
                        static_cast<int>(match.capturedLength()),
                        sourceFormat(colors.linkTarget)});
      if (match.capturedStart(1) >= 0) {
        ranges.push_back({static_cast<int>(match.capturedStart(1) - 1),
                          static_cast<int>(match.capturedLength(1) + 2),
                          sourceFormat(colors.linkLabel)});
      }
    }
  }

  QTextCharFormat zeroWidth = sourceFormat(colors.zeroWidthText, true);
  zeroWidth.setBackground(colors.zeroWidthBackground);
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text.at(i) == kZeroWidthSpace) {
      ranges.push_back({static_cast<int>(i), 1, zeroWidth});
    }
  }

  SpellChecker& checker = SpellChecker::instance();
  if (checker.isEnabled()) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}][\\p{L}'\\x{2019}]*"),
        QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression skipRe(
        QStringLiteral("`[^`]*`|\\b(?:https?|ftp|file)://\\S+|\\bwww\\.[^\\s]+|[\\w.+-]+@[\\w-]+(?:\\.[\\w-]+)+"));
    QVector<QPair<int, int>> skip;
    auto skips = skipRe.globalMatch(text);
    while (skips.hasNext()) {
      const auto match = skips.next();
      skip.push_back({static_cast<int>(match.capturedStart()),
                      static_cast<int>(match.capturedEnd())});
    }
    QTextCharFormat spell;
    spell.setUnderlineColor(colors.spell);
    spell.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    auto words = wordRe.globalMatch(text);
    while (words.hasNext()) {
      const auto match = words.next();
      if (match.capturedLength() <= 1) continue;
      const int start = static_cast<int>(match.capturedStart());
      const int end = static_cast<int>(match.capturedEnd());
      const bool excluded = std::any_of(skip.cbegin(), skip.cend(), [start, end](const auto& range) {
        return start < range.second && end > range.first;
      });
      if (!excluded && !checker.isCorrect(match.captured())) {
        ranges.push_back({start, end - start, spell});
      }
    }
  }
  return ranges;
}

bool isWordCharacter(QChar ch) {
  return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

int boundedScrollRange(qint64 value) {
  return static_cast<int>(qBound<qint64>(
      qint64(0), value, static_cast<qint64>(std::numeric_limits<int>::max())));
}

QColor mixColor(const QColor& from, const QColor& to, qreal amount) {
  const qreal t = qBound<qreal>(0.0, amount, 1.0);
  return QColor::fromRgbF(
      from.redF() + (to.redF() - from.redF()) * t,
      from.greenF() + (to.greenF() - from.greenF()) * t,
      from.blueF() + (to.blueF() - from.blueF()) * t,
      from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

qreal linearColorChannel(qreal channel) {
  return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

qreal colorLuminance(const QColor& color) {
  return 0.2126 * linearColorChannel(color.redF()) +
         0.7152 * linearColorChannel(color.greenF()) +
         0.0722 * linearColorChannel(color.blueF());
}

qreal colorContrast(const QColor& a, const QColor& b) {
  const qreal lighter = qMax(colorLuminance(a), colorLuminance(b));
  const qreal darker = qMin(colorLuminance(a), colorLuminance(b));
  return (lighter + 0.05) / (darker + 0.05);
}

QColor ensureContrast(const QColor& color, const QColor& background, qreal minimum, bool dark) {
  if (colorContrast(color, background) >= minimum) return color;

  const QColor target = dark ? QColor(Qt::white) : QColor(Qt::black);
  qreal low = 0.0;
  qreal high = 1.0;
  for (int i = 0; i < 12; ++i) {
    const qreal middle = (low + high) / 2.0;
    if (colorContrast(mixColor(color, target, middle), background) >= minimum) {
      high = middle;
    } else {
      low = middle;
    }
  }
  return mixColor(color, target, high);
}

}  // namespace

struct VirtualSourceEdit::LineLayout {
  int line = 0;
  qsizetype sourceStart = 0;
  qsizetype length = 0;
  QString text;
  std::unique_ptr<QTextLayout> layout;
  int height = 1;
  qreal width = 0.0;
  bool longLine = false;
};

SourceEditorColors SourceEditorColors::fromTheme(const RenderTheme& theme) {
  const bool dark = theme.backgroundColor().lightness() < 128;
  SourceEditorColors colors;
  colors.background = theme.backgroundColor();
  colors.text = theme.textColor();
  colors.selection = theme.selectionColor();
  colors.inlineCodeText = theme.textColor();
  colors.inlineCodeBackground = theme.codeBackgroundColor();
  colors.spell = theme.spellCheckColor();
  colors.linkTarget = theme.linkColor();
  colors.heading = dark ? QColor(QStringLiteral("#ff8fb3")) : QColor(QStringLiteral("#e34f8b"));
  colors.emphasis = dark ? QColor(QStringLiteral("#ff7ad9")) : QColor(QStringLiteral("#a00070"));
  colors.listMarker = dark ? QColor(QStringLiteral("#ffb347")) : QColor(QStringLiteral("#c27a00"));
  colors.linkLabel = dark ? QColor(QStringLiteral("#ffc070")) : QColor(QStringLiteral("#c77700"));
  colors.fence = dark ? QColor(QStringLiteral("#e0a85a")) : QColor(QStringLiteral("#8a5a00"));
  colors.quote = dark ? QColor(QStringLiteral("#8b949e")) : QColor(QStringLiteral("#7a7a7a"));
  colors.table = dark ? QColor(QStringLiteral("#6cb6ff")) : QColor(QStringLiteral("#1a60a8"));
  // Keep source chrome in the active theme's tonal range. The old fixed
  // near-black gutter detached medium-dark themes such as Night from the page.
  colors.gutterBackground = mixColor(colors.background, QColor(Qt::black), dark ? 0.08 : 0.025);
  colors.lineNumber = ensureContrast(
      mixColor(colors.background, colors.text, 0.65), colors.gutterBackground, 3.1, dark);
  colors.currentLine = mixColor(colors.background, QColor(Qt::black), dark ? 0.12 : 0.04);
  colors.zeroWidthText = colors.heading;
  colors.zeroWidthBackground = dark ? QColor(QStringLiteral("#3a2230")) : QColor(QStringLiteral("#fff0f6"));
  return colors;
}

VirtualSourceEdit::VirtualSourceEdit(QWidget* parent)
    : QAbstractScrollArea(parent), colors_(SourceEditorColors::fromTheme(RenderTheme::github())) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  setAcceptDrops(true);
  setFrameShape(QFrame::NoFrame);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setCursor(Qt::IBeamCursor);
  viewport()->setMouseTracking(true);
  sourceFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  sourceFont_.setStyleHint(QFont::Monospace);
  sourceFont_.setFixedPitch(true);
  sourceFont_.setPointSizeF(13.0);
  lineNumberFont_ = sourceFont_;
  lineNumberFont_.setPointSizeF(10.0);
  cursorTimer_ = new QTimer(this);
  cursorTimer_->setInterval(QApplication::cursorFlashTime() / 2);
  connect(cursorTimer_, &QTimer::timeout, this, [this] {
    cursorVisible_ = !cursorVisible_;
    viewport()->update(cursorRectForOffset(cursor_).adjusted(-2, -2, 2, 2));
  });
  connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
  connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
  connect(&SpellChecker::instance(), &SpellChecker::enabledChanged, this, [this] {
    invalidateLayoutCache();
    viewport()->update();
  });
  connect(&SpellChecker::instance(), &SpellChecker::languageChanged, this, [this] {
    invalidateLayoutCache();
    viewport()->update();
  });
  applyScrollBarStyle();
  resetGeometryIndex(false);
}

const PieceTable& VirtualSourceEdit::source() const {
  return session_ ? session_->markdownText() : standalone_;
}

PieceTable& VirtualSourceEdit::standaloneSource() {
  return standalone_;
}

void VirtualSourceEdit::bindSession(DocumentSession* session) {
  if (session_ == session) return;
  session_ = session;
  undoStack_.clear();
  redoStack_.clear();
  syncFromSession(false);
}

void VirtualSourceEdit::syncFromSession(bool preserveCursor) {
  if (!preserveCursor) cursor_ = anchor_ = 0;
  cursor_ = boundedOffset(cursor_);
  anchor_ = boundedOffset(anchor_);
  resetGeometryIndex(preserveCursor);
  emitCursorPosition();
}

void VirtualSourceEdit::notifyDocumentChanged() {
  if (!applyingEdit_) {
    undoStack_.clear();
    redoStack_.clear();
  }
  cursor_ = boundedOffset(cursor_);
  anchor_ = boundedOffset(anchor_);
  resetGeometryIndex(true);
}

void VirtualSourceEdit::setStandaloneText(QString text) {
  session_ = nullptr;
  standalone_ = PieceTable(std::move(text));
  cursor_ = anchor_ = 0;
  undoStack_.clear();
  redoStack_.clear();
  resetGeometryIndex(false);
  emitCursorPosition();
}

QString VirtualSourceEdit::text() const { return source().toString(); }
qsizetype VirtualSourceEdit::cursorPosition() const { return cursor_; }
qsizetype VirtualSourceEdit::anchorPosition() const { return anchor_; }
qsizetype VirtualSourceEdit::selectionStart() const { return qMin(anchor_, cursor_); }
qsizetype VirtualSourceEdit::selectionEnd() const { return qMax(anchor_, cursor_); }
bool VirtualSourceEdit::hasSelection() const { return anchor_ != cursor_; }

QString VirtualSourceEdit::selectedText() const {
  return hasSelection() ? source().mid(selectionStart(), selectionEnd() - selectionStart()) : QString();
}

qsizetype VirtualSourceEdit::boundedOffset(qsizetype offset) const {
  return qBound<qsizetype>(0, offset, source().size());
}

qsizetype VirtualSourceEdit::previousCharacterOffset(qsizetype offset) const {
  offset = boundedOffset(offset);
  if (offset == 0) return 0;
  --offset;
  if (source().at(offset).isLowSurrogate() && offset > 0 &&
      source().at(offset - 1).isHighSurrogate()) {
    --offset;
  }
  return offset;
}

qsizetype VirtualSourceEdit::nextCharacterOffset(qsizetype offset) const {
  offset = boundedOffset(offset);
  if (offset >= source().size()) return source().size();
  if (source().at(offset).isHighSurrogate() && offset + 1 < source().size() &&
      source().at(offset + 1).isLowSurrogate()) {
    return offset + 2;
  }
  return offset + 1;
}

qsizetype VirtualSourceEdit::previousWordOffset(qsizetype offset) const {
  offset = boundedOffset(offset);
  while (offset > 0 && !isWordCharacter(source().at(previousCharacterOffset(offset)))) {
    offset = previousCharacterOffset(offset);
  }
  while (offset > 0 && isWordCharacter(source().at(previousCharacterOffset(offset)))) {
    offset = previousCharacterOffset(offset);
  }
  return offset;
}

qsizetype VirtualSourceEdit::nextWordOffset(qsizetype offset) const {
  offset = boundedOffset(offset);
  while (offset < source().size() && isWordCharacter(source().at(offset))) {
    offset = nextCharacterOffset(offset);
  }
  while (offset < source().size() && !isWordCharacter(source().at(offset))) {
    offset = nextCharacterOffset(offset);
  }
  return offset;
}

void VirtualSourceEdit::setCursorPosition(qsizetype position, bool keepAnchor) {
  moveCursorTo(position, keepAnchor);
  ensureCursorVisible();
}

void VirtualSourceEdit::setSelection(qsizetype start, qsizetype end) {
  anchor_ = boundedOffset(start);
  cursor_ = boundedOffset(end);
  preferredColumn_ = -1;
  resetCursorBlink();
  ensureCursorVisible();
  emitCursorPosition();
  viewport()->update();
}

void VirtualSourceEdit::selectAll() { setSelection(0, source().size()); }

int VirtualSourceEdit::lineForOffset(qsizetype offset) const {
  return qMax(0, source().lineForOffset(boundedOffset(offset)) - 1);
}

qsizetype VirtualSourceEdit::lineStart(int zeroBasedLine) const {
  return source().lineStartOffset(qBound(0, zeroBasedLine + 1, source().lineCount()));
}

qsizetype VirtualSourceEdit::lineEnd(int zeroBasedLine) const {
  return source().lineEndOffset(qBound(0, zeroBasedLine + 1, source().lineCount()));
}

QString VirtualSourceEdit::lineText(int zeroBasedLine) const {
  const qsizetype start = lineStart(zeroBasedLine);
  const qsizetype end = lineEnd(zeroBasedLine);
  return start >= 0 && end >= start ? source().mid(start, end - start) : QString();
}

void VirtualSourceEdit::selectLine() {
  const int line = lineForOffset(cursor_);
  setSelection(lineStart(line), lineEnd(line));
}

std::pair<qsizetype, qsizetype> VirtualSourceEdit::wordRangeAt(qsizetype position) const {
  const qsizetype bounded = boundedOffset(position);
  const int line = lineForOffset(bounded);
  const qsizetype startBound = lineStart(line);
  const qsizetype endBound = lineEnd(line);
  qsizetype probe = bounded;
  if (probe == endBound && probe > startBound) --probe;
  if (probe < startBound || probe >= endBound || !isWordCharacter(source().at(probe))) {
    return {bounded, bounded};
  }
  qsizetype start = probe;
  qsizetype end = probe + 1;
  while (start > startBound && isWordCharacter(source().at(start - 1))) --start;
  while (end < endBound && isWordCharacter(source().at(end))) ++end;
  return {start, end};
}

void VirtualSourceEdit::selectWord() {
  const auto [start, end] = wordRangeAt(cursor_);
  setSelection(start, end);
}

bool VirtualSourceEdit::applyEdit(
    qsizetype start, qsizetype end, const QString& inserted, bool recordUndo) {
  if (readOnly_) return false;
  start = boundedOffset(start);
  end = qBound(start, end, source().size());
  EditRecord record;
  record.start = start;
  record.removed = source().mid(start, end - start);
  record.inserted = inserted;
  record.beforeAnchor = anchor_;
  record.beforeCursor = cursor_;
  record.afterAnchor = record.afterCursor = start + inserted.size();

  bool applied = true;
  applyingEdit_ = true;
  if (session_) {
    applied = session_->applyTextDelta(start, end - start, inserted, true);
  } else {
    standaloneSource().replace(start, end, inserted);
  }
  applyingEdit_ = false;
  if (!applied) return false;

  anchor_ = record.afterAnchor;
  cursor_ = record.afterCursor;
  if (recordUndo) {
    undoStack_.push_back(record);
    redoStack_.clear();
  }
  resetGeometryIndex(true);
  preferredColumn_ = -1;
  resetCursorBlink();
  ensureCursorVisible();
  emitCursorPosition();
  emit editApplied();
  viewport()->update();
  return true;
}

void VirtualSourceEdit::insertText(const QString& text) {
  if (text.isEmpty()) return;
  QString normalized = text;
  normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  applyEdit(selectionStart(), selectionEnd(), normalized, true);
}

void VirtualSourceEdit::replaceSelection(const QString& text) {
  if (hasSelection()) insertText(text);
}

void VirtualSourceEdit::deleteForward() {
  if (hasSelection()) {
    applyEdit(selectionStart(), selectionEnd(), {}, true);
  } else if (cursor_ < source().size()) {
    qsizetype end = cursor_ + 1;
    if (source().at(cursor_).isHighSurrogate() && end < source().size() && source().at(end).isLowSurrogate()) ++end;
    applyEdit(cursor_, end, {}, true);
  }
}

void VirtualSourceEdit::deleteBackward() {
  if (hasSelection()) {
    applyEdit(selectionStart(), selectionEnd(), {}, true);
  } else if (cursor_ > 0) {
    qsizetype start = cursor_ - 1;
    if (source().at(start).isLowSurrogate() && start > 0 && source().at(start - 1).isHighSurrogate()) --start;
    applyEdit(start, cursor_, {}, true);
  }
}

void VirtualSourceEdit::deleteWord() {
  const auto [start, end] = wordRangeAt(cursor_);
  if (start != end) applyEdit(start, end, {}, true);
}

void VirtualSourceEdit::deleteLineContent() {
  const int line = lineForOffset(cursor_);
  applyEdit(lineStart(line), lineEnd(line), {}, true);
}

void VirtualSourceEdit::deleteWholeLine() {
  const int line = lineForOffset(cursor_);
  qsizetype start = lineStart(line);
  qsizetype end = lineEnd(line);
  if (line + 1 < source().lineCount()) {
    end = lineStart(line + 1);
  }
  applyEdit(start, end, {}, true);
}

void VirtualSourceEdit::moveCurrentLineUp() {
  const int line = lineForOffset(cursor_);
  if (line <= 0) return;
  const qsizetype previousStart = lineStart(line - 1);
  const qsizetype currentStart = lineStart(line);
  const qsizetype currentEnd = lineEnd(line);
  const QString previous = source().mid(previousStart, currentStart - previousStart - 1);
  const QString current = source().mid(currentStart, currentEnd - currentStart);
  const qsizetype column = cursor_ - currentStart;
  if (applyEdit(previousStart, currentEnd, current + QLatin1Char('\n') + previous, true)) {
    setCursorPosition(previousStart + qMin(column, static_cast<qsizetype>(current.size())));
  }
}

void VirtualSourceEdit::moveCurrentLineDown() {
  const int line = lineForOffset(cursor_);
  if (line + 1 >= source().lineCount()) return;
  const qsizetype currentStart = lineStart(line);
  const qsizetype nextStart = lineStart(line + 1);
  const qsizetype nextEnd = lineEnd(line + 1);
  const QString current = source().mid(currentStart, nextStart - currentStart - 1);
  const QString next = source().mid(nextStart, nextEnd - nextStart);
  const qsizetype column = cursor_ - currentStart;
  if (applyEdit(currentStart, nextEnd, next + QLatin1Char('\n') + current, true)) {
    setCursorPosition(currentStart + next.size() + 1 + qMin(column, static_cast<qsizetype>(current.size())));
  }
}

bool VirtualSourceEdit::canUndo() const { return !readOnly_ && !undoStack_.isEmpty(); }
bool VirtualSourceEdit::canRedo() const { return !readOnly_ && !redoStack_.isEmpty(); }

bool VirtualSourceEdit::applyEditRecord(const EditRecord& record, bool reverse) {
  const QString& remove = reverse ? record.inserted : record.removed;
  const QString& insert = reverse ? record.removed : record.inserted;
  if (applyEdit(record.start, record.start + remove.size(), insert, false)) {
    anchor_ = reverse ? record.beforeAnchor : record.afterAnchor;
    cursor_ = reverse ? record.beforeCursor : record.afterCursor;
    ensureCursorVisible();
    emitCursorPosition();
    return true;
  }
  return false;
}

void VirtualSourceEdit::undo() {
  if (!canUndo()) return;
  EditRecord record = undoStack_.takeLast();
  if (applyEditRecord(record, true)) {
    redoStack_.push_back(std::move(record));
  } else {
    undoStack_.push_back(std::move(record));
  }
}

void VirtualSourceEdit::redo() {
  if (!canRedo()) return;
  EditRecord record = redoStack_.takeLast();
  if (applyEditRecord(record, false)) {
    undoStack_.push_back(std::move(record));
  } else {
    redoStack_.push_back(std::move(record));
  }
}

void VirtualSourceEdit::moveDocumentStart() { setCursorPosition(0); }
void VirtualSourceEdit::moveDocumentEnd() { setCursorPosition(source().size()); }
void VirtualSourceEdit::moveLineStart() { setCursorPosition(lineStart(lineForOffset(cursor_))); }
void VirtualSourceEdit::moveLineEnd() { setCursorPosition(lineEnd(lineForOffset(cursor_))); }

void VirtualSourceEdit::moveLineVertical(int delta) {
  const int currentLine = lineForOffset(cursor_);
  if (preferredColumn_ < 0) preferredColumn_ = cursorColumn() - 1;
  const int targetLine = qBound(0, currentLine + delta, source().lineCount() - 1);
  setCursorPosition(qMin(lineStart(targetLine) + preferredColumn_, lineEnd(targetLine)));
  preferredColumn_ = qMax(0, preferredColumn_);
}

void VirtualSourceEdit::selectNextOccurrence() {
  QString needle = selectedText();
  if (needle.isEmpty()) {
    const auto [start, end] = wordRangeAt(cursor_);
    needle = source().mid(start, end - start);
  }
  if (needle.isEmpty()) return;
  qsizetype found = source().indexOf(needle, selectionEnd());
  if (found < 0) found = source().indexOf(needle);
  if (found >= 0) setSelection(found, found + needle.size());
}

qsizetype VirtualSourceEdit::findText(QStringView text, qsizetype from) const {
  return source().indexOf(text, from);
}

qsizetype VirtualSourceEdit::findTextBackward(QStringView text, qsizetype from) const {
  return source().lastIndexOf(text, from);
}

int VirtualSourceEdit::baseLineHeight() const {
  return qMax(14, static_cast<int>(std::ceil(QFontMetricsF(sourceFont_).lineSpacing() * kLineSpacingScale)));
}

int VirtualSourceEdit::textAreaWidth() const {
  return qMax(40, viewport()->width() - kGutterWidth - 2 * kTextInset);
}

std::shared_ptr<VirtualSourceEdit::LineLayout> VirtualSourceEdit::buildLineLayout(int zeroBasedLine) const {
  const auto cached = layoutCache_.constFind(zeroBasedLine);
  if (cached != layoutCache_.cend()) {
    layoutLru_.removeAll(zeroBasedLine);
    layoutLru_.append(zeroBasedLine);
    return cached.value();
  }

  auto result = std::make_shared<LineLayout>();
  result->line = zeroBasedLine;
  result->sourceStart = lineStart(zeroBasedLine);
  const qsizetype end = lineEnd(zeroBasedLine);
  result->length = qMax<qsizetype>(0, end - result->sourceStart);
  const int rowHeight = baseLineHeight();
  if (result->length > kLongLineThreshold) {
    result->longLine = true;
    const qreal advance = qMax<qreal>(1.0, QFontMetricsF(sourceFont_).horizontalAdvance(QLatin1Char('M')));
    if (wordWrap_) {
      const qsizetype columns = qMax<qsizetype>(1, static_cast<qsizetype>(textAreaWidth() / advance));
      const qint64 rows = qMax<qint64>(1, (result->length + columns - 1) / columns);
      result->height = boundedScrollRange(rows * rowHeight);
      result->width = textAreaWidth();
    } else {
      result->height = rowHeight;
      result->width = qMin<qreal>(std::numeric_limits<int>::max(), result->length * advance);
    }
    layoutCache_.insert(zeroBasedLine, result);
    layoutLru_.append(zeroBasedLine);
    if (layoutLru_.size() > kMaxCachedLayouts) layoutCache_.remove(layoutLru_.takeFirst());
    return result;
  }

  result->text = source().mid(result->sourceStart, result->length);
  result->layout = std::make_unique<QTextLayout>(result->text, sourceFont_);
  QTextOption option;
  option.setWrapMode(wordWrap_ ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
  option.setTabStopDistance(QFontMetricsF(sourceFont_).horizontalAdvance(QLatin1Char(' ')) * 4.0);
  result->layout->setTextOption(option);
  result->layout->setFormats(sourceFormats(result->text, colors_, sourceFont_));
  result->layout->beginLayout();
  qreal y = 0.0;
  qreal maxWidth = 0.0;
  while (true) {
    QTextLine line = result->layout->createLine();
    if (!line.isValid()) break;
    line.setLineWidth(wordWrap_ ? textAreaWidth() : 1000000000.0);
    line.setPosition(QPointF(0.0, y));
    y += qMax<qreal>(rowHeight, line.height());
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
  }
  result->layout->endLayout();
  result->height = qMax(rowHeight, static_cast<int>(std::ceil(y)));
  result->width = maxWidth;
  layoutCache_.insert(zeroBasedLine, result);
  layoutLru_.append(zeroBasedLine);
  if (layoutLru_.size() > kMaxCachedLayouts) layoutCache_.remove(layoutLru_.takeFirst());
  return result;
}

int VirtualSourceEdit::measuredLineHeight(int zeroBasedLine) const {
  return buildLineLayout(zeroBasedLine)->height;
}

void VirtualSourceEdit::resetGeometryIndex(bool preserveScroll) {
  const int oldValue = verticalScrollBar()->value();
  invalidateLayoutCache();
  heights_.reset(source().lineCount(), baseLineHeight());
  updateScrollBars();
  if (preserveScroll) verticalScrollBar()->setValue(qMin(oldValue, verticalScrollBar()->maximum()));
  viewport()->update();
}

void VirtualSourceEdit::invalidateLayoutCache() {
  layoutCache_.clear();
  layoutLru_.clear();
}

void VirtualSourceEdit::updateScrollBars() {
  verticalScrollBar()->setSingleStep(baseLineHeight());
  verticalScrollBar()->setPageStep(qMax(baseLineHeight(), viewport()->height()));
  verticalScrollBar()->setRange(0, boundedScrollRange(heights_.totalHeight() - viewport()->height()));
  if (wordWrap_) {
    horizontalScrollBar()->setRange(0, 0);
  } else {
    horizontalScrollBar()->setPageStep(qMax(1, textAreaWidth()));
  }
}

void VirtualSourceEdit::applyScrollBarStyle() {
  const QString background = colors_.background.name(QColor::HexRgb);
  setStyleSheet(QStringLiteral(
      "QScrollBar:vertical { background:%1; width:8px; margin:0; }"
      "QScrollBar::handle:vertical { background:#b7b7b7; min-height:54px; border-radius:3px; margin:1px 2px; }"
      "QScrollBar::handle:vertical:hover { background:#999999; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; border:0; background:transparent; }"
      "QScrollBar:add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
      "QScrollBar:horizontal { background:%1; height:8px; margin:0; }"
      "QScrollBar::handle:horizontal { background:#b7b7b7; min-width:54px; border-radius:3px; margin:2px 1px; }"
      "QScrollBar::handle:horizontal:hover { background:#999999; }"
      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; border:0; background:transparent; }"
      "QScrollBar:add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }")
      .arg(background));
}

void VirtualSourceEdit::paintEvent(QPaintEvent* event) {
  QPainter painter(viewport());
  painter.fillRect(event->rect(), colors_.background);
  painter.fillRect(QRect(0, event->rect().top(), kGutterWidth, event->rect().height()), colors_.gutterBackground);

  if (source().isEmpty() && !placeholder_.isEmpty()) {
    painter.setFont(sourceFont_);
    painter.setPen(colors_.lineNumber);
    painter.drawText(QPointF(kGutterWidth + kTextInset, baseLineHeight()), placeholder_);
    return;
  }

  const qint64 scrollY = verticalScrollBar()->value();
  int line = static_cast<int>(heights_.lineAtY(scrollY));
  qreal y = static_cast<qreal>(heights_.yForLine(line) - scrollY);
  const int currentLine = lineForOffset(cursor_);
  const qsizetype selStart = selectionStart();
  const qsizetype selEnd = selectionEnd();
  qreal widest = horizontalScrollBar()->maximum() + textAreaWidth();

  while (line < source().lineCount() && y < viewport()->height()) {
    const std::shared_ptr<LineLayout> lineLayout = buildLineLayout(line);
    heights_.setHeight(line, lineLayout->height);
    widest = qMax(widest, lineLayout->width);
    if (line == currentLine) {
      painter.fillRect(QRectF(kGutterWidth, y, viewport()->width() - kGutterWidth, lineLayout->height), colors_.currentLine);
    }

    painter.setFont(lineNumberFont_);
    painter.setPen(colors_.lineNumber);
    painter.drawText(QRectF(0, y, kGutterWidth - 10, baseLineHeight()),
                     Qt::AlignRight | Qt::AlignVCenter, QString::number(line + 1));

    const qreal originX = kGutterWidth + kTextInset - horizontalScrollBar()->value();
    if (lineLayout->longLine) {
      const qreal advance = qMax<qreal>(1.0, QFontMetricsF(sourceFont_).horizontalAdvance(QLatin1Char('M')));
      const int rowHeight = baseLineHeight();
      const qsizetype columns = wordWrap_
          ? qMax<qsizetype>(1, static_cast<qsizetype>(textAreaWidth() / advance))
          : qMax<qsizetype>(1, static_cast<qsizetype>(viewport()->width() / advance) + 4);
      const int firstRow = wordWrap_ ? qMax(0, static_cast<int>(std::floor(-y / rowHeight))) : 0;
      const int lastRow = wordWrap_
          ? qMin<int>(static_cast<int>((lineLayout->length + columns - 1) / columns),
                      static_cast<int>(std::ceil((viewport()->height() - y) / rowHeight)) + 1)
          : 1;
      painter.setFont(sourceFont_);
      painter.setPen(colors_.text);
      for (int row = firstRow; row < lastRow; ++row) {
        qsizetype chunkStart = wordWrap_ ? static_cast<qsizetype>(row) * columns
                                         : static_cast<qsizetype>(horizontalScrollBar()->value() / advance);
        const qsizetype chunkLength = qMin(columns, lineLayout->length - chunkStart);
        if (chunkLength <= 0) break;
        const QString chunk = source().mid(lineLayout->sourceStart + chunkStart, chunkLength);
        const qreal rowY = y + row * rowHeight;
        const qreal chunkX = wordWrap_
            ? originX
            : kGutterWidth + kTextInset - std::fmod(horizontalScrollBar()->value(), advance);
        const qsizetype absoluteStart = lineLayout->sourceStart + chunkStart;
        const qsizetype selectedStart = qMax(selStart, absoluteStart);
        const qsizetype selectedEnd = qMin(selEnd, absoluteStart + chunkLength);
        if (selectedEnd > selectedStart) {
          const qreal sx = chunkX + (selectedStart - absoluteStart) * advance;
          painter.fillRect(QRectF(sx, rowY, (selectedEnd - selectedStart) * advance, rowHeight), colors_.selection);
        }
        painter.drawText(QPointF(chunkX, rowY + QFontMetricsF(sourceFont_).ascent() +
                                 (rowHeight - QFontMetricsF(sourceFont_).height()) / 2.0), chunk);
      }
    } else {
      QVector<QTextLayout::FormatRange> selections;
      const qsizetype localStart = qMax<qsizetype>(0, selStart - lineLayout->sourceStart);
      const qsizetype localEnd = qMin<qsizetype>(lineLayout->length, selEnd - lineLayout->sourceStart);
      if (localEnd > localStart) {
        QTextCharFormat selected;
        selected.setBackground(colors_.selection);
        selections.push_back({static_cast<int>(localStart), static_cast<int>(localEnd - localStart), selected});
      }
      painter.setPen(colors_.text);
      lineLayout->layout->draw(&painter, QPointF(originX, y), selections);
    }

    y += lineLayout->height;
    ++line;
  }

  if (!wordWrap_) {
    horizontalScrollBar()->setRange(0, boundedScrollRange(widest - textAreaWidth()));
  }
  updateScrollBars();

  if (hasFocus() && cursorVisible_) {
    const QRect cursorRect = cursorRectForOffset(cursor_);
    painter.fillRect(QRect(cursorRect.left(), cursorRect.top(), 2, cursorRect.height()), colors_.text);
    if (!preedit_.isEmpty()) {
      painter.setFont(sourceFont_);
      painter.setPen(colors_.text);
      painter.drawText(cursorRect.bottomLeft(), preedit_);
    }
  }
}

QPoint VirtualSourceEdit::contentPoint(const QPoint& viewportPoint) const {
  return QPoint(viewportPoint.x() + horizontalScrollBar()->value(),
                viewportPoint.y() + verticalScrollBar()->value());
}

qsizetype VirtualSourceEdit::offsetForPoint(const QPoint& viewportPoint) const {
  const qint64 absoluteY = viewportPoint.y() + verticalScrollBar()->value();
  const int line = static_cast<int>(heights_.lineAtY(absoluteY));
  const qint64 lineY = heights_.yForLine(line);
  const std::shared_ptr<LineLayout> layout = buildLineLayout(line);
  const qreal x = viewportPoint.x() - kGutterWidth - kTextInset + horizontalScrollBar()->value();
  const qreal localY = absoluteY - lineY;
  qsizetype local = 0;
  if (layout->longLine) {
    const qreal advance = qMax<qreal>(1.0, QFontMetricsF(sourceFont_).horizontalAdvance(QLatin1Char('M')));
    const qsizetype columns = wordWrap_
        ? qMax<qsizetype>(1, static_cast<qsizetype>(textAreaWidth() / advance))
        : layout->length;
    const qsizetype row = wordWrap_ ? qMax<qsizetype>(0, static_cast<qsizetype>(localY / baseLineHeight())) : 0;
    local = row * columns + qMax<qsizetype>(0, static_cast<qsizetype>(std::round(x / advance)));
  } else if (layout->layout) {
    QTextLine textLine = layout->layout->lineAt(0);
    for (int i = 0; i < layout->layout->lineCount(); ++i) {
      const QTextLine candidate = layout->layout->lineAt(i);
      if (localY >= candidate.y() && localY < candidate.y() + qMax<qreal>(baseLineHeight(), candidate.height())) {
        textLine = candidate;
        break;
      }
    }
    local = textLine.xToCursor(qMax<qreal>(0.0, x));
  }
  return qMin(layout->sourceStart + qMin(local, layout->length), lineEnd(line));
}

QRect VirtualSourceEdit::cursorRectForOffset(qsizetype offset) const {
  offset = boundedOffset(offset);
  const int line = lineForOffset(offset);
  const qsizetype start = lineStart(line);
  const qsizetype local = offset - start;
  const std::shared_ptr<LineLayout> layout = buildLineLayout(line);
  qreal x = kGutterWidth + kTextInset - horizontalScrollBar()->value();
  qreal y = heights_.yForLine(line) - verticalScrollBar()->value();
  int height = baseLineHeight();
  if (layout->longLine) {
    const qreal advance = qMax<qreal>(1.0, QFontMetricsF(sourceFont_).horizontalAdvance(QLatin1Char('M')));
    const qsizetype columns = wordWrap_
        ? qMax<qsizetype>(1, static_cast<qsizetype>(textAreaWidth() / advance))
        : qMax<qsizetype>(1, layout->length + 1);
    const qsizetype row = wordWrap_ ? local / columns : 0;
    const qsizetype column = wordWrap_ ? local % columns : local;
    x += column * advance;
    y += row * baseLineHeight();
  } else if (layout->layout) {
    QTextLine textLine = layout->layout->lineForTextPosition(static_cast<int>(local));
    if (!textLine.isValid() && layout->layout->lineCount() > 0) textLine = layout->layout->lineAt(layout->layout->lineCount() - 1);
    if (textLine.isValid()) {
      x += textLine.cursorToX(static_cast<int>(local));
      y += textLine.y();
      height = qMax(height, static_cast<int>(std::ceil(textLine.height())));
    }
  }
  return QRect(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)), 2, height);
}

void VirtualSourceEdit::moveCursorTo(qsizetype position, bool keepAnchor) {
  cursor_ = boundedOffset(position);
  if (!keepAnchor) anchor_ = cursor_;
  preferredColumn_ = -1;
  resetCursorBlink();
  emitCursorPosition();
  viewport()->update();
}

void VirtualSourceEdit::ensureCursorVisible() {
  const QRect rect = cursorRectForOffset(cursor_);
  int vertical = verticalScrollBar()->value();
  if (rect.top() < 0) vertical += rect.top() - baseLineHeight();
  else if (rect.bottom() > viewport()->height()) vertical += rect.bottom() - viewport()->height() + baseLineHeight();
  verticalScrollBar()->setValue(vertical);
  if (!wordWrap_) {
    int horizontal = horizontalScrollBar()->value();
    const int left = kGutterWidth + kTextInset;
    if (rect.left() < left) horizontal += rect.left() - left - kTextInset;
    else if (rect.right() > viewport()->width()) horizontal += rect.right() - viewport()->width() + kTextInset;
    horizontalScrollBar()->setValue(horizontal);
  }
}

void VirtualSourceEdit::centerCursor() {
  const qint64 lineY = heights_.yForLine(lineForOffset(cursor_));
  verticalScrollBar()->setValue(boundedScrollRange(lineY - viewport()->height() / 2));
  ensureCursorVisible();
}

int VirtualSourceEdit::cursorLine() const { return lineForOffset(cursor_) + 1; }
int VirtualSourceEdit::cursorColumn() const { return static_cast<int>(cursor_ - lineStart(lineForOffset(cursor_)) + 1); }

void VirtualSourceEdit::setWordWrapEnabled(bool enabled) {
  if (wordWrap_ == enabled) return;
  wordWrap_ = enabled;
  setHorizontalScrollBarPolicy(enabled ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
  horizontalScrollBar()->setValue(0);
  resetGeometryIndex(true);
  ensureCursorVisible();
}

bool VirtualSourceEdit::wordWrapEnabled() const { return wordWrap_; }

void VirtualSourceEdit::setSourceFont(QFont font) {
  sourceFont_ = std::move(font);
  sourceFont_.setStyleHint(QFont::Monospace);
  sourceFont_.setFixedPitch(true);
  lineNumberFont_ = sourceFont_;
  lineNumberFont_.setPointSizeF(qMax(8.0, sourceFont_.pointSizeF() * 10.0 / 13.0));
  resetGeometryIndex(true);
}

void VirtualSourceEdit::setColors(SourceEditorColors colors) {
  colors_ = std::move(colors);
  applyScrollBarStyle();
  invalidateLayoutCache();
  viewport()->update();
}

void VirtualSourceEdit::setPlaceholderText(QString text) {
  placeholder_ = std::move(text);
  viewport()->update();
}

void VirtualSourceEdit::setDocumentPath(QString path) { documentPath_ = std::move(path); }

void VirtualSourceEdit::setReadOnly(bool readOnly) {
  if (readOnly_ == readOnly) return;
  readOnly_ = readOnly;
  if (readOnly_) preedit_.clear();
  viewport()->update();
}

bool VirtualSourceEdit::isReadOnly() const { return readOnly_; }

void VirtualSourceEdit::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  resetGeometryIndex(true);
  ensureCursorVisible();
}

void VirtualSourceEdit::keyPressEvent(QKeyEvent* event) {
  const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
  const bool control = event->modifiers().testFlag(Qt::ControlModifier);
  const bool command = event->modifiers().testFlag(Qt::MetaModifier);
  const bool wordNavigation = control || command;
  if (event->matches(QKeySequence::Copy)) {
    if (hasSelection()) QApplication::clipboard()->setText(selectedText());
  } else if (event->matches(QKeySequence::Cut)) {
    if (!readOnly_ && hasSelection()) {
      QApplication::clipboard()->setText(selectedText());
      applyEdit(selectionStart(), selectionEnd(), {}, true);
    }
  } else if (event->matches(QKeySequence::Paste)) {
    if (!readOnly_) insertText(QApplication::clipboard()->text());
  } else if (event->matches(QKeySequence::Undo)) {
    undo();
  } else if (event->matches(QKeySequence::Redo)) {
    redo();
  } else if (event->matches(QKeySequence::SelectAll)) {
    selectAll();
  } else {
    switch (event->key()) {
      case Qt::Key_Left: {
        qsizetype next = cursor_;
        if (!shift && hasSelection()) next = selectionStart();
        else next = wordNavigation ? previousWordOffset(next) : previousCharacterOffset(next);
        moveCursorTo(next, shift);
        break;
      }
      case Qt::Key_Right: {
        qsizetype next = cursor_;
        if (!shift && hasSelection()) next = selectionEnd();
        else next = wordNavigation ? nextWordOffset(next) : nextCharacterOffset(next);
        moveCursorTo(next, shift);
        break;
      }
      case Qt::Key_Up:
      case Qt::Key_Down: {
        const int direction = event->key() == Qt::Key_Up ? -1 : 1;
        const int column = preferredColumn_ >= 0 ? preferredColumn_ : cursorColumn() - 1;
        const int targetLine = qBound(0, lineForOffset(cursor_) + direction, source().lineCount() - 1);
        moveCursorTo(qMin(lineStart(targetLine) + column, lineEnd(targetLine)), shift);
        preferredColumn_ = column;
        break;
      }
      case Qt::Key_Home:
        moveCursorTo(control ? 0 : lineStart(lineForOffset(cursor_)), shift);
        break;
      case Qt::Key_End:
        moveCursorTo(control ? source().size() : lineEnd(lineForOffset(cursor_)), shift);
        break;
      case Qt::Key_PageUp:
      case Qt::Key_PageDown: {
        const int lines = qMax(1, viewport()->height() / baseLineHeight());
        const int direction = event->key() == Qt::Key_PageUp ? -lines : lines;
        const int targetLine = qBound(0, lineForOffset(cursor_) + direction, source().lineCount() - 1);
        moveCursorTo(qMin(lineStart(targetLine) + cursorColumn() - 1, lineEnd(targetLine)), shift);
        break;
      }
      case Qt::Key_Backspace:
        if (wordNavigation && !hasSelection()) {
          applyEdit(previousWordOffset(cursor_), cursor_, {}, true);
        } else {
          deleteBackward();
        }
        break;
      case Qt::Key_Delete:
        if (wordNavigation && !hasSelection()) {
          applyEdit(cursor_, nextWordOffset(cursor_), {}, true);
        } else {
          deleteForward();
        }
        break;
      case Qt::Key_Return:
      case Qt::Key_Enter: insertText(QStringLiteral("\n")); break;
      case Qt::Key_Tab: insertText(QStringLiteral("\t")); break;
      default:
        if (!control && !command && !event->modifiers().testFlag(Qt::AltModifier) && !event->text().isEmpty()) {
          insertText(event->text());
        } else {
          QAbstractScrollArea::keyPressEvent(event);
          return;
        }
    }
    ensureCursorVisible();
  }
  event->accept();
}

void VirtualSourceEdit::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) return;
  setFocus(Qt::MouseFocusReason);
  moveCursorTo(offsetForPoint(event->position().toPoint()), event->modifiers().testFlag(Qt::ShiftModifier));
  draggingSelection_ = true;
  event->accept();
}

void VirtualSourceEdit::mouseMoveEvent(QMouseEvent* event) {
  viewport()->setCursor(event->position().x() < kGutterWidth
                            ? Qt::ArrowCursor
                            : Qt::IBeamCursor);
  if (!draggingSelection_) return;
  moveCursorTo(offsetForPoint(event->position().toPoint()), true);
  ensureCursorVisible();
}

void VirtualSourceEdit::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) draggingSelection_ = false;
}

void VirtualSourceEdit::mouseDoubleClickEvent(QMouseEvent* event) {
  moveCursorTo(offsetForPoint(event->position().toPoint()), false);
  selectWord();
}

void VirtualSourceEdit::focusInEvent(QFocusEvent* event) {
  QAbstractScrollArea::focusInEvent(event);
  resetCursorBlink();
}

void VirtualSourceEdit::focusOutEvent(QFocusEvent* event) {
  cursorTimer_->stop();
  cursorVisible_ = false;
  viewport()->update();
  QAbstractScrollArea::focusOutEvent(event);
}

void VirtualSourceEdit::resetCursorBlink() {
  cursorVisible_ = true;
  if (hasFocus() && QApplication::cursorFlashTime() > 0) cursorTimer_->start();
}

void VirtualSourceEdit::inputMethodEvent(QInputMethodEvent* event) {
  if (readOnly_) {
    event->ignore();
    return;
  }
  if (!event->commitString().isEmpty()) insertText(event->commitString());
  preedit_ = event->preeditString();
  viewport()->update();
  event->accept();
}

QVariant VirtualSourceEdit::inputMethodQuery(Qt::InputMethodQuery query) const {
  switch (query) {
    case Qt::ImCursorRectangle: return cursorRectForOffset(cursor_);
    case Qt::ImCursorPosition: return static_cast<int>(cursor_);
    case Qt::ImAnchorPosition: return static_cast<int>(anchor_);
    case Qt::ImAbsolutePosition: return static_cast<int>(cursor_);
    case Qt::ImCurrentSelection: return selectedText();
    case Qt::ImSurroundingText: return lineText(lineForOffset(cursor_));
    default: return QAbstractScrollArea::inputMethodQuery(query);
  }
}

void VirtualSourceEdit::contextMenuEvent(QContextMenuEvent* event) {
  const qsizetype clicked = offsetForPoint(event->pos());
  if (!hasSelection() || clicked < selectionStart() || clicked > selectionEnd()) {
    moveCursorTo(clicked, false);
  }

  QMenu menu(this);
  SpellChecker& checker = SpellChecker::instance();
  const auto [wordStart, wordEnd] = wordRangeAt(clicked);
  const QString word = source().mid(wordStart, wordEnd - wordStart);
  if (checker.isEnabled() && !word.isEmpty() && !checker.isCorrect(word)) {
    const QStringList suggestions = checker.suggestions(word);
    if (suggestions.isEmpty()) {
      QAction* none = menu.addAction(QCoreApplication::translate(
          "muffin::EditorView", "(no spelling suggestions)"));
      none->setEnabled(false);
    } else {
      for (int i = 0; i < qMin(8, suggestions.size()); ++i) {
        const QString suggestion = suggestions.at(i);
        QAction* replacement = menu.addAction(suggestion, this, [this, wordStart, wordEnd, suggestion] {
          applyEdit(wordStart, wordEnd, suggestion, true);
        });
        replacement->setEnabled(!readOnly_);
      }
    }
    menu.addAction(QCoreApplication::translate(
        "muffin::EditorView", "Ignore \"%1\"").arg(word), this, [this, word] {
      SpellChecker::instance().ignoreWord(word);
      invalidateLayoutCache();
      viewport()->update();
    });
    menu.addSeparator();
  }

  QAction* undoAction = menu.addAction(QCoreApplication::translate("muffin::VirtualSourceEdit", "Undo"), this, &VirtualSourceEdit::undo);
  QAction* redoAction = menu.addAction(QCoreApplication::translate("muffin::VirtualSourceEdit", "Redo"), this, &VirtualSourceEdit::redo);
  undoAction->setEnabled(canUndo());
  redoAction->setEnabled(canRedo());
  menu.addSeparator();
  QAction* cutAction = menu.addAction(QCoreApplication::translate("muffin::VirtualSourceEdit", "Cut"), this, [this] {
    if (hasSelection()) {
      QApplication::clipboard()->setText(selectedText());
      applyEdit(selectionStart(), selectionEnd(), {}, true);
    }
  });
  QAction* copyAction = menu.addAction(QCoreApplication::translate("muffin::VirtualSourceEdit", "Copy"), this, [this] {
    if (hasSelection()) QApplication::clipboard()->setText(selectedText());
  });
  QAction* pasteAction = menu.addAction(QCoreApplication::translate("muffin::VirtualSourceEdit", "Paste"), this, [this] {
    insertText(QApplication::clipboard()->text());
  });
  cutAction->setEnabled(hasSelection());
  copyAction->setEnabled(hasSelection());
  if (readOnly_) {
    undoAction->setEnabled(false);
    redoAction->setEnabled(false);
    cutAction->setEnabled(false);
    pasteAction->setEnabled(false);
  }
  menu.addSeparator();
  menu.addAction(QCoreApplication::translate("muffin::VirtualSourceEdit", "Select All"), this, &VirtualSourceEdit::selectAll);
  menu.exec(event->globalPos());
}

void VirtualSourceEdit::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasText() || event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void VirtualSourceEdit::dropEvent(QDropEvent* event) {
  moveCursorTo(offsetForPoint(event->position().toPoint()), false);
  const QMimeData* mime = event->mimeData();
  if (mime->hasFormat(kMuffinFileTreeDragMime) && mime->hasUrls()) {
    const QUrl url = mime->urls().constFirst();
    if (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isDir()) {
      insertText(FilePathOps::markdownLinkForFile(url.toLocalFile(), documentPath_));
      event->acceptProposedAction();
      return;
    }
  }
  if (mime->hasText()) {
    insertText(mime->text());
    event->acceptProposedAction();
  }
}

void VirtualSourceEdit::emitCursorPosition() {
  emit cursorPositionChanged(cursorLine(), cursorColumn());
}

void VirtualSourceEdit::scrollContentsBy(int dx, int dy) {
  Q_UNUSED(dx);
  Q_UNUSED(dy);
  viewport()->update();
}

}  // namespace muffin

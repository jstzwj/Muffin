#include "app/StatusBarWidget.h"

#include "app/UiMetrics.h"

#include "spellcheck/SpellChecker.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QToolButton>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QVector>

namespace {

enum class IconKind { SidebarExpand, SidebarCollapse, SourceMode };

// Load a status-bar SVG, recolor it to `ink`, and rasterize it as a multi-size
// QIcon. Mirrors the free function that used to live in MainWindow.cpp.
QIcon makeStatusIcon(IconKind kind, const QColor& ink) {
  const char* path = nullptr;
  switch (kind) {
    case IconKind::SidebarExpand: path = ":/icons/statusbar/sidebar-expand.svg"; break;
    case IconKind::SidebarCollapse: path = ":/icons/statusbar/sidebar-collapse.svg"; break;
    case IconKind::SourceMode: path = ":/icons/statusbar/source-code.svg"; break;
  }
  QFile svgFile(QString::fromLatin1(path));
  if (!svgFile.open(QIODevice::ReadOnly)) {
    return {};
  }
  QByteArray svgData = svgFile.readAll();
  svgFile.close();
  svgData.replace("#000000", ink.name(QColor::HexRgb).toUtf8());

  QSvgRenderer renderer(svgData);
  if (!renderer.isValid()) {
    return {};
  }
  const qreal dpr = qApp ? qApp->devicePixelRatio() : qreal(1.0);
  QIcon icon;
  for (const int sz : {16, 24, 32}) {
    QPixmap px(static_cast<int>(sz * dpr), static_cast<int>(sz * dpr));
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    // Explicit logical target rect: render(&p) without bounds paints into the painter's
    // viewport, which is the *physical* size (sz*dpr) on a high-DPI pixmap — so the SVG would
    // rasterize at dpr× scale and only its top-left corner would fit. Pass the logical sz×sz so
    // the painter's ×dpr transform maps it exactly onto the physical backing store.
    renderer.render(&p, QRectF(0, 0, sz, sz));
    icon.addPixmap(px);
  }
  return icon;
}

// A small upward popup showing the document statistics. Painted (not a real
// widget tree) after TableResizePopup's pattern: QFrame + Qt::Popup + paintEvent.
class StatsPopup final : public QFrame {
public:
  struct Row { QString label; QString value; };
  StatsPopup(const QVector<Row>& rows, const QColor& bg, const QColor& text, const QColor& muted,
             const QColor& border, QWidget* parent = nullptr)
      : QFrame(parent, Qt::Popup), rows_(rows), bg_(bg), text_(text), muted_(muted), border_(border) {
    setFrameShape(QFrame::NoFrame);
    QFont labelFont = font();
    labelFont.setPointSizeF(labelFont.pointSizeF() > 0 ? labelFont.pointSizeF() : 9.0);
    setFont(labelFont);
    const QFontMetrics fm(labelFont);
    int maxLabel = 0;
    int maxValue = 0;
    for (const auto& r : rows) {
      maxLabel = qMax(maxLabel, fm.horizontalAdvance(r.label));
      maxValue = qMax(maxValue, fm.horizontalAdvance(r.value));
    }
    constexpr int kPadX = 18;
    constexpr int kPadY = 12;
    constexpr int kGap = 36;
    constexpr int kRowH = 22;
    setFixedSize(maxLabel + kGap + maxValue + kPadX * 2, qMax(1, rows.size()) * kRowH + kPadY * 2);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), bg_);
    p.setPen(QPen(border_, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 6, 6);

    constexpr int kPadX = 18;
    constexpr int kPadY = 12;
    constexpr int kRowH = 22;
    const QFontMetrics fm(font());
    const int valueWidth = [this, &fm]() {
      int w = 0;
      for (const auto& r : rows_) w = qMax(w, fm.horizontalAdvance(r.value));
      return w;
    }();
    int y = kPadY;
    for (const auto& r : rows_) {
      const QRect labelRect(kPadX, y, width() - kPadX * 2 - valueWidth - 24, kRowH);
      const QRect valueRect(width() - kPadX - valueWidth, y, valueWidth, kRowH);
      p.setPen(muted_);
      p.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, r.label);
      p.setPen(text_);
      p.drawText(valueRect, Qt::AlignVCenter | Qt::AlignRight, r.value);
      y += kRowH;
    }
  }

private:
  QVector<Row> rows_;
  QColor bg_, text_, muted_, border_;
};

// Draw a tiny downward chevron centered at (cx, cy).
void drawChevron(QPainter& p, int cx, int cy, const QColor& color) {
  p.setPen(QPen(color, 1.4));
  p.setBrush(Qt::NoBrush);
  QPainterPath pp;
  pp.moveTo(cx - 3, cy - 1.5);
  pp.lineTo(cx, cy + 1.5);
  pp.lineTo(cx + 3, cy - 1.5);
  p.drawPath(pp);
}

}  // namespace

muffin::StatusBarWidget::StatusBarWidget(QWidget* parent) : QWidget(parent) {
  setFixedHeight(muffin::ui_metrics::kStatusBarHeight);
  setMouseTracking(true);
  setAttribute(Qt::WA_StyledBackground, false);

  // Match the bumped chrome size (~10pt ≈ 13px @96dpi) and inherit the app
  // font's PreferVerticalHinting so the painted segments anti-alias smoothly.
  QFont barFont = font();
  barFont.setPointSizeF(10.0);
  setFont(barFont);

  sidebarButton_ = new QToolButton(this);
  sidebarButton_->setIconSize(QSize(muffin::ui_metrics::kStatusIconSize, muffin::ui_metrics::kStatusIconSize));
  sidebarButton_->setCheckable(true);
  sidebarButton_->setAutoRaise(true);
  sidebarButton_->setFocusPolicy(Qt::NoFocus);
  sidebarButton_->setCursor(Qt::PointingHandCursor);

  sourceModeButton_ = new QToolButton(this);
  sourceModeButton_->setIconSize(QSize(muffin::ui_metrics::kStatusIconSize, muffin::ui_metrics::kStatusIconSize));
  sourceModeButton_->setCheckable(true);
  sourceModeButton_->setAutoRaise(true);
  sourceModeButton_->setFocusPolicy(Qt::NoFocus);
  sourceModeButton_->setCursor(Qt::PointingHandCursor);

  // The sidebar icon flips between collapse « (sidebar visible) and expand » (hidden) on every
  // toggle, so the button always shows the action the next click will perform.
  connect(sidebarButton_, &QToolButton::toggled, this, [this] { updateSidebarIcon(); });

  // Defaults until applyThemeColors() is called.
  bg_ = QColor(0xff, 0xff, 0xff);
  text_ = QColor(0x11, 0x11, 0x11);
  muted_ = QColor(0x55, 0x55, 0x55);
  border_ = QColor(0xe5, 0xe7, 0xeb);
  spellDisplayLabel_ = QStringLiteral("EN");
}

QToolButton* muffin::StatusBarWidget::sidebarButton() const { return sidebarButton_; }
QToolButton* muffin::StatusBarWidget::sourceModeButton() const { return sourceModeButton_; }

void muffin::StatusBarWidget::setCursorStatus(const QString& text) {
  if (cursorStatus_ != text) {
    cursorStatus_ = text;
    update();
  }
}

void muffin::StatusBarWidget::setWordCount(int words) {
  wordCount_ = words;
  const QString t = tr("%1 words").arg(words);
  if (wordCountText_ != t) {
    wordCountText_ = t;
    update();
  }
}

void muffin::StatusBarWidget::setBlockSource(const QString& text, const QString& toolTip) {
  blockSource_ = text;
  blockSourceToolTip_ = toolTip;
  setToolTip(blockSource_.isEmpty() ? QString() : blockSourceToolTip_);
  update();
}

void muffin::StatusBarWidget::setEncodingLineEnding(const QString& text) {
  if (encodingLineEnding_ != text) {
    encodingLineEnding_ = text;
    update();
  }
}

void muffin::StatusBarWidget::setSpellLanguage(const QString& localeCode, bool enabled) {
  spellLocaleCode_ = localeCode;
  spellEnabled_ = enabled;
  // Compact label: show the language subtag (e.g. "en_US" -> "EN"); falls back to the code.
  QString label = localeCode.section(QLatin1Char('_'), 0, 0).toUpper();
  if (label.isEmpty()) {
    label = localeCode.toUpper();
  }
  spellDisplayLabel_ = label;
  if (spellMenu_) {
    rebuildSpellMenu();
  }
  update();
}

void muffin::StatusBarWidget::setSidebarChecked(bool checked) {
  if (sidebarButton_) {
    sidebarButton_->setChecked(checked);
    updateSidebarIcon();
  }
}

void muffin::StatusBarWidget::setSourceModeChecked(bool checked) {
  if (sourceModeButton_) sourceModeButton_->setChecked(checked);
}

void muffin::StatusBarWidget::updateSidebarIcon() {
  if (!sidebarButton_) {
    return;
  }
  // Sidebar visible (checked) → collapse « (the next click hides it); hidden → expand ».
  const IconKind kind = sidebarButton_->isChecked() ? IconKind::SidebarCollapse : IconKind::SidebarExpand;
  sidebarButton_->setIcon(makeStatusIcon(kind, muted_));
}

void muffin::StatusBarWidget::applyThemeColors(const QColor& bg, const QColor& text, const QColor& muted,
                                               const QColor& border) {
  bg_ = bg;
  text_ = text;
  muted_ = muted;
  border_ = border;

  // Style the two child buttons so they blend with the painted bar.
  const bool dark = bg.lightness() < 128;
  const QColor hover = dark ? bg.lighter(150) : bg.darker(108);
  const QColor checked = dark ? QColor(0x30, 0x36, 0x3d) : QColor(0xe3, 0xe6, 0xea);
  setStyleSheet(QStringLiteral(
      "QToolButton { background: transparent; border: 0; border-radius: 4px;"
      " padding: 0 6px; min-width: 22px; min-height: 20px; }"
      "QToolButton:hover { background: %1; }"
      "QToolButton:checked { background: %2; }").arg(hover.name(QColor::HexRgb), checked.name(QColor::HexRgb)));

  const QColor ink = muted;
  sourceModeButton_->setIcon(makeStatusIcon(IconKind::SourceMode, ink));
  updateSidebarIcon();
  update();
}

muffin::StatusBarWidget::SegmentRects muffin::StatusBarWidget::layoutSegments() const {
  SegmentRects r;
  const QFontMetrics fm(font());
  const int h = height();
  const int gap = muffin::ui_metrics::kStatusGap;
  const int rightPad = 12;
  const int chev = 14;

  // Stats trigger (word count + chevron) — rightmost, clickable.
  int statsW = fm.horizontalAdvance(wordCountText_);
  r.stats = QRect(width() - rightPad - statsW - chev, 0, statsW + chev, h);
  r.rightEdge = r.stats.left() - gap;

  // Spell-language button — second from right, clickable.
  int spellTextW = fm.horizontalAdvance(spellDisplayLabel_);
  r.spell = QRect(r.rightEdge - spellTextW - chev, 0, spellTextW + chev, h);
  r.rightEdge = r.spell.left() - gap;

  // Cursor status.
  int curW = fm.horizontalAdvance(cursorStatus_);
  r.cursor = QRect(r.rightEdge - curW, 0, curW, h);
  r.rightEdge = r.cursor.left() - gap;

  // Encoding·line-ending.
  int encW = fm.horizontalAdvance(encodingLineEnding_);
  r.encoding = QRect(r.rightEdge - encW, 0, encW, h);
  r.rightEdge = r.encoding.left() - gap;

  // Left cluster starts after the two tool buttons (laid out in relayoutButtons).
  r.leftStart = (sourceModeButton_ ? sourceModeButton_->geometry().right() + 8 : 0) + 4;

  // Block-source fills the middle.
  r.blockSource = QRect(r.leftStart, 0, qMax(0, r.rightEdge - r.leftStart - gap), h);
  return r;
}

void muffin::StatusBarWidget::relayoutButtons() {
  if (!sidebarButton_ || !sourceModeButton_) {
    return;
  }
  const int btnW = 30;
  const int btnH = height() - 4;
  int x = 4;
  sidebarButton_->setGeometry(x, 2, btnW, btnH);
  x += btnW;
  sourceModeButton_->setGeometry(x, 2, btnW, btnH);
}

void muffin::StatusBarWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  relayoutButtons();
}

void muffin::StatusBarWidget::drawSegmentText(QPainter& p, const QString& text, const QRect& rect,
                                              const QColor& color, int flags) const {
  p.setPen(color);
  p.drawText(rect, flags, text);
}

bool muffin::StatusBarWidget::event(QEvent* event) {
  // The status bar is a single self-painted widget whose segments are clickable
  // (stats popup, spell-language menu) but expose nothing to Qt's tooltip/accessibility
  // layer — so a hover over "EN" or the word count gives no hint that it's interactive.
  // Intercept the tooltip event and route it per-segment via the same layout test the
  // paint/mouse handlers use.
  if (event->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(event);
    const SegmentRects r = layoutSegments();
    const QPoint pos = he->pos();
    QString tip;
    QRect area;
    if (r.stats.contains(pos)) {
      tip = tr("Click to view document statistics");
      area = r.stats;
    } else if (r.spell.contains(pos)) {
      tip = spellEnabled_ ? tr("Click to change the spell-check language")
                          : tr("Spell check is off — click to enable");
      area = r.spell;
    } else if (r.cursor.contains(pos)) {
      tip = tr("Cursor position (line : column)");
      area = r.cursor;
    } else if (r.encoding.contains(pos)) {
      tip = tr("File encoding · line ending");
      area = r.encoding;
    } else if (!blockSourceToolTip_.isEmpty() && r.blockSource.contains(pos)) {
      tip = blockSourceToolTip_;
      area = r.blockSource;
    }
    if (!tip.isEmpty()) {
      QToolTip::showText(he->globalPos(), tip, this, area);
      return true;
    }
    QToolTip::hideText();
  }
  return QWidget::event(event);
}

void muffin::StatusBarWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  p.setFont(font());

  // Background + 1px top separator.
  p.fillRect(rect(), bg_);
  p.setPen(QPen(border_, 1));
  p.drawLine(QPointF(0, 0.5), QPointF(width(), 0.5));

  const SegmentRects r = layoutSegments();
  const int cy = height() / 2;

  // Encoding·line-ending (muted, display only).
  if (!encodingLineEnding_.isEmpty()) {
    drawSegmentText(p, encodingLineEnding_, r.encoding, muted_);
  }

  // Stats trigger (clickable): word count + chevron, highlighted on hover.
  const QColor statsColor = (hover_ == Hover::Stats) ? text_ : muted_;
  drawSegmentText(p, wordCountText_, r.stats.adjusted(0, 0, -14, 0), statsColor);
  drawChevron(p, r.stats.right() - 7, cy, statsColor);

  // Cursor status (muted).
  drawSegmentText(p, cursorStatus_, r.cursor, muted_);

  // Spell-language button (clickable): label + chevron.
  const QColor spellColor = spellEnabled_ ? ((hover_ == Hover::Spell) ? text_ : muted_) : muted_.lighter(135);
  drawSegmentText(p, spellDisplayLabel_, r.spell.adjusted(0, 0, -14, 0), spellColor);
  drawChevron(p, r.spell.right() - 7, cy, spellColor);

  // Block-source preview (monospace-ish, muted), elided to the middle area.
  if (!blockSource_.isEmpty() && r.blockSource.width() > 24) {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(font().pointSizeF());
    p.setFont(mono);
    const QFontMetrics mfm(mono);
    const QString elided = mfm.elidedText(blockSource_, Qt::ElideRight, r.blockSource.width());
    p.setPen(muted_);
    p.drawText(r.blockSource, Qt::AlignVCenter | Qt::AlignLeft, elided);
    p.setFont(font());
  }
}

void muffin::StatusBarWidget::mouseMoveEvent(QMouseEvent* event) {
  const QPointF pos = event->position();
  const SegmentRects r = layoutSegments();
  Hover next = Hover::None;
  if (r.stats.contains(pos.toPoint())) {
    next = Hover::Stats;
  } else if (r.spell.contains(pos.toPoint())) {
    next = Hover::Spell;
  }
  if (next != hover_) {
    hover_ = next;
    setCursor(next == Hover::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void muffin::StatusBarWidget::leaveEvent(QEvent* event) {
  if (hover_ != Hover::None) {
    hover_ = Hover::None;
    setCursor(Qt::ArrowCursor);
    update();
  }
  QWidget::leaveEvent(event);
}

void muffin::StatusBarWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  const QPoint pos = event->position().toPoint();
  const SegmentRects r = layoutSegments();
  if (r.stats.contains(pos)) {
    emit statsClicked();
    return;
  }
  if (r.spell.contains(pos)) {
    ensureSpellMenu();
    if (spellMenu_) {
      const QPoint global = mapToGlobal(QPoint(r.spell.left(), 0));
      spellMenu_->exec(QPoint(global.x(), global.y() - spellMenu_->sizeHint().height() - 4));
    }
    return;
  }
  QWidget::mousePressEvent(event);
}

void muffin::StatusBarWidget::ensureSpellMenu() {
  if (!spellMenu_) {
    spellMenu_ = new QMenu(this);
    rebuildSpellMenu();
  }
}

void muffin::StatusBarWidget::rebuildSpellMenu() {
  if (!spellMenu_) {
    return;
  }
  spellMenu_->clear();

  QAction* toggle = spellMenu_->addAction(tr("Spell Check"));
  toggle->setCheckable(true);
  toggle->setChecked(spellEnabled_);
  connect(toggle, &QAction::toggled, this, [](bool on) { SpellChecker::instance().setEnabled(on); });
  spellMenu_->addSeparator();

  const QStringList langs = SpellChecker::instance().availableLanguages();
  for (const QString& code : langs) {
    const QString label = QLocale(code).nativeLanguageName();
    QAction* a = spellMenu_->addAction(label.isEmpty() ? code : label);
    a->setCheckable(true);
    a->setChecked(code == spellLocaleCode_);
    a->setData(code);
    connect(a, &QAction::triggered, this, [code]() { SpellChecker::instance().setLanguage(code); });
  }
}

void muffin::StatusBarWidget::showStatsPopup(const StatusBarStats& stats) {
  QVector<StatsPopup::Row> rows;
  rows.append({tr("Words"), QString::number(stats.words)});
  rows.append({tr("Characters"), QString::number(stats.characters)});
  rows.append({tr("Lines"), QString::number(stats.lines)});
  rows.append({tr("Reading time"), stats.readingMinutes <= 0 ? tr("< 1 min") : tr("%1 min").arg(stats.readingMinutes)});
  if (stats.selectedWords >= 0) {
    rows.append({tr("Selected"), tr("%1 words").arg(stats.selectedWords)});
  }

  auto* popup = new StatsPopup(rows, bg_, text_, muted_, border_, this);
  popup->setAttribute(Qt::WA_DeleteOnClose);

  const SegmentRects r = layoutSegments();
  const QPoint anchor = mapToGlobal(QPoint(r.stats.left() - 8, r.stats.top()));
  int x = anchor.x();
  int y = anchor.y() - popup->height() - 6;
  if (y < 0) {
    y = mapToGlobal(QPoint(0, r.stats.bottom())).y() + 6;  // flip below if no room above
  }
  // Keep on screen horizontally.
  if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos())) {
    const QRect avail = screen->availableGeometry();
    if (x + popup->width() > avail.right()) x = avail.right() - popup->width();
    if (x < avail.left()) x = avail.left();
  }
  popup->move(x, y);
  popup->show();
}

void muffin::StatusBarWidget::retranslateUi() {
  wordCountText_ = tr("%1 words").arg(wordCount_);
  if (spellMenu_) {
    rebuildSpellMenu();
  }
  update();
}

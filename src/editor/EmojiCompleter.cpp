#include "editor/EmojiCompleter.h"

#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QListView>
#include <QScreen>
#include <QStringListModel>
#include <QTextStream>

namespace muffin {

QVector<EmojiEntry> BundledEmojiProvider::matches(const QString& prefix, int max) const {
  load();
  QVector<EmojiEntry> hits;
  for (const EmojiEntry& entry : entries_) {
    if (entry.shortcode.contains(prefix, Qt::CaseInsensitive)) {
      hits.append(entry);
    }
  }
  // Shorter shortcodes first (":smile:" before ":smiley_cat:"), then alphabetical, so the most
  // common emojis surface at the top of the popup.
  std::sort(hits.begin(), hits.end(), [](const EmojiEntry& a, const EmojiEntry& b) {
    if (a.shortcode.size() != b.shortcode.size()) {
      return a.shortcode.size() < b.shortcode.size();
    }
    return a.shortcode.compare(b.shortcode, Qt::CaseInsensitive) < 0;
  });
  if (hits.size() > max) {
    hits.resize(max);
  }
  return hits;
}

void BundledEmojiProvider::load() const {
  if (loaded_) {
    return;
  }
  loaded_ = true;
  QFile file(QStringLiteral(":/emoji/emoji.txt"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  QTextStream in(&file);
  in.setGenerateByteOrderMark(false);
  QString line;
  while (in.readLineInto(&line)) {
    const qsizetype tab = line.indexOf(QLatin1Char('\t'));
    if (tab <= 0 || tab + 1 >= line.size()) {
      continue;
    }
    EmojiEntry entry;
    entry.shortcode = line.left(tab).trimmed();
    entry.glyph = line.mid(tab + 1).trimmed();
    if (!entry.shortcode.isEmpty() && !entry.glyph.isEmpty()) {
      entries_.append(entry);
    }
  }
}

EmojiCompleter::EmojiCompleter(QWidget* viewport, const EmojiProvider* provider, QObject* parent)
    : QObject(parent), viewport_(viewport), provider_(provider) {}

void EmojiCompleter::ensurePopup() {
  if (popup_) {
    return;
  }
  popup_ = new QListView(viewport_);
  popup_->setObjectName(QStringLiteral("emojiPopup"));
  popup_->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
  popup_->setUniformItemSizes(true);
  popup_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  popup_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  popup_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  popup_->setFocusPolicy(Qt::NoFocus);
  popup_->setStyleSheet(QStringLiteral(
      "QListView#emojiPopup {"
      "  background:#ffffff;"
      "  color:#333333;"
      "  border:1px solid #e1e4e8;"
      "  border-radius:6px;"
      "  padding:4px 0;"
      "  outline:0;"
      "  font-size:13px;"
      "}"
      "QListView#emojiPopup::item {"
      "  min-height:26px;"
      "  padding:3px 12px;"
      "}"
      "QListView#emojiPopup::item:selected {"
      "  background:#f1f6ff;"
      "  color:#111111;"
      "}"));
  auto* shadow = new QGraphicsDropShadowEffect(popup_);
  shadow->setBlurRadius(14.0);
  shadow->setOffset(0.0, 3.0);
  shadow->setColor(QColor(15, 23, 42, 35));
  popup_->setGraphicsEffect(shadow);

  model_ = new QStringListModel(popup_);
  popup_->setModel(model_);
  popup_->hide();
}

void EmojiCompleter::present(const QString& prefix, const QPoint& caretViewportPos) {
  if (!provider_) {
    hide();
    return;
  }
  ensurePopup();
  if (!popup_ || !viewport_) {
    hide();
    return;
  }

  entries_ = provider_->matches(prefix, 50);
  if (entries_.isEmpty()) {
    hide();
    return;
  }

  QStringList labels;
  labels.reserve(entries_.size());
  for (const EmojiEntry& entry : entries_) {
    labels.push_back(QStringLiteral("%1    :%2:").arg(entry.glyph, entry.shortcode));
  }
  model_->setStringList(labels);
  popup_->setCurrentIndex(model_->index(0));

  const int rowHeight = 26 + 6;  // min-height 26 + padding 3*2
  const int width = 240;
  const int visibleRows = qMin(entries_.size(), 8);
  popup_->setFixedSize(width, qMax(1, visibleRows) * rowHeight + 8);

  // The popup is a top-level window (Qt::ToolTip), so move() takes global screen coordinates —
  // map the caret's viewport position out of the viewport before positioning.
  const QPoint caretGlobal = viewport_->mapToGlobal(caretViewportPos);
  int x = caretGlobal.x();
  int y = caretGlobal.y() + 4;
  const QRect available = viewport_->screen()->availableGeometry();
  if (x + width > available.right()) {
    x = qMax(0, available.right() - width);
  }
  if (y + popup_->height() > available.bottom()) {
    y = qMax(available.top(), caretGlobal.y() - popup_->height() - 4);  // flip above the caret
  }
  popup_->move(qMax(0, x), qMax(0, y));
  popup_->show();
  popup_->raise();
  active_ = true;
}

bool EmojiCompleter::isVisible() const {
  return active_;
}

void EmojiCompleter::hide() {
  active_ = false;
  if (popup_) {
    popup_->hide();
  }
}

void EmojiCompleter::moveSelection(int delta) {
  if (!popup_ || !popup_->isVisible() || !model_) {
    return;
  }
  int row = popup_->currentIndex().row();
  if (row < 0) {
    row = 0;
  }
  row = qBound(0, row + delta, model_->rowCount() - 1);
  popup_->setCurrentIndex(model_->index(row));
}

void EmojiCompleter::acceptCurrent() {
  if (!popup_ || !popup_->isVisible() || !model_) {
    return;
  }
  int row = popup_->currentIndex().row();
  if (row < 0) {
    row = 0;
  }
  if (row < entries_.size()) {
    emit accepted(entries_.at(row).glyph);
  }
  hide();
}

}  // namespace muffin

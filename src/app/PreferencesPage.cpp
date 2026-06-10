#include "app/PreferencesPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

muffin::PreferencesPage::PreferencesPage(QWidget* parent) : QWidget(parent) {
  setStyleSheet(QStringLiteral("background:transparent;"));
}

QLabel* muffin::PreferencesPage::makeSectionLabel(QWidget* parent) const {
  auto* label = new QLabel(parent);
  label->setStyleSheet(QStringLiteral("font-size:13px; font-weight:600; color:#1f2328;"));
  return label;
}

QLabel* muffin::PreferencesPage::makeMutedLabel(QWidget* parent) const {
  auto* label = new QLabel(parent);
  label->setStyleSheet(QStringLiteral("font-size:12px; color:#6e7781;"));
  return label;
}

QPushButton* muffin::PreferencesPage::makeButton(QWidget* parent) const {
  return new QPushButton(parent);
}

QLabel* muffin::PreferencesPage::makeInfoIcon(QWidget* parent) const {
  auto* label = new QLabel(parent);
  const qreal dpr = qApp->devicePixelRatio();
  const int sz = 14;
  QPixmap px(static_cast<int>(sz * dpr), static_cast<int>(sz * dpr));
  px.setDevicePixelRatio(dpr);
  px.fill(Qt::transparent);
  QPainter p(&px);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(QColor(0x8c, 0x95, 0x9f));
  p.setPen(Qt::NoPen);
  p.drawEllipse(0, 0, sz, sz);
  p.setPen(Qt::white);
  QFont f;
  f.setPixelSize(10);
  f.setBold(true);
  p.setFont(f);
  p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, QStringLiteral("?"));
  p.end();
  label->setPixmap(px);
  return label;
}

QWidget* muffin::PreferencesPage::makeCard(QWidget* parent) const {
  auto* card = new QWidget(parent);
  card->setObjectName(QStringLiteral("settingsCard"));
  return card;
}

QVBoxLayout* muffin::PreferencesPage::makeCardLayout(QWidget* card) const {
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(10);
  return layout;
}

void muffin::PreferencesPage::wireBoolSetting(QCheckBox* checkBox, const QString& key) const {
  connect(checkBox, &QCheckBox::toggled, this, [key](bool checked) {
    QSettings().setValue(key, checked);
  });
}

void muffin::PreferencesPage::wireComboIndexSetting(QComboBox* comboBox, const QString& key) const {
  connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [key](int index) {
    QSettings().setValue(key, index);
  });
}

void muffin::PreferencesPage::wireLineEditSetting(QLineEdit* lineEdit, const QString& key) const {
  connect(lineEdit, &QLineEdit::textChanged, this, [key](const QString& text) {
    QSettings().setValue(key, text);
  });
}

void muffin::PreferencesPage::loadCheck(QCheckBox* checkBox, const QString& key, bool defaultValue) const {
  const QSignalBlocker blocker(checkBox);
  checkBox->setChecked(QSettings().value(key, defaultValue).toBool());
}

void muffin::PreferencesPage::loadComboIndex(QComboBox* comboBox, const QString& key, int defaultValue) const {
  if (comboBox->count() <= 0) {
    return;
  }
  const QSignalBlocker blocker(comboBox);
  const int value = QSettings().value(key, defaultValue).toInt();
  comboBox->setCurrentIndex(qBound(0, value, comboBox->count() - 1));
}

void muffin::PreferencesPage::loadLineEdit(QLineEdit* lineEdit, const QString& key, const QString& defaultValue) const {
  const QSignalBlocker blocker(lineEdit);
  lineEdit->setText(QSettings().value(key, defaultValue).toString());
}

void muffin::PreferencesPage::rebuildCombo(QComboBox* comboBox, const QVector<QString>& items) const {
  const int current = comboBox->currentIndex();
  const QSignalBlocker blocker(comboBox);
  comboBox->clear();
  for (const QString& item : items) {
    comboBox->addItem(item);
  }
  polishComboBox(comboBox);
  if (comboBox->count() > 0) {
    comboBox->setCurrentIndex(qBound(0, current, comboBox->count() - 1));
  }
}

void muffin::PreferencesPage::polishComboBox(QComboBox* comboBox, int visibleItems) const {
  if (!comboBox) {
    return;
  }

  auto* view = qobject_cast<QListView*>(comboBox->view());
  if (!view) {
    view = new QListView(comboBox);
    comboBox->setView(view);
  }

  comboBox->setMaxVisibleItems(qMax(3, visibleItems));
  view->setUniformItemSizes(true);
  view->setSpacing(0);
  view->setMinimumHeight(0);
  view->setStyleSheet(QStringLiteral(
      "QListView { background:#ffffff; color:#24292f; border:1px solid #d0d7de; border-radius:6px; padding:4px; outline:0; }"
      "QListView::item { min-height:28px; padding:4px 10px; color:#24292f; background:#ffffff; border-radius:4px; }"
      "QListView::item:hover { background:#f3f6fa; color:#24292f; }"
      "QListView::item:selected { background:#edf5ff; color:#0969da; }"
      "QScrollBar:vertical { width:8px; background:transparent; margin:2px; border:0; }"
      "QScrollBar::handle:vertical { background:#c9d1d9; min-height:28px; border-radius:4px; }"
      "QScrollBar::handle:vertical:hover { background:#8c959f; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"));

  QPalette palette = view->palette();
  palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
  palette.setColor(QPalette::Text, QColor(QStringLiteral("#24292f")));
  palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#edf5ff")));
  palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#0969da")));
  view->setPalette(palette);
}

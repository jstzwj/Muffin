#include "app/PreferencesPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
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
  // Themed via object name (see dialogStyleSheet) so the colour follows the
  // active theme instead of a hard-coded near-black that's invisible on dark.
  label->setObjectName(QStringLiteral("preferencesSectionLabel"));
  return label;
}

QLabel* muffin::PreferencesPage::makeMutedLabel(QWidget* parent) const {
  auto* label = new QLabel(parent);
  label->setObjectName(QStringLiteral("preferencesMutedLabel"));
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
  layout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  layout->setSpacing(kRowSpacing);
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
  // Dropdown colours come from the dialog-level stylesheet (QComboBox
  // QAbstractItemView rules in dialogStyleSheet), which is inherited by the
  // combo popup. We deliberately do NOT set a per-view stylesheet or palette
  // here — doing so overrode the theme and forced a white dropdown in dark mode.
}

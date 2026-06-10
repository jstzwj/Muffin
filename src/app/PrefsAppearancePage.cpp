#include "app/PrefsAppearancePage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

muffin::PrefsAppearancePage::PrefsAppearancePage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPageLeftMargin, kPageTopMargin, kPageRightMargin, kPageBottomMargin);
  layout->setSpacing(18);

  auto* cardContainer = new QWidget(this);
  cardContainer->setMaximumWidth(kContentWidth);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(kCardSpacing);
  layout->addWidget(cardContainer);

  // --- Card 1: Theme ---
  auto* themeCard = makeCard(this);
  auto* themeLayout = new QHBoxLayout(themeCard);
  themeLayout->setContentsMargins(18, 16, 18, 16);
  themeLayout->setSpacing(24);
  themeLabel_ = makeSectionLabel(themeCard);
  themeCombo_ = new QComboBox(themeCard);
  themeCombo_->setMinimumWidth(320);
  themeLayout->addWidget(themeLabel_);
  themeLayout->addStretch(1);
  themeLayout->addWidget(themeCombo_);
  cardColumn->addWidget(themeCard);

  // --- Card 2: Zoom ---
  auto* zoomCard = makeCard(this);
  auto* zoomCardLayout = new QVBoxLayout(zoomCard);
  zoomCardLayout->setContentsMargins(18, 16, 18, 16);
  zoomCardLayout->setSpacing(10);
  zoomLabel_ = makeSectionLabel(zoomCard);
  auto* zoomRow = new QHBoxLayout();
  zoomRow->setSpacing(10);
  zoomCombo_ = new QComboBox(zoomCard);
  addNumberItems(zoomCombo_, {60, 75, 90, 100, 110, 125, 150, 175, 200}, QStringLiteral("%"));
  zoomCombo_->setMinimumWidth(180);
  resetZoomButton_ = makeButton(zoomCard);
  zoomRow->addWidget(zoomCombo_);
  zoomRow->addWidget(resetZoomButton_);
  zoomRow->addStretch(1);
  zoomCardLayout->addWidget(zoomLabel_);
  zoomCardLayout->addSpacing(2);
  zoomCardLayout->addLayout(zoomRow);
  cardColumn->addWidget(zoomCard);

  // --- Card 3: Font Size ---
  auto* fontSizeCard = makeCard(this);
  auto* fontSizeLayout = new QHBoxLayout(fontSizeCard);
  fontSizeLayout->setContentsMargins(18, 16, 18, 16);
  fontSizeLayout->setSpacing(24);
  fontSizeLabel_ = makeSectionLabel(fontSizeCard);
  fontSizeCombo_ = new QComboBox(fontSizeCard);
  addNumberItems(fontSizeCombo_, {12, 14, 15, 16, 18, 20, 22, 24}, QStringLiteral("px"));
  fontSizeCombo_->setMinimumWidth(180);
  fontSizeLayout->addWidget(fontSizeLabel_);
  fontSizeLayout->addStretch(1);
  fontSizeLayout->addWidget(fontSizeCombo_);
  cardColumn->addWidget(fontSizeCard);

  // --- Card 4: Status Bar ---
  auto* statusCard = makeCard(this);
  auto* statusLayout = makeCardLayout(statusCard);
  statusBarLabel_ = makeSectionLabel(statusCard);
  showStatusBarCheck_ = new QCheckBox(statusCard);
  showStatusBarCheck_->setChecked(true);
  statusLayout->addWidget(statusBarLabel_);
  statusLayout->addSpacing(2);
  statusLayout->addWidget(showStatusBarCheck_);
  cardColumn->addWidget(statusCard);

  layout->addStretch(1);

  retranslateUi();

  connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    const QString name = themeCombo_->itemData(index).toString();
    if (!name.isEmpty()) {
      emit themeRequested(name);
    }
  });
  connect(zoomCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index >= 0) {
      emit zoomPercentRequested(zoomCombo_->itemData(index).toInt());
    }
  });
  connect(resetZoomButton_, &QPushButton::clicked, this, [this] {
    setNumberComboValue(zoomCombo_, 100);
    emit zoomPercentRequested(100);
  });
  connect(fontSizeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index >= 0) {
      emit fontSizePxRequested(fontSizeCombo_->itemData(index).toInt());
    }
  });
  connect(showStatusBarCheck_, &QCheckBox::toggled, this, &PrefsAppearancePage::statusBarVisibleRequested);
}

void muffin::PrefsAppearancePage::retranslateUi() {
  themeLabel_->setText(tr("Theme"));
  zoomLabel_->setText(tr("Zoom"));
  resetZoomButton_->setText(tr("Reset"));
  fontSizeLabel_->setText(tr("Text Size"));
  statusBarLabel_->setText(tr("Status Bar"));
  showStatusBarCheck_->setText(tr("Show status bar"));
  for (int i = 0; i < themeCombo_->count(); ++i) {
    themeCombo_->setItemText(i, themeDisplayName(themeCombo_->itemData(i).toString()));
  }
}

void muffin::PrefsAppearancePage::setAvailableThemes(const QStringList& themes) {
  const QString current = themeCombo_->currentData().toString();
  themeCombo_->blockSignals(true);
  themeCombo_->clear();
  for (const QString& theme : themes) {
    themeCombo_->addItem(themeDisplayName(theme), theme);
  }
  polishComboBox(themeCombo_);
  const int index = themeCombo_->findData(current);
  if (index >= 0) {
    themeCombo_->setCurrentIndex(index);
  }
  themeCombo_->blockSignals(false);
}

void muffin::PrefsAppearancePage::setCurrentThemeName(const QString& name) {
  const int index = themeCombo_->findData(name.toLower());
  if (index >= 0) {
    themeCombo_->blockSignals(true);
    themeCombo_->setCurrentIndex(index);
    themeCombo_->blockSignals(false);
  }
}

void muffin::PrefsAppearancePage::setStatusBarVisible(bool visible) {
  showStatusBarCheck_->blockSignals(true);
  showStatusBarCheck_->setChecked(visible);
  showStatusBarCheck_->blockSignals(false);
}

void muffin::PrefsAppearancePage::setZoomPercent(int percent) {
  setNumberComboValue(zoomCombo_, percent);
}

void muffin::PrefsAppearancePage::setFontSizePx(int px) {
  setNumberComboValue(fontSizeCombo_, px);
}

QString muffin::PrefsAppearancePage::themeDisplayName(const QString& name) {
  if (name == QStringLiteral("github")) {
    return QStringLiteral("Github");
  }
  if (name == QStringLiteral("newsprint")) {
    return QStringLiteral("Newsprint");
  }
  if (name == QStringLiteral("night")) {
    return QStringLiteral("Night");
  }
  if (name == QStringLiteral("pixyll")) {
    return QStringLiteral("Pixyll");
  }
  if (name == QStringLiteral("whitey")) {
    return QStringLiteral("Whitey");
  }
  return name;
}

void muffin::PrefsAppearancePage::addNumberItems(QComboBox* combo, const QVector<int>& values, const QString& suffix) const {
  combo->clear();
  for (int value : values) {
    combo->addItem(QStringLiteral("%1%2").arg(value).arg(suffix), value);
  }
  polishComboBox(combo);
}

void muffin::PrefsAppearancePage::setNumberComboValue(QComboBox* combo, int value) {
  if (!combo) {
    return;
  }
  int index = combo->findData(value);
  if (index < 0) {
    index = combo->findData(100);
  }
  if (index < 0) {
    index = combo->findData(16);
  }
  combo->blockSignals(true);
  combo->setCurrentIndex(index < 0 ? 0 : index);
  combo->blockSignals(false);
}

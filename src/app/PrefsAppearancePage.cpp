#include "app/PrefsAppearancePage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace muffin {

PrefsAppearancePage::PrefsAppearancePage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(38, 34, 46, 34);
  layout->setSpacing(22);

  auto* content = new QWidget(this);
  content->setMaximumWidth(620);
  auto* form = new QFormLayout(content);
  form->setContentsMargins(0, 0, 0, 0);
  form->setHorizontalSpacing(28);
  form->setVerticalSpacing(18);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  layout->addWidget(content);

  themeLabel_ = makeSectionLabel(content);
  themeCombo_ = new QComboBox(content);
  themeCombo_->setMinimumWidth(320);
  form->addRow(themeLabel_, themeCombo_);

  zoomLabel_ = makeSectionLabel(content);
  auto* zoomBox = new QWidget(content);
  auto* zoomLayout = new QHBoxLayout(zoomBox);
  zoomLayout->setContentsMargins(0, 0, 0, 0);
  zoomLayout->setSpacing(10);
  zoomCombo_ = new QComboBox(zoomBox);
  addNumberItems(zoomCombo_, {60, 75, 90, 100, 110, 125, 150, 175, 200}, QStringLiteral("%"));
  zoomCombo_->setMinimumWidth(180);
  resetZoomButton_ = makeButton(zoomBox);
  zoomLayout->addWidget(zoomCombo_);
  zoomLayout->addWidget(resetZoomButton_);
  zoomLayout->addStretch(1);
  form->addRow(zoomLabel_, zoomBox);

  fontSizeLabel_ = makeSectionLabel(content);
  fontSizeCombo_ = new QComboBox(content);
  addNumberItems(fontSizeCombo_, {12, 14, 15, 16, 18, 20, 22, 24}, QStringLiteral("px"));
  fontSizeCombo_->setMinimumWidth(180);
  form->addRow(fontSizeLabel_, fontSizeCombo_);

  statusBarLabel_ = makeSectionLabel(content);
  showStatusBarCheck_ = new QCheckBox(content);
  showStatusBarCheck_->setChecked(true);
  form->addRow(statusBarLabel_, showStatusBarCheck_);

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

void PrefsAppearancePage::retranslateUi() {
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

void PrefsAppearancePage::setAvailableThemes(const QStringList& themes) {
  const QString current = themeCombo_->currentData().toString();
  themeCombo_->blockSignals(true);
  themeCombo_->clear();
  for (const QString& theme : themes) {
    themeCombo_->addItem(themeDisplayName(theme), theme);
  }
  const int index = themeCombo_->findData(current);
  if (index >= 0) {
    themeCombo_->setCurrentIndex(index);
  }
  themeCombo_->blockSignals(false);
}

void PrefsAppearancePage::setCurrentThemeName(const QString& name) {
  const int index = themeCombo_->findData(name.toLower());
  if (index >= 0) {
    themeCombo_->blockSignals(true);
    themeCombo_->setCurrentIndex(index);
    themeCombo_->blockSignals(false);
  }
}

void PrefsAppearancePage::setStatusBarVisible(bool visible) {
  showStatusBarCheck_->blockSignals(true);
  showStatusBarCheck_->setChecked(visible);
  showStatusBarCheck_->blockSignals(false);
}

void PrefsAppearancePage::setZoomPercent(int percent) {
  setNumberComboValue(zoomCombo_, percent);
}

void PrefsAppearancePage::setFontSizePx(int px) {
  setNumberComboValue(fontSizeCombo_, px);
}

QString PrefsAppearancePage::themeDisplayName(const QString& name) {
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

void PrefsAppearancePage::addNumberItems(QComboBox* combo, const QVector<int>& values, const QString& suffix) {
  combo->clear();
  for (int value : values) {
    combo->addItem(QStringLiteral("%1%2").arg(value).arg(suffix), value);
  }
}

void PrefsAppearancePage::setNumberComboValue(QComboBox* combo, int value) {
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

}  // namespace muffin

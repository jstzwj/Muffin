#include "app/PrefsAppearancePage.h"
#include "app/PreferencesComboBox.h"
#include "theme/TypographyOverrides.h"
#include "theme/RenderTheme.h"
#include "theme/FontRendering.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// Keep names in the UI font so symbolic and decorative families remain identifiable.
// FontRole supplies the sample font; the standard style still paints focus and selection.
class FontPreviewDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void setSampleText(QString text) { sampleText_ = std::move(text); }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem item(option);
    initStyleOption(&item, index);
    auto sampleFont = item.font;
    sampleFont.setPixelSize(16);
    muffin::font_rendering::configureForScreen(sampleFont);
    item.font = option.font;
    const auto name = item.text;
    item.text.clear();
    const auto* style = item.widget ? item.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);

    const auto content = item.rect.adjusted(12, 0, -12, 0);
    const int sampleWidth = qMin(220, content.width() / 2);
    const QRect nameRect(content.left(), content.top(), content.width() - sampleWidth - 16, content.height());
    const QRect sampleRect(content.right() - sampleWidth + 1, content.top(), sampleWidth, content.height());
    const auto colorGroup = item.state & QStyle::State_Enabled ? QPalette::Normal : QPalette::Disabled;
    const auto colorRole = item.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text;
    painter->save();
    painter->setClipRect(content);
    painter->setPen(item.palette.color(colorGroup, colorRole));
    painter->setFont(item.font);
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(item.font).elidedText(name, Qt::ElideRight, nameRect.width()));
    painter->setFont(sampleFont);
    painter->drawText(sampleRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(sampleFont).elidedText(sampleText_, Qt::ElideRight, sampleRect.width()));
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    Q_UNUSED(index);
    // Preview rows have a stable UI-sized height, independent of the document's font size.
    return QSize(360, qMax(36, option.fontMetrics.height() + 18));
  }

private:
  QString sampleText_ = QStringLiteral("ABC 123");
};

void addFontFamily(QComboBox* combo, const QString& family) {
  combo->addItem(family, family);
  combo->setItemData(combo->count() - 1, QFont(family), Qt::FontRole);
  combo->setItemData(combo->count() - 1, family, Qt::ToolTipRole);
}

} // namespace

muffin::PrefsAppearancePage::PrefsAppearancePage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPageLeftMargin, kPageTopMargin, kPageRightMargin, kPageBottomMargin);
  layout->setSpacing(14);

  auto* cardContainer = new QWidget(this);
  cardContainer->setObjectName(QStringLiteral("settingsGroup"));
  cardContainer->setMaximumWidth(kContentWidth);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(kCardSpacing);
  layout->addWidget(cardContainer);

  // --- Card 1: Theme ---
  auto* themeCard = makeCard(this);
  auto* themeLayout = new QVBoxLayout(themeCard);
  themeLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  themeLayout->setSpacing(8);
  themeLabel_ = makeSectionLabel(themeCard);
  themeCombo_ = new PreferencesComboBox(themeCard);
  themeCombo_->setMinimumWidth(320);
  auto* themeRow = new QHBoxLayout();
  themeRow->setSpacing(18);
  themeRow->addWidget(themeLabel_);
  themeRow->addStretch(1);
  themeRow->addWidget(themeCombo_);
  themeLayout->addLayout(themeRow);
  importThemeButton_ = makeButton(themeCard);
  importThemeButton_->setCursor(Qt::PointingHandCursor);
  themeLayout->addWidget(importThemeButton_);
  cardColumn->addWidget(themeCard);

  // --- Card 2: Zoom ---
  auto* zoomCard = makeCard(this);
  auto* zoomCardLayout = new QVBoxLayout(zoomCard);
  zoomCardLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin,
                                     kRowVerticalMargin);
  zoomCardLayout->setSpacing(kRowSpacing);
  zoomLabel_ = makeSectionLabel(zoomCard);
  auto* zoomRow = new QHBoxLayout();
  zoomRow->setSpacing(10);
  zoomCombo_ = new PreferencesComboBox(zoomCard);
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
  fontSizeLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin,
                                     kRowVerticalMargin);
  fontSizeLayout->setSpacing(18);
  fontSizeLabel_ = makeSectionLabel(fontSizeCard);
  fontSizeCombo_ = new PreferencesComboBox(fontSizeCard);
  addNumberItems(fontSizeCombo_, {12, 14, 15, 16, 18, 20, 22, 24}, QStringLiteral("px"));
  fontSizeCombo_->setMinimumWidth(180);
  fontSizeLayout->addWidget(fontSizeLabel_);
  fontSizeLayout->addStretch(1);
  fontSizeLayout->addWidget(fontSizeCombo_);
  cardColumn->addWidget(fontSizeCard);

  auto* fontsCard = makeCard(this);
  auto* fontsLayout = makeCardLayout(fontsCard);
  typographyLabel_ = makeSectionLabel(fontsCard);
  typographyNote_ = makeMutedLabel(fontsCard);
  typographyNote_->setWordWrap(true);
  fontsLayout->addWidget(typographyLabel_);
  fontsLayout->addWidget(typographyNote_);
  for (auto& section : typographySections_)
    section = makeSectionLabel(fontsCard);
  fontsLayout->addSpacing(4);
  fontsLayout->addWidget(typographySections_[0]);
  auto* fontForm = new QFormLayout();
  fontForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  fontForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
  fontForm->setSpacing(kRowSpacing);
  fontsLayout->addLayout(fontForm);
  const auto families = QFontDatabase::families();
  const char* familyNames[] = {"TypographyBodyFamily", "TypographyCodeFamily"};
  for (int i = 0; i < 2; ++i) {
    familyLabels_[i] = new QLabel(fontsCard);
    auto* combo = familyCombos_[i] = new PreferencesComboBox(fontsCard);
    combo->setObjectName(QString::fromLatin1(familyNames[i]));
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(18);
    combo->addItem(QString(), QString());
    for (const auto& family : families)
      addFontFamily(combo, family);
    polishComboBox(combo, 12);
    combo->setItemDelegate(new FontPreviewDelegate(combo));
    fontForm->addRow(familyLabels_[i], combo);
  }
  std::array<QGridLayout*, 2> sizeGrids{};
  for (int i = 0; i < 2; ++i) {
    fontsLayout->addSpacing(10);
    fontsLayout->addWidget(typographySections_[i + 1]);
    auto* grid = sizeGrids[i] = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(kRowSpacing);
    for (int column = 0; column < 3; ++column)
      grid->setColumnStretch(column, 1);
    fontsLayout->addLayout(grid);
  }
  const char* sizeNames[] = {"TypographyBodySize", "TypographyCodeSize", "TypographySourceSize",
                             "TypographyH1Size",   "TypographyH2Size",   "TypographyH3Size",
                             "TypographyH4Size",   "TypographyH5Size",   "TypographyH6Size"};
  for (int i = 0; i < 9; ++i) {
    auto* field = new QWidget(fontsCard);
    auto* fieldLayout = new QVBoxLayout(field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(6);
    sizeLabels_[i] = new QLabel(field);
    sizeLabels_[i]->setWordWrap(true);
    auto* combo = sizeCombos_[i] = new PreferencesComboBox(field);
    combo->setObjectName(QString::fromLatin1(sizeNames[i]));
    combo->addItem(QString(), 0);
    for (int px = 8; px <= 96; ++px)
      combo->addItem(QStringLiteral("%1 px").arg(px), px);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(8);
    polishComboBox(combo, 10);
    fieldLayout->addWidget(sizeLabels_[i]);
    fieldLayout->addWidget(combo);
    sizeGrids[i < 3 ? 0 : 1]->addWidget(field, i < 3 ? 0 : (i - 3) / 3, i % 3, Qt::AlignTop);
  }
  fontsLayout->addSpacing(10);
  resetFontsButton_ = makeButton(fontsCard);
  resetFontsButton_->setObjectName(QStringLiteral("TypographyReset"));
  fontsLayout->addWidget(resetFontsButton_, 0, Qt::AlignLeft);
  cardColumn->addWidget(fontsCard);

  // --- Card 4: Content Width ---
  auto* contentWidthCard = makeCard(this);
  auto* contentWidthLayout = new QHBoxLayout(contentWidthCard);
  contentWidthLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin,
                                         kRowVerticalMargin);
  contentWidthLayout->setSpacing(18);
  contentWidthLabel_ = makeSectionLabel(contentWidthCard);
  contentWidthCombo_ = new PreferencesComboBox(contentWidthCard);
  contentWidthCombo_->addItem(QString(), 0);
  contentWidthCombo_->addItem(QString(), 1024);
  contentWidthCombo_->addItem(QString(), 1200);
  contentWidthCombo_->addItem(QString(), -1);
  contentWidthCombo_->setMinimumWidth(180);
  polishComboBox(contentWidthCombo_);
  contentWidthLayout->addWidget(contentWidthLabel_);
  contentWidthLayout->addStretch(1);
  contentWidthLayout->addWidget(contentWidthCombo_);
  cardColumn->addWidget(contentWidthCard);

  // --- Card 5: Status Bar ---
  auto* statusCard = makeCard(this);
  statusCard->setProperty("lastSettingsRow", true);
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
  loadTypography();
  for (auto* combo : familyCombos_)
    connect(combo, &QComboBox::currentIndexChanged, this, &PrefsAppearancePage::saveTypography);
  for (auto* combo : sizeCombos_)
    connect(combo, &QComboBox::currentIndexChanged, this, &PrefsAppearancePage::saveTypography);
  connect(resetFontsButton_, &QPushButton::clicked, this, [this] {
    TypographyOverrides{}.save();
    loadTypography();
    emit typographyChanged();
  });

  connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    const QString name = themeCombo_->itemData(index).toString();
    if (!name.isEmpty()) {
      emit themeRequested(name);
    }
  });
  connect(importThemeButton_, &QPushButton::clicked, this, &PrefsAppearancePage::importThemeRequested);
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
      updateThemeFontSamples();
    }
  });
  connect(contentWidthCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index >= 0) {
      emit contentWidthPxRequested(contentWidthCombo_->itemData(index).toInt());
    }
  });
  connect(showStatusBarCheck_, &QCheckBox::toggled, this, &PrefsAppearancePage::statusBarVisibleRequested);
}

void muffin::PrefsAppearancePage::retranslateUi() {
  themeLabel_->setText(tr("Theme"));
  zoomLabel_->setText(tr("Zoom"));
  resetZoomButton_->setText(tr("Reset"));
  fontSizeLabel_->setText(tr("Text Scale"));
  for (int i = 0; i < fontSizeCombo_->count(); ++i)
    fontSizeCombo_->setItemText(i, QStringLiteral("%1%").arg(fontSizeCombo_->itemData(i).toInt() * 100.0 / 16));
  typographyLabel_->setText(tr("Fonts and Typography"));
  typographySections_[0]->setText(tr("Fonts"));
  typographySections_[1]->setText(tr("Font sizes"));
  typographySections_[2]->setText(tr("Heading sizes"));
  typographyNote_->setText(tr(
      "Theme defaults are kept unless overridden. Sizes are logical pixels before text scale and zoom. Headings keep the theme's proportions unless set individually."));
  familyLabels_[0]->setText(tr("Body font"));
  familyLabels_[1]->setText(tr("Code font"));
  sizeLabels_[0]->setText(tr("Body size"));
  sizeLabels_[1]->setText(tr("Code size"));
  sizeLabels_[2]->setText(tr("Source editor size"));
  for (int i = 0; i < 6; ++i)
    sizeLabels_[i + 3]->setText(tr("Heading %1 size").arg(i + 1));
  //: Font dropdown sample. Add a short sample in this language's script; keep it compact. The English fallback is ABC 123.
  const auto fontSample = tr("ABC 123");
  for (auto* combo : familyCombos_) {
    combo->setItemText(0, tr("Theme Default"));
    static_cast<FontPreviewDelegate*>(combo->itemDelegate())->setSampleText(fontSample);
    combo->view()->viewport()->update();
  }
  for (int i = 0; i < 9; ++i)
    sizeCombos_[i]->setItemText(0, i == 2 ? tr("Follow code size") : tr("Theme Default"));
  resetFontsButton_->setText(tr("Restore theme fonts"));
  contentWidthLabel_->setText(tr("Content Width"));
  contentWidthCombo_->setItemText(0, tr("Theme Default"));
  contentWidthCombo_->setItemText(1, tr("Comfortable (1024 px)"));
  contentWidthCombo_->setItemText(2, tr("Wide (1200 px)"));
  contentWidthCombo_->setItemText(3, tr("Full Width"));
  statusBarLabel_->setText(tr("Status Bar"));
  showStatusBarCheck_->setText(tr("Show status bar"));
  if (importThemeButton_) {
    importThemeButton_->setText(tr("Import Theme..."));
  }
  // Refresh dropdown labels from the stored (id, label) pairs — custom themes
  // carry their authored label, so this isn't a hard-coded lookup any more.
  for (int i = 0; i < themeCombo_->count() && i < themes_.size(); ++i) {
    themeCombo_->setItemText(i, themes_.at(i).second);
  }
}

void muffin::PrefsAppearancePage::setAvailableThemes(const QVector<QPair<QString, QString>>& themes) {
  const QString current = themeCombo_->currentData().toString();
  themes_ = themes;
  themeCombo_->blockSignals(true);
  themeCombo_->clear();
  for (const auto& entry : themes_) {
    themeCombo_->addItem(entry.second, entry.first); // text=label, data=id
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
  updateThemeFontSamples();
}

void muffin::PrefsAppearancePage::loadTypography() {
  const auto values = TypographyOverrides::load();
  const QString families[] = {values.bodyFamily, values.codeFamily};
  for (int i = 0; i < 2; ++i) {
    auto* combo = familyCombos_[i];
    const QSignalBlocker blocker(combo);
    int index = combo->findData(families[i]);
    if (index < 0) {
      // Keep a configured font visible when it is temporarily unavailable.
      addFontFamily(combo, families[i]);
      index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
  }
  for (int i = 0; i < 9; ++i) {
    auto* combo = sizeCombos_[i];
    const QSignalBlocker blocker(combo);
    const int size = i == 0   ? values.bodySize
                     : i == 1 ? values.codeSize
                     : i == 2 ? values.sourceSize
                              : values.headingSizes[i - 3];
    combo->setCurrentIndex(combo->findData(size));
  }
  updateThemeFontSamples();
}

void muffin::PrefsAppearancePage::saveTypography() {
  TypographyOverrides values;
  values.bodyFamily = familyCombos_[0]->currentData().toString();
  values.codeFamily = familyCombos_[1]->currentData().toString();
  values.bodySize = sizeCombos_[0]->currentData().toInt();
  values.codeSize = sizeCombos_[1]->currentData().toInt();
  values.sourceSize = sizeCombos_[2]->currentData().toInt();
  for (int i = 0; i < 6; ++i)
    values.headingSizes[i] = sizeCombos_[i + 3]->currentData().toInt();
  values.save();
  emit typographyChanged();
}

void muffin::PrefsAppearancePage::setPreviewTheme(const ThemeDefinition& definition) {
  previewTheme_ = definition;
  updateThemeFontSamples();
}

void muffin::PrefsAppearancePage::updateThemeFontSamples() {
  auto theme = RenderTheme::fromDefinition(previewTheme_, 100, fontSizeCombo_->currentData().toInt());
  theme.updateForViewport(680);
  familyCombos_[0]->setItemData(0, theme.paragraphFont(), Qt::FontRole);
  familyCombos_[1]->setItemData(0, theme.codeFont(), Qt::FontRole);
}

void muffin::PrefsAppearancePage::setContentWidthPx(int px) {
  setNumberComboValue(contentWidthCombo_, px);
}

void muffin::PrefsAppearancePage::addNumberItems(QComboBox* combo, const QVector<int>& values,
                                                 const QString& suffix) const {
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

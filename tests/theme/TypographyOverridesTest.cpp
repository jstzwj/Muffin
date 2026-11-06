#include "app/PrefsAppearancePage.h"
#include "document/DocumentSession.h"
#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"
#include "editor/VirtualSourceEdit.h"
#include "export/NativeExporter.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/DocumentStyleTree.h"
#include "theme/RenderTheme.h"
#include "theme/TypographyOverrides.h"
#include <QApplication>
#include <QComboBox>
#include <QAbstractItemView>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QKeyEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>
#include <QStyleOptionViewItem>
#include <QTextStream>
#include <QWheelEvent>
#include <cmath>
#include <cstdlib>

using namespace muffin;
static void require(bool ok, const char* message) {
  if (!ok) {
    QTextStream(stderr) << message << '\n';
    std::exit(1);
  }
}
static bool near(qreal a, qreal b) {
  return std::abs(a - b) < .03;
}
static qreal pixels(const QFont& font) {
  return font.pointSizeF() / .75;
}

static void themesAndProjection() {
  for (const auto& id : {"github", "newsprint", "night", "pixyll", "whitey"}) {
    const auto definition = *ThemeDefinition::builtIn(QString::fromLatin1(id));
    auto original = RenderTheme::fromDefinition(definition);
    original.updateForViewport(1100);
    const auto body = pixels(original.paragraphFont());
    const auto heading = pixels(original.headingFont(1));
    auto changed = original.detachedPrototype();
    TypographyOverrides overrides;
    overrides.bodySize = 24;
    overrides.codeSize = 17;
    overrides.headingSizes[2] = 31;
    overrides.bodyFamily = "Arial";
    overrides.codeFamily = "Consolas";
    changed.setTypographyOverrides(overrides);
    changed.updateForViewport(1100);
    require(changed.paragraphFont().families().first() == "Arial" &&
                changed.codeFont().families().first() == "Consolas",
            "body and code use their separately selected font families");
    require(near(pixels(changed.paragraphFont()), 24), "body uses the requested logical size");
    require(near(pixels(changed.headingFont(1)), heading * 24 / body), "default headings keep theme proportions");
    require(near(pixels(changed.headingFont(3)), 31), "explicit heading size is independent of body size");
    require(near(pixels(changed.codeFont()), 17) && near(pixels(changed.inlineCodeFont()), 17),
            "inline and block code share the explicit code size");
    const auto expected = changed.paragraphFont().key();
    changed.updateForViewport(900);
    changed.updateForViewport(1100);
    require(changed.paragraphFont().key() == expected, "cached projection retains typography");
    require(near(pixels(original.paragraphFont()), body), "overrides never mutate the original theme");
    changed.setZoomPercent(125);
    changed.updateForViewport(1100);
    require(near(pixels(changed.codeFont()), 17 * 1.25), "zoom applies once to explicit code size");
    changed.setTypographyOverrides({});
    changed.setZoomPercent(100);
    changed.updateForViewport(1100);
    require(changed.paragraphFont().key() == original.paragraphFont().key(), "reset restores original theme exactly");
  }
}

static void cascadeAndSpecialFonts() {
  auto definition = CssThemeMapper::fromCss(
      "#write {font:16px Arial} #write p {font-size:18px} #write h1 {font: bold 32px Georgia !important} "
      "#write pre,#write code {font:12px monospace !important} "
      "#write .icon {font-family:IconFont !important} "
      "@media (max-width:700px) {#write {font-size:20px} #write p {font-size:20px} h1 {font-size:40px}}",
      "test", {});
  auto theme = RenderTheme::fromDefinition(definition);
  TypographyOverrides overrides;
  overrides.bodySize = 24;
  overrides.bodyFamily = "Segoe UI";
  overrides.codeFamily = "Consolas";
  overrides.codeSize = 18;
  overrides.headingSizes[0] = 35;
  theme.setTypographyOverrides(overrides);
  theme.updateForViewport(900);
  require(near(pixels(theme.headingFont(1)), 35), "user origin overrides important author typography");
  DocumentStyleHost host;
  CssElement icon;
  icon.tag = "span";
  icon.classes << "icon";
  icon.parent = &host.write;
  require(theme.computedStyleForElement(icon).resolvedValue("font-family").contains("IconFont"),
          "icon fonts retain their explicit families");
  CssElement h1;
  h1.tag = "h1";
  h1.parent = &host.write;
  h1.inlineDeclarations = CssThemeParser::parseDeclarations("font-size:90px!important");
  require(near(theme.computedStyleForElement(h1).fontSizePx * theme.documentCssEnvironment().textScale, 35),
          "user origin has consistent precedence for live inline styles");
  CssComputedStyleEngine htmlEngine(documentStyleSheet(*theme.documentStyleSheet()), theme.documentCssEnvironment());
  require(near(htmlEngine.styleFor(h1).fontSizePx * theme.documentCssEnvironment().textScale, 35),
          "HTML and Markdown share the effective stylesheet");
  theme.updateForViewport(650);
  require(near(pixels(theme.paragraphFont()), 24) && near(pixels(theme.headingFont(1)), 35),
          "responsive themes retain absolute overrides across media changes");
}

static void settingsAndPreferences() {
  TypographyOverrides{}.save();
  PrefsAppearancePage page;
  page.setPreviewTheme(*ThemeDefinition::builtIn("github"));
  page.setFontSizePx(16);
  page.resize(730, 1400);
  page.show();
  QApplication::processEvents();
  for (const auto* name : {"TypographySourceSize", "TypographyH1Size", "TypographyH6Size"})
    require(page.findChild<QComboBox*>(name)->isVisible(), "source and heading sizes are visible without expansion");
  auto* family = page.findChild<QComboBox*>("TypographyBodyFamily");
  const int sampleIndex = family->count() > 1 ? 1 : 0;
  if (sampleIndex > 0)
    require(family->itemData(sampleIndex, Qt::FontRole).value<QFont>().family() ==
                family->itemData(sampleIndex).toString(),
            "installed font rows carry their own preview family");
  const auto renderSample = [&](const QFont& font) {
    family->setItemData(sampleIndex, font, Qt::FontRole);
    QImage image(500, 40, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QStyleOptionViewItem option;
    option.initFrom(family->view());
    option.rect = image.rect();
    family->itemDelegate()->paint(&painter, option, family->model()->index(sampleIndex, 0));
    return image;
  };
  auto plain = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  auto bold = plain;
  bold.setBold(!plain.bold());
  const auto firstPreview = renderSample(plain), secondPreview = renderSample(bold);
  require(firstPreview.copy(0, 0, 250, 40) == secondPreview.copy(0, 0, 250, 40) &&
              firstPreview.copy(280, 0, 220, 40) != secondPreview.copy(280, 0, 220, 40),
          "font samples use row typography while names stay readable in the UI font");
  class SampleTranslator final : public QTranslator {
  public:
    bool isEmpty() const override { return false; }
    QString translate(const char* context, const char* source, const char*, int) const override {
      return QByteArray(context) == "muffin::PrefsAppearancePage" && QByteArray(source) == "ABC 123"
                 ? QStringLiteral("ABC 123 日本語")
                 : QString{};
    }
  } translator;
  const auto fallbackSample = renderSample(plain);
  QApplication::installTranslator(&translator);
  page.retranslateUi();
  const auto localizedSample = renderSample(plain);
  require(fallbackSample.copy(280, 0, 220, 40) != localizedSample.copy(280, 0, 220, 40),
          "dropdown samples repaint with the interface language");
  QApplication::removeTranslator(&translator);
  page.retranslateUi();
  require(renderSample(plain) == fallbackSample, "a missing sample translation falls back to ABC 123");
  int changed = 0;
  QObject::connect(&page, &PrefsAppearancePage::typographyChanged, [&] { ++changed; });
  auto* body = page.findChild<QComboBox*>("TypographyBodySize");
  auto* code = page.findChild<QComboBox*>("TypographyCodeSize");
  auto* heading = page.findChild<QComboBox*>("TypographyH3Size");
  require(body && code && heading && body->currentData().toInt() == 0, "new settings follow the theme");
  body->setCurrentIndex(body->findData(22));
  code->setCurrentIndex(code->findData(18));
  heading->setCurrentIndex(heading->findData(29));
  require(changed == 3 && TypographyOverrides::load().headingSizes[2] == 29, "changes save and notify immediately");
  PrefsAppearancePage reopened;
  require(reopened.findChild<QComboBox*>("TypographyBodySize")->currentData().toInt() == 22,
          "a reopened preferences page restores the saved values");
  page.setPreviewTheme(*ThemeDefinition::builtIn("night"));
  require(TypographyOverrides::load().bodySize == 22, "switching theme keeps user choices");
  page.findChild<QPushButton*>("TypographyReset")->click();
  require(!TypographyOverrides::load().hasDocumentOverrides() && body->currentData().toInt() == 0,
          "restore theme fonts removes all overrides");
}

static void wheelScrollsPreferencesWithoutChangingValues() {
  TypographyOverrides{}.save();
  QScrollArea scroll;
  auto* page = new PrefsAppearancePage;
  scroll.setWidget(page);
  scroll.setWidgetResizable(true);
  scroll.resize(760, 360);
  scroll.show();
  QApplication::processEvents();
  int changes = 0;
  QObject::connect(page, &PrefsAppearancePage::typographyChanged, [&] { ++changes; });
  const auto wheel = [](QWidget* target, QPoint pixels, QPoint angle) {
    const QPointF local = target->rect().center();
    QWheelEvent event(local, target->mapToGlobal(local.toPoint()), pixels, angle, Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &event);
    QApplication::processEvents();
  };
  for (auto* combo : page->findChildren<QComboBox*>()) {
    for (bool focused : {false, true}) {
      scroll.ensureWidgetVisible(combo);
      if (focused)
        combo->setFocus();
      else
        scroll.setFocus();
      const auto index = combo->currentIndex();
      wheel(combo, {}, {0, -120});
      wheel(combo, {}, {0, 120});
      wheel(combo, {0, -24}, {});
      require(combo->currentIndex() == index,
              "closed preference lists ignore mouse and touchpad wheels even with focus");
    }
  }
  require(changes == 0 && TypographyOverrides::load() == TypographyOverrides{},
          "scrolling over font settings never saves unintended changes");
  auto* size = page->findChild<QComboBox*>("TypographyBodySize");
  scroll.ensureWidgetVisible(size);
  scroll.verticalScrollBar()->setValue(scroll.verticalScrollBar()->maximum() / 2);
  const int before = scroll.verticalScrollBar()->value();
  wheel(size, {}, {0, -120});
  require(scroll.verticalScrollBar()->value() > before, "ignored field wheels continue scrolling the preferences page");
  scroll.ensureWidgetVisible(size);
  size->showPopup();
  QApplication::processEvents();
  auto* list = size->view();
  const int listBefore = list->verticalScrollBar()->value();
  wheel(list->viewport(), {}, {0, -120});
  require(list->verticalScrollBar()->value() > listBefore, "open preference lists retain wheel scrolling");
  size->hidePopup();
  size->setFocus();
  QKeyEvent key(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
  QApplication::sendEvent(size, &key);
  require(size->currentData().toInt() == 8 && changes == 1 && TypographyOverrides::load().bodySize == 8,
          "intentional keyboard changes still apply and save");
  TypographyOverrides{}.save();
}

static void layoutAndHitTesting() {
  DocumentSession session;
  session.setMarkdownText("# Heading\n\nBody text with 中文 and `code`.\n\n```cpp\nint x = 42;\n```", false);
  EditorView view;
  view.resize(900, 700);
  auto theme = RenderTheme::github();
  TypographyOverrides overrides;
  overrides.bodySize = 25;
  overrides.codeSize = 19;
  overrides.headingSizes[0] = 41;
  theme.setTypographyOverrides(overrides);
  view.setTheme(theme);
  view.setDocument(session.document());
  view.show();
  for (int i = 0; i < 30; ++i)
    QApplication::processEvents();
  const auto& paragraph = *session.document().root().children()[1];
  const auto* block = view.blockLayoutForNode(paragraph.id());
  require(block && near(pixels(view.theme().paragraphFont()), 25), "live layout uses overridden typography");
  const auto hit = view.hitTest(view.mapDocumentToViewport(block->rect().topLeft() + QPointF(2, 12)));
  require(hit.blockId == paragraph.id(), "text hit testing follows the changed layout");
  require(session.markdownText().toString().startsWith("# Heading\n\nBody"),
          "typography never changes document source");
}

static void sourceFontAndZoom() {
  auto theme = RenderTheme::github();
  TypographyOverrides overrides;
  overrides.codeSize = 18;
  overrides.codeFamily = "Consolas";
  theme.setTypographyOverrides(overrides);
  SourceEditorWidget source;
  source.setTheme(theme);
  auto* editor = source.findChild<VirtualSourceEdit*>();
  require(editor != nullptr, "source facade owns the source editor");
  auto font = [&] { return qvariant_cast<QFont>(editor->inputMethodQuery(Qt::ImFont)); };
  require(near(pixels(font()), 18), "source editor follows the explicit code size");
  require(font().families().first() == "Consolas", "source editor follows the selected code family");
  source.setZoomPercent(150);
  require(near(pixels(font()), 27), "source zoom is applied once");
  overrides.sourceSize = 21;
  theme.setTypographyOverrides(overrides);
  source.setTheme(theme);
  require(near(pixels(font()), 31.5), "independent source size overrides code size");
  theme.updateForViewport(900);
  require(near(pixels(theme.codeFont()), 18), "source size does not change rendered code");
}

static void nativeExportUsesTypography() {
  DocumentSession session;
  const QString markdown = QStringLiteral("Words wrapping on several lines. ").repeated(45);
  session.setMarkdownText(markdown, false);
  QTemporaryDir directory;
  auto theme = RenderTheme::github();
  ImageExportOptions options;
  options.width = 600;
  const auto original = directory.filePath("theme.png");
  require(exportNativeImage(original, session.document(), theme, {}, options), "export theme-default image");
  TypographyOverrides overrides;
  overrides.bodySize = 30;
  theme.setTypographyOverrides(overrides);
  const auto custom = directory.filePath("custom.png");
  require(exportNativeImage(custom, session.document(), theme, {}, options), "export image with user typography");
  require(QImage(custom).height() > QImage(original).height() * 1.5,
          "native export projects the requested typography before measuring and painting");
  require(
      exportNativePdf(directory.filePath("custom.pdf"), session.document(), theme, {}, pdfExportOptions(QVariantMap{})),
      "PDF export initializes the same effective theme");
  require(session.markdownText().toString() == markdown && !session.document().isModified(),
          "export typography preserves the source document");
}

static void overrideCacheLifetime() {
  DocumentSession session;
  session.setMarkdownText(QString("A **bold** paragraph with some words.\n\n").repeated(160), false);
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss("strong:hover{color:red}", "cache-lifetime", {}));
  TypographyOverrides overrides;
  overrides.bodySize = 22;
  theme.setTypographyOverrides(overrides);
  theme.updateForViewport(700);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 700, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  layout.buildAll(theme);
  auto* paragraph = session.document().root().children().front().get();
  const auto identity = theme.cssElementForNode(*paragraph)->cacheId;
  require(session.applyTextDelta(paragraph->sourceRange().byteStart + 2, 0, "x", true), "edit overridden document");
  layout.rebuildTopLevelRange(session.lastLocalTopLevelRangeChange(), session.document(), theme, {});
  paragraph = session.document().root().children().front().get();
  require(theme.hasLocalStyleChange() && theme.cssElementForNode(*paragraph)->cacheId == identity,
          "typography overlays preserve local style rebinding");
  const auto before = theme.styleCacheStats().elements;
  layout.trimDetails(100, 103, theme, {paragraph->id()}, 8);
  require(theme.styleCacheStats().elements < before / 2,
          "an override base never retains live inline caches after layout eviction");
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir settings;
  QCoreApplication::setOrganizationName("MuffinTests");
  QCoreApplication::setApplicationName("TypographyOverridesTest");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  themesAndProjection();
  cascadeAndSpecialFonts();
  settingsAndPreferences();
  wheelScrollsPreferencesWithoutChangingValues();
  layoutAndHitTesting();
  sourceFontAndZoom();
  nativeExportUsesTypography();
  overrideCacheLifetime();
  return 0;
}

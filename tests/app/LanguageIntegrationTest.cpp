#include "app/HelpViewerDialog.h"
#include "app/LanguageManager.h"
#include "app/StatusBarWidget.h"
#include "app/SidebarWidget.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorView.h"
#include "editor/VirtualSourceEdit.h"
#include "image/CustomCommandUploader.h"
#include "io/FilePathOps.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QHelpEvent>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QToolTip>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const QString& message) {
  if (!condition) {
    std::cerr << message.toStdString() << '\n';
    std::exit(1);
  }
}

QString translated(const char* context, const char* source) {
  return QCoreApplication::translate(context, source);
}

void verifyStatusBarTooltips(muffin::StatusBarWidget& bar, bool localized) {
  for (const bool checked : {false, true}) {
    bar.setSidebarChecked(checked);
    bar.setSourceModeChecked(checked);
    const char* sidebarHint = checked ? "Hide sidebar" : "Show sidebar";
    const char* sourceHint = checked ? "Switch to rendered mode" : "Switch to source mode";
    const QString sidebarText = translated("muffin::StatusBarWidget", sidebarHint);
    const QString sourceText = translated("muffin::StatusBarWidget", sourceHint);
    require(bar.sidebarButton()->toolTip() == sidebarText,
            QStringLiteral("Sidebar tooltip must follow visibility and language"));
    require(bar.sourceModeButton()->toolTip() == sourceText,
            QStringLiteral("Source-mode tooltip must follow mode and language"));
    if (localized) {
      require(sidebarText != QString::fromUtf8(sidebarHint), QStringLiteral("Sidebar hint must be translated"));
      require(sourceText != QString::fromUtf8(sourceHint), QStringLiteral("Source-mode hint must be translated"));
    }
    for (QToolButton* button : {bar.sidebarButton(), bar.sourceModeButton()}) {
      const QPoint center = button->rect().center();
      QHelpEvent help(QEvent::ToolTip, center, button->mapToGlobal(center));
      QApplication::sendEvent(button, &help);
      require(QToolTip::text() == button->toolTip(), QStringLiteral("Hover event must display the button hint"));
      QToolTip::hideText();
    }
  }
}
} // namespace

int main(int argc, char** argv) {
#if !defined(Q_OS_MACOS)
  if (qgetenv("QT_QPA_PLATFORM").isEmpty())
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
  QApplication app(argc, argv);
  QTemporaryDir settingsDir;
  require(settingsDir.isValid(), QStringLiteral("Temporary settings directory must exist"));
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinLanguageTests"));
  QCoreApplication::setApplicationName(QStringLiteral("LanguageIntegration"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

  muffin::DocumentSession tableSession;
  tableSession.setMarkdownText(QStringLiteral("| A | B |\n| --- | --- |\n| x | y |\n"), false);
  muffin::EditorView rendered;
  rendered.resize(800, 600);
  rendered.setDocument(tableSession.document());
  rendered.show();
  app.processEvents();
  const auto* table = tableSession.document().root().children().front().get();
  muffin::HitTestResult tableHit;
  tableHit.zone = muffin::HitTestResult::Zone::TableCell;
  tableHit.blockId = table->id();
  tableHit.textNodeId = table->children().front()->children().front()->id();
  tableHit.tableRow = 0;
  tableHit.tableColumn = 0;
  tableHit.textOffset = 0;
  rendered.setCursorHit(tableHit);
  muffin::VirtualSourceEdit source;
  muffin::StatusBarWidget statusBar;
  muffin::SidebarWidget sidebar;
  statusBar.resize(800, statusBar.height());
  statusBar.show();
  require(statusBar.sidebarButton()->toolTip() == QStringLiteral("Show sidebar"),
          QStringLiteral("Sidebar hint must be available at startup before any language change"));
  require(statusBar.sourceModeButton()->toolTip() == QStringLiteral("Switch to source mode"),
          QStringLiteral("Source-mode hint must be available at startup before any language change"));
  statusBar.setWordCount(123);
  statusBar.setEncodingLineEnding(QStringLiteral("UTF-8 | LF"));
  statusBar.setCursorStatus(QStringLiteral("12 : 4"));
  verifyStatusBarTooltips(statusBar, false);
  auto& manager = muffin::LanguageManager::instance();
  muffin::HelpViewerDialog::open(nullptr, muffin::HelpTopic::QuickStart);
  QStringList codes;
  for (const auto& language : manager.availableLanguages()) {
    if (language.code != QStringLiteral("system"))
      codes.append(language.code);
  }
  codes.append({QStringLiteral("zh-HK"), QStringLiteral("pt-BR"), QStringLiteral("system")});
  for (const QString& code : codes) {
    require(manager.setLanguage(code), QStringLiteral("Language must load: %1").arg(code));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    app.processEvents();
    require(manager.currentLanguageCode() == code, QStringLiteral("Settings must retain the selected language"));
    require(manager.effectiveLanguageCode() != QStringLiteral("system"),
            QStringLiteral("Effective language must resolve system"));
    if (code == QStringLiteral("zh-HK")) {
      require(manager.effectiveLanguageCode() == QStringLiteral("zh_TW"),
              QStringLiteral("Hong Kong must select Traditional Chinese resources"));
    }
    require(rendered.accessibleName() == translated("muffin::EditorView", "Markdown editor"),
            QStringLiteral("Rendered editor accessible name must follow language changes"));
    require(rendered.accessibleDescription() == translated("muffin::EditorView", "Rendered Markdown document"),
            QStringLiteral("Rendered editor description must follow language changes"));
    require(source.accessibleName() == translated("muffin::VirtualSourceEdit", "Markdown source editor"),
            QStringLiteral("Source editor accessible name must follow language changes"));
    require(source.accessibleDescription() == translated("muffin::VirtualSourceEdit", "Plain-text Markdown source"),
            QStringLiteral("Source editor description must follow language changes"));
    verifyStatusBarTooltips(statusBar, manager.effectiveLanguageCode() != QStringLiteral("en"));
    for (const auto& entry :
         {qMakePair("ResizeTableButton", "Resize Table"), qMakePair("TableAlignLeftButton", "Align Left"),
          qMakePair("TableAlignCenterButton", "Align Center"), qMakePair("TableAlignRightButton", "Align Right"),
          qMakePair("TableMoreActionsButton", "More Actions"), qMakePair("DeleteTableButton", "Delete Table")}) {
      const auto* button = rendered.findChild<QToolButton*>(QString::fromLatin1(entry.first));
      const auto label = translated("muffin::TableToolbar", entry.second);
      require(button && button->toolTip() == label && button->accessibleName() == label,
              QStringLiteral("Existing table toolbar hints and accessible names follow language changes: %1 / %2")
                  .arg(code, QString::fromLatin1(entry.first)));
      if (manager.effectiveLanguageCode() != QStringLiteral("en"))
        require(label != QString::fromLatin1(entry.second), QStringLiteral("Table toolbar hints are translated"));
    }
    for (const auto& entry :
         {qMakePair("RefreshFoldersButton", "Refresh Files"), qMakePair("SortFilesButton", "Sort Files")}) {
      auto* button = sidebar.findChild<QToolButton*>(QString::fromLatin1(entry.first));
      const auto label = translated("muffin::SidebarWidget", entry.second);
      require(button && button->toolTip() == label && button->accessibleName() == label,
              QStringLiteral("Explorer button hints follow the current language"));
      if (manager.effectiveLanguageCode() != QStringLiteral("en"))
        require(label != QString::fromLatin1(entry.second), QStringLiteral("Explorer button hints are translated"));
    }
    auto* sortMenu = sidebar.findChild<QMenu*>(QStringLiteral("FileSortMenu"));
    for (const auto& entry : {qMakePair("SortByName", "File Name"), qMakePair("SortByModified", "Date Modified"),
                              qMakePair("SortByCreated", "Date Created"), qMakePair("SortByType", "File Type"),
                              qMakePair("SortBySize", "File Size"), qMakePair("SortAscending", "Ascending"),
                              qMakePair("SortDescending", "Descending")}) {
      auto* action = sortMenu->findChild<QAction*>(QString::fromLatin1(entry.first));
      require(action && action->text() == translated("muffin::SidebarWidget", entry.second),
              QStringLiteral("Explorer sort options follow the current language"));
    }
    for (const bool spellEnabled : {false, true}) {
      statusBar.setSpellLanguage(QStringLiteral("en_US"), spellEnabled);
      QStringList shownHints;
      for (int x = statusBar.sourceModeButton()->geometry().right() + 1; x < statusBar.width(); x += 4) {
        const QPoint pos(x, statusBar.height() / 2);
        QHelpEvent help(QEvent::ToolTip, pos, statusBar.mapToGlobal(pos));
        QApplication::sendEvent(&statusBar, &help);
        shownHints.append(QToolTip::text());
      }
      const char* spellHint =
          spellEnabled ? "Click to change the spell-check language" : "Spell check is off — click to enable";
      for (const char* hint : {"Click to view document statistics", spellHint, "Cursor position (line : column)",
                               "File encoding · line ending"}) {
        require(shownHints.contains(translated("muffin::StatusBarWidget", hint)),
                QStringLiteral("Painted status-bar hints must display in the current language: %1")
                    .arg(QString::fromUtf8(hint)));
      }
      QToolTip::hideText();
    }
    muffin::StatusBarWidget newStatusBar;
    require(newStatusBar.sidebarButton()->toolTip() == translated("muffin::StatusBarWidget", "Show sidebar"),
            QStringLiteral("New status bars must use the startup language"));
    require(newStatusBar.sourceModeButton()->toolTip() ==
                translated("muffin::StatusBarWidget", "Switch to source mode"),
            QStringLiteral("New source-mode buttons must use the startup language"));

    bool found = false;
    for (QWidget* window : QApplication::topLevelWidgets()) {
      auto* help = qobject_cast<muffin::HelpViewerDialog*>(window);
      if (!help)
        continue;
      found = true;
      QString path = QStringLiteral(":/help/quick-start.md");
      if (manager.effectiveLanguageCode() != QStringLiteral("en")) {
        path = QStringLiteral(":/help/%1/quick-start.md").arg(manager.effectiveLanguageCode());
      }
      QFile doc(path);
      require(doc.open(QIODevice::ReadOnly), QStringLiteral("Localized help resource must exist: %1").arg(path));
      const QString heading = QString::fromUtf8(doc.readLine()).trimmed().mid(2);
      auto* browser = help->findChild<QTextBrowser*>();
      require(browser && browser->toPlainText().section(QLatin1Char('\n'), 0, 0) == heading,
              QStringLiteral("Help body must use the effective language: %1").arg(code));
    }
    require(found, QStringLiteral("Help viewer must open"));
  }

  require(manager.setLanguage(QStringLiteral("zh_CN")), QStringLiteral("Chinese must load"));
  const QString path = settingsDir.filePath(QStringLiteral("existing.md"));
  QFile existing(path);
  require(existing.open(QIODevice::WriteOnly), QStringLiteral("Fixture file must open"));
  existing.close();
  QString error;
  require(!muffin::FilePathOps::createFile(path, &error), QStringLiteral("Existing file must be rejected"));
  require(error == translated("muffin::FilePathOps", "file already exists") &&
              error != QStringLiteral("file already exists"),
          QStringLiteral("File operation error detail must be translated"));
  require(!muffin::FilePathOps::revealPathInManager(settingsDir.filePath(QStringLiteral("missing.md")), &error),
          QStringLiteral("A missing path must be rejected before opening the file manager"));
  require(error == translated("muffin::FilePathOps", "path does not exist") &&
              error != QStringLiteral("path does not exist"),
          QStringLiteral("Missing path error detail must be translated"));
  const auto upload = muffin::CustomCommandUploader::upload(nullptr, {});
  require(upload.error == translated("muffin::CustomCommandUploader", "no upload command configured") &&
              upload.error != QStringLiteral("no upload command configured"),
          QStringLiteral("Upload error detail must be translated"));

  require(manager.setLanguage(QStringLiteral("vi")), QStringLiteral("Vietnamese must load"));
  QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::Discard);
  require(buttons.button(QDialogButtonBox::Save)->text().remove(QLatin1Char('&')) == QStringLiteral("Lưu"),
          QStringLiteral("Vietnamese Qt Save button must be translated"));
  require(buttons.button(QDialogButtonBox::Cancel)->text().remove(QLatin1Char('&')) == QStringLiteral("Hủy"),
          QStringLiteral("Vietnamese Qt Cancel button must be translated"));
  require(buttons.button(QDialogButtonBox::Discard)->text().remove(QLatin1Char('&')) == QStringLiteral("Bỏ thay đổi"),
          QStringLiteral("Vietnamese Qt Discard button must be translated"));
  return 0;
}

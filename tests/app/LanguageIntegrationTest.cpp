#include "app/HelpViewerDialog.h"
#include "app/LanguageManager.h"
#include "editor/EditorView.h"
#include "editor/VirtualSourceEdit.h"
#include "image/CustomCommandUploader.h"
#include "io/FilePathOps.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBrowser>

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
}  // namespace

int main(int argc, char** argv) {
#if !defined(Q_OS_MACOS)
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
  QApplication app(argc, argv);
  QTemporaryDir settingsDir;
  require(settingsDir.isValid(), QStringLiteral("Temporary settings directory must exist"));
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinLanguageTests"));
  QCoreApplication::setApplicationName(QStringLiteral("LanguageIntegration"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

  muffin::EditorView rendered;
  muffin::VirtualSourceEdit source;
  auto& manager = muffin::LanguageManager::instance();
  muffin::HelpViewerDialog::open(nullptr, muffin::HelpTopic::QuickStart);
  QStringList codes;
  for (const auto& language : manager.availableLanguages()) {
    if (language.code != QStringLiteral("system")) codes.append(language.code);
  }
  codes.append({QStringLiteral("zh-HK"), QStringLiteral("pt-BR"), QStringLiteral("system")});
  for (const QString& code : codes) {
    require(manager.setLanguage(code), QStringLiteral("Language must load: %1").arg(code));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
    app.processEvents();
    require(manager.currentLanguageCode() == code, QStringLiteral("Settings must retain the selected language"));
    require(manager.effectiveLanguageCode() != QStringLiteral("system"), QStringLiteral("Effective language must resolve system"));
    if (code == QStringLiteral("zh-HK")) {
      require(manager.effectiveLanguageCode() == QStringLiteral("zh_TW"), QStringLiteral("Hong Kong must select Traditional Chinese resources"));
    }
    require(rendered.accessibleName() == translated("muffin::EditorView", "Markdown editor"),
            QStringLiteral("Rendered editor accessible name must follow language changes"));
    require(rendered.accessibleDescription() == translated("muffin::EditorView", "Rendered Markdown document"),
            QStringLiteral("Rendered editor description must follow language changes"));
    require(source.accessibleName() == translated("muffin::VirtualSourceEdit", "Markdown source editor"),
            QStringLiteral("Source editor accessible name must follow language changes"));
    require(source.accessibleDescription() == translated("muffin::VirtualSourceEdit", "Plain-text Markdown source"),
            QStringLiteral("Source editor description must follow language changes"));

    bool found = false;
    for (QWidget* window : QApplication::topLevelWidgets()) {
      auto* help = qobject_cast<muffin::HelpViewerDialog*>(window);
      if (!help) continue;
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
  require(error == translated("muffin::FilePathOps", "file already exists") && error != QStringLiteral("file already exists"),
          QStringLiteral("File operation error detail must be translated"));
  require(!muffin::FilePathOps::revealPathInManager(settingsDir.filePath(QStringLiteral("missing.md")), &error),
          QStringLiteral("A missing path must be rejected before opening the file manager"));
  require(error == translated("muffin::FilePathOps", "path does not exist") && error != QStringLiteral("path does not exist"),
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

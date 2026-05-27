#include "FileManager.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace Muffin {

FileManager::FileManager(QObject* parent) : QObject(parent) {}

QString FileManager::readFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    file.close();
    return content;
}

bool FileManager::writeFile(const QString& filePath, const QString& content) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    file.close();
    return true;
}

bool FileManager::isMarkdownFile(const QString& filePath) {
    static const QStringList extensions = {
        "md", "markdown", "mdown", "mkd", "mkdn", "mdwn", "mdtxt", "mdtext"
    };
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return extensions.contains(suffix);
}

} // namespace Muffin

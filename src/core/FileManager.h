#pragma once
#include <QString>
#include <QObject>

class QFile;

namespace Muffin {

class FileManager : public QObject {
    Q_OBJECT

public:
    explicit FileManager(QObject* parent = nullptr);

    QString readFile(const QString& filePath);
    bool writeFile(const QString& filePath, const QString& content);

    static bool isMarkdownFile(const QString& filePath);

private:
};

} // namespace Muffin

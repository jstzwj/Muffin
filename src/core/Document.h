#pragma once
#include "parser/AstTree.h"
#include "renderer/RenderSourceMap.h"
#include "theme/Theme.h"
#include <QTextDocument>
#include <QString>
#include <memory>

namespace Muffin {

class Document : public QObject {
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);

    QString markdown() const { return m_markdown; }
    QTextDocument* textDocument() const { return m_textDocument.get(); }
    const AstTree& astTree() const { return m_astTree; }
    const RenderSourceMap& sourceMap() const { return m_sourceMap; }

    void setMarkdown(const QString& markdown);
    void setTheme(const Theme& theme);
    const Theme& theme() const { return m_theme; }
    void setFilePath(const QString& path) { m_filePath = path; }
    QString filePath() const { return m_filePath; }

    bool isModified() const;

signals:
    void markdownChanged();
    void documentRendered();

private:
    void render();

    QString m_markdown;
    AstTree m_astTree;
    std::unique_ptr<QTextDocument> m_textDocument;
    RenderSourceMap m_sourceMap;
    QString m_filePath;
    Theme m_theme = Theme::preset(ThemePreset::Github);
};

} // namespace Muffin

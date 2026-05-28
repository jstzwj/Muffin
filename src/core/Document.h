#pragma once
#include "parser/AstTree.h"
#include "parser/MathSpan.h"
#include "renderer/MarkdownBlock.h"
#include "renderer/RenderSourceMap.h"
#include "renderer/SyntaxTokenSpan.h"
#include "theme/Theme.h"
#include <QTextDocument>
#include <QUndoStack>
#include <QString>
#include <memory>

namespace Muffin {

namespace Internal {
class MarkdownEditCommand;
}

class Document : public QObject {
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);

    QString markdown() const { return m_markdown; }
    QTextDocument* textDocument() const { return m_textDocument.get(); }
    const AstTree& astTree() const { return m_astTree; }
    const QVector<MathSpan>& mathSpans() const { return m_mathSpans; }
    const RenderSourceMap& sourceMap() const { return m_sourceMap; }
    const QVector<MarkdownBlock>& blocks() const { return m_blocks; }
    const QVector<SyntaxTokenSpan>& syntaxTokens() const { return m_syntaxTokens; }

    void setMarkdown(const QString& markdown);
    void applyMarkdownEdit(const QString& markdown, int cursorSourceOffset, const QString& label);
    QUndoStack* undoStack() { return &m_undoStack; }
    const QUndoStack* undoStack() const { return &m_undoStack; }
    void setTheme(const Theme& theme);
    const Theme& theme() const { return m_theme; }
    void setFilePath(const QString& path) { m_filePath = path; }
    QString filePath() const { return m_filePath; }

    bool isModified() const;

signals:
    void markdownChanged();
    void documentRendered();
    void cursorSourceOffsetRequested(int offset);

private:
    friend class Internal::MarkdownEditCommand;

    void setMarkdownInternal(const QString& markdown, int cursorSourceOffset = -1);
    void render();

    QString m_markdown;
    AstTree m_astTree;
    QVector<MathSpan> m_mathSpans;
    std::unique_ptr<QTextDocument> m_textDocument;
    RenderSourceMap m_sourceMap;
    QVector<MarkdownBlock> m_blocks;
    QVector<SyntaxTokenSpan> m_syntaxTokens;
    QUndoStack m_undoStack;
    QString m_filePath;
    Theme m_theme = Theme::preset(ThemePreset::Github);
};

} // namespace Muffin

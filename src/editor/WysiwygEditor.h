#pragma once

#include <QTextEdit>
#include <memory>

namespace Md {

class MdDocument;
class DocumentRenderer;
class Theme;

class WysiwygEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit WysiwygEditor(QWidget *parent = nullptr);
    ~WysiwygEditor();

    bool loadMarkdown(const QString &markdown);
    void setTheme(const Theme &theme);

private:
    void resizeEvent(QResizeEvent *event) override;
    void updateDocumentWidth();
    void onDocumentReset();

    std::unique_ptr<MdDocument> m_doc;
    std::unique_ptr<DocumentRenderer> m_renderer;
};

} // namespace Md

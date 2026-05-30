#pragma once

#include "Theme.h"
#include <QTextDocument>
#include <memory>

namespace Md {

class MdNode;

class DocumentRenderer {
public:
    DocumentRenderer();

    void renderDocument(const MdNode *root, QTextDocument *target);
    void setTheme(const Theme &theme);

private:
    Theme m_theme;
};

} // namespace Md

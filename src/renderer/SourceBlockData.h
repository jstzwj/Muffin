#pragma once

#include "parser/AstNode.h"
#include <QTextBlockUserData>

namespace Muffin {

class SourceBlockData : public QTextBlockUserData
{
public:
    explicit SourceBlockData(SourceRange range)
        : m_range(range)
    {
    }

    SourceRange range() const { return m_range; }
    bool isValid() const { return m_range.startLine > 0; }

private:
    SourceRange m_range;
};

} // namespace Muffin

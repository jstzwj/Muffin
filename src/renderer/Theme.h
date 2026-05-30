#pragma once

#include <QString>
#include <QFont>
#include <QColor>

namespace Md {

class Theme {
public:
    Theme();

    QString name() const { return m_name; }

    QFont bodyFont() const { return m_bodyFont; }
    QFont headingFont() const { return m_headingFont; }
    QFont codeFont() const { return m_codeFont; }

    QColor textColor() const { return m_textColor; }
    QColor backgroundColor() const { return m_bgColor; }
    QColor linkColor() const { return m_linkColor; }
    QColor codeBgColor() const { return m_codeBgColor; }
    QColor metaColor() const { return m_metaColor; }

    int bodyFontSize() const { return m_bodyFontSize; }

private:
    QString m_name;
    QFont m_bodyFont;
    QFont m_headingFont;
    QFont m_codeFont;
    QColor m_textColor;
    QColor m_bgColor;
    QColor m_linkColor;
    QColor m_codeBgColor;
    QColor m_metaColor;
    int m_bodyFontSize = 14;
};

} // namespace Md

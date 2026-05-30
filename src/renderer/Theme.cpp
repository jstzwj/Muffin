#include "Theme.h"

namespace Md {

Theme::Theme() {
    m_name = "Default Light";

    m_bodyFont.setFamily("Segoe UI");
    m_bodyFont.setPointSize(m_bodyFontSize);

    m_headingFont.setFamily("Segoe UI");
    m_headingFont.setBold(true);

    m_codeFont.setFamily("Consolas");
    m_codeFont.setPointSize(m_bodyFontSize - 1);

    m_textColor = QColor("#24292e");
    m_bgColor = QColor("#ffffff");
    m_linkColor = QColor("#0366d6");
    m_codeBgColor = QColor("#f6f8fa");
    m_metaColor = QColor("#959da5");
}

} // namespace Md

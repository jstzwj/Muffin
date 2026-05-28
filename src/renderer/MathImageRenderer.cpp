#include "MathImageRenderer.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100 4267 4996)
#endif
#include "latex.h"
#include "platform/qt/graphic_qt.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>
#include <memory>
#include <mutex>
#include <string>

namespace Muffin {
namespace {

QString microTeXResourcePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::cleanPath(appDir + QStringLiteral("/third_party/MicroTeX/res")),
        QDir::cleanPath(appDir + QStringLiteral("/../third_party/MicroTeX/res")),
        QDir::cleanPath(QDir::currentPath() + QStringLiteral("/third_party/MicroTeX/res")),
        QDir::cleanPath(QDir::currentPath() + QStringLiteral("/../../third_party/MicroTeX/res"))
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate + QStringLiteral("/.clatexmath-res_root"))) {
            return candidate;
        }
    }

    return QStringLiteral("res");
}

void initializeMicroTeX()
{
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        const QString resourcePath = microTeXResourcePath();
        tex::LaTeX::init(QDir::toNativeSeparators(resourcePath).toStdString());
        tex::LaTeX::setDebug(false);
    });
}

tex::color toMicroTeXColor(const QColor& color)
{
    return tex::argb(color.alpha(), color.red(), color.green(), color.blue());
}

QImage renderFallback(const MathSpan& span, const Theme& theme)
{
    QFont font = theme.bodyFont;
    font.setItalic(true);
    font.setPointSizeF(font.pointSizeF() * (span.display ? 1.25 : 1.0));

    const QString text = span.tex.trimmed().isEmpty() ? QStringLiteral(" ") : span.tex.trimmed();
    const QFontMetricsF metrics(font);
    const qreal paddingX = span.display ? 18.0 : 4.0;
    const qreal paddingY = span.display ? 10.0 : 2.0;
    const QSizeF textSize = metrics.boundingRect(text).size();
    const int width = qCeil(textSize.width() + paddingX * 2.0);
    const int height = qCeil(qMax(textSize.height(), metrics.height()) + paddingY * 2.0);

    QImage image(QSize(qMax(width, 1), qMax(height, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(theme.foreground);
    painter.drawText(QRectF(paddingX, paddingY, textSize.width(), metrics.height()), Qt::AlignLeft | Qt::AlignVCenter, text);
    return image;
}

} // namespace

MathImageRenderer::MathImageRenderer(const Theme& theme)
    : m_theme(theme)
{
}

QImage MathImageRenderer::render(const MathSpan& span) const
{
    const QString texSource = span.tex.trimmed();
    if (texSource.isEmpty()) {
        return renderFallback(span, m_theme);
    }

    try {
        initializeMicroTeX();

        const float textSize = static_cast<float>(qMax<qreal>(m_theme.bodyFont.pointSizeF(), 1.0) * (span.display ? 1.35 : 1.0));
        const int maxWidth = span.display ? 1000 : 600;
        std::wstring formulaText;
        formulaText.reserve(static_cast<size_t>(texSource.size()));
        for (const QChar ch : texSource) {
            formulaText.push_back(static_cast<wchar_t>(ch.unicode()));
        }
        std::unique_ptr<tex::TeXRender> render(tex::LaTeX::parse(
            formulaText.c_str(),
            formulaText.size(),
            maxWidth,
            textSize,
            textSize / 3.0f,
            toMicroTeXColor(m_theme.foreground)));

        if (!render || render->getWidth() <= 0 || render->getHeight() <= 0) {
            return renderFallback(span, m_theme);
        }

        const int padding = span.display ? 8 : 2;
        QImage image(QSize(render->getWidth() + padding * 2, render->getHeight() + padding * 2),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        tex::Graphics2D_qt graphics(&painter);
        render->draw(graphics, padding, padding);
        return image;
    } catch (...) {
        return renderFallback(span, m_theme);
    }
}

} // namespace Muffin

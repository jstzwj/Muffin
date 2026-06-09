#include "html/HtmlRenderer.h"
#include "html/HtmlBoxBuilder.h"
#include "html/HtmlParser.h"
#include "html/HtmlStyleResolver.h"
#include "html/HtmlLayoutEngine.h"
#include "html/HtmlLayoutResult.h"

#include <QFontMetricsF>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <utility>

namespace muffin::html {
namespace {

QString resolvedImageSource(const QString& src, const QString& baseDirectory) {
  const QFileInfo info(src);
  if (info.isAbsolute()) {
    return info.absoluteFilePath();
  }

  const QUrl url(src);
  if (url.isLocalFile()) {
    return QFileInfo(url.toLocalFile()).absoluteFilePath();
  }
  if (url.isValid() && !url.scheme().isEmpty()) {
    return src;
  }

  if (!baseDirectory.isEmpty()) {
    return QFileInfo(QDir(baseDirectory).absoluteFilePath(src)).absoluteFilePath();
  }
  return src;
}

void resolveResourcePaths(HtmlBox& box, const QString& baseDirectory) {
  if (box.tag() == HtmlTag::Image && !box.src().isEmpty()) {
    const QString resolved = resolvedImageSource(box.src(), baseDirectory);
    if (resolved != box.src()) {
      box.setSrc(resolved);
    }
  }

  for (auto& child : box.children()) {
    resolveResourcePaths(*child, baseDirectory);
  }
}

}  // namespace

HtmlRenderer::HtmlRenderer() = default;
HtmlRenderer::~HtmlRenderer() = default;

HtmlLayoutResult HtmlRenderer::render(const QString& html, qreal baseFontSize, qreal availableWidth, QString baseDirectory) {
  HtmlLayoutResult result;
  result.setBaseDirectory(std::move(baseDirectory));

  if (html.trimmed().isEmpty()) {
    result.setError(QStringLiteral("Empty HTML content"));
    return result;
  }

  // 1. Parse HTML
  HtmlDocument document;
  if (!document.parse(html)) {
    result.setError(document.errorString());
    return result;
  }

  // 2. Build box tree from DOM
  HtmlBoxBuilder boxBuilder;
  auto root = boxBuilder.build(document);
  if (!root) {
    result.setError(QStringLiteral("Failed to build box tree"));
    return result;
  }
  resolveResourcePaths(*root, result.baseDirectory());

  // 3. Resolve styles
  HtmlStyleResolver styleResolver;
  styleResolver.resolve(*root, baseFontSize, availableWidth);

  // 4. Run layout
  HtmlLayoutEngine layoutEngine;
  std::vector<std::unique_ptr<HtmlTextLayout>> textLayouts;
  layoutEngine.layout(*root, availableWidth, baseFontSize, textLayouts);

  // 5. Compute total size
  const auto& geo = root->geometry();
  QSizeF totalSize(geo.width, geo.height);

  result.setRoot(std::move(root));
  result.setTextLayouts(std::move(textLayouts));
  result.setSize(totalSize);
  return result;
}

}  // namespace muffin::html

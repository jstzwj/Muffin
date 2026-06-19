#include "blocks/html/HtmlSanitizer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include <cstdlib>

// Verifies HtmlSanitizer's URL policy: Windows drive-letter paths
// (C:/... / C:\...) must survive (they look like a one-letter URL scheme "c:"
// but are local file paths), relative/http URLs survive, and dangerous schemes
// (javascript:) are still neutralized to "#". Follows the project test convention.

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

const muffin::HtmlSanitizer kSanitizer;

// The src attribute value in `html`, or "NOT FOUND".
QString srcValue(const QString& html) {
  const int start = html.indexOf(QStringLiteral("src=\""));
  if (start < 0) {
    return QStringLiteral("NOT FOUND");
  }
  const int valStart = start + 5;
  const int end = html.indexOf(QLatin1Char('"'), valStart);
  if (end < 0) {
    return QStringLiteral("NOT FOUND");
  }
  return html.mid(valStart, end - valStart);
}

void testWindowsForwardSlashPathSurvives() {
  const QString in = QStringLiteral("<img src=\"C:/Users/me/logo.svg\" alt=\"logo\">");
  const QString out = kSanitizer.sanitizedPreview(in);
  require(srcValue(out) == QStringLiteral("C:/Users/me/logo.svg"),
          QStringLiteral("C:/... drive path should survive, got: %1").arg(srcValue(out)));
}

void testWindowsBackslashPathSurvives() {
  const QString in = QStringLiteral("<img src=\"D:\\images\\pic.png\">");
  const QString out = kSanitizer.sanitizedPreview(in);
  require(srcValue(out) == QStringLiteral("D:\\images\\pic.png"),
          QStringLiteral("D:\\... drive path should survive, got: %1").arg(srcValue(out)));
}

void testRelativePathSurvives() {
  const QString in = QStringLiteral("<img src=\"assets/pic.png\">");
  const QString out = kSanitizer.sanitizedPreview(in);
  require(srcValue(out) == QStringLiteral("assets/pic.png"),
          QStringLiteral("relative path should survive, got: %1").arg(srcValue(out)));
}

void testHttpsUrlSurvives() {
  const QString in = QStringLiteral("<img src=\"https://example.com/a.png\">");
  const QString out = kSanitizer.sanitizedPreview(in);
  require(srcValue(out) == QStringLiteral("https://example.com/a.png"),
          QStringLiteral("https URL should survive, got: %1").arg(srcValue(out)));
}

void testJavascriptSchemeStillNeutralized() {
  const QString in = QStringLiteral("<img src=\"javascript:alert(1)\">");
  const QString out = kSanitizer.sanitizedPreview(in);
  require(srcValue(out) == QStringLiteral("#"),
          QStringLiteral("javascript: must still be neutralized to #, got: %1").arg(srcValue(out)));
}

void testDataSvgStillBlocked() {
  const QString in = QStringLiteral("<img src=\"data:image/svg+xml,<svg></svg>\">");
  const QString out = kSanitizer.sanitizedPreview(in);
  require(srcValue(out) == QStringLiteral("#"),
          QStringLiteral("data:image/svg must still be blocked, got: %1").arg(srcValue(out)));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("MuffinTests"));
  testWindowsForwardSlashPathSurvives();
  testWindowsBackslashPathSurvives();
  testRelativePathSurvives();
  testHttpsUrlSurvives();
  testJavascriptSchemeStillNeutralized();
  testDataSvgStillBlocked();
  return 0;
}

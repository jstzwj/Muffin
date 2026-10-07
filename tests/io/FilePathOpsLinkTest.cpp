#include "io/FilePathOps.h"
#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "editor/ResourceUrl.h"

#include "../TestUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

using namespace muffin;

void requireEq(const QString& actual, const QString& expected, const QString& message) {
  require(actual == expected,
          QStringLiteral("%1\n   expected: %2\n   actual:   %3").arg(message, expected, actual));
}

// linkTargetForPath resolves relative to documentDir when the file lives inside it (a direct
// child or in a subdir), and falls back to an absolute path otherwise. Separators are native
// (backslash on Windows), so expected values are built with QDir::toNativeSeparators to keep
// the test cross-platform. Paths need not exist on disk — QDir/QFileInfo operate on strings.
void testRelativeInsideDocumentDir() {
  const QString base = QDir::tempPath();  // absolute, platform-appropriate root
  const QString docDir = base + QStringLiteral("/proj");
  const QString file = docDir + QStringLiteral("/note.md");

  const QString target = FilePathOps::linkTargetForPath(file, docDir);
  requireEq(target, QStringLiteral("note.md"),
            QStringLiteral("sibling file in documentDir → bare filename"));
}

void testRelativeInSubdirectory() {
  const QString base = QDir::tempPath();
  const QString docDir = base + QStringLiteral("/proj");
  const QString file = docDir + QStringLiteral("/sub/示例.md");

  const QString target = FilePathOps::linkTargetForPath(file, docDir);
  requireEq(target, QDir::toNativeSeparators(QStringLiteral("sub/示例.md")),
            QStringLiteral("file in a subdir → relative subdir path, native separators"));
}

void testParentEscapeFallsBackToAbsolute() {
  const QString base = QDir::tempPath();
  const QString docDir = base + QStringLiteral("/proj");
  const QString file = base + QStringLiteral("/note.md");  // in the parent of docDir

  const QString target = FilePathOps::linkTargetForPath(file, docDir);
  requireEq(target, QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath()),
            QStringLiteral("file escaping documentDir upward (../) → absolute fallback"));
}

void testEmptyDocumentDirIsAbsolute() {
  const QString base = QDir::tempPath();
  const QString file = base + QStringLiteral("/note.md");

  const QString target = FilePathOps::linkTargetForPath(file, QString());
  requireEq(target, QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath()),
            QStringLiteral("empty documentDir → absolute path"));
}

#ifdef Q_OS_WIN
void testDifferentDriveFallsBackToAbsolute() {
  // relativeFilePath returns an absolute path when the file is on a different drive than
  // documentDir; the isAbsolute guard must catch that and not emit a "relative" path.
  const QString docDir = QStringLiteral("C:/proj");
  const QString file = QStringLiteral("D:/elsewhere/note.md");

  const QString target = FilePathOps::linkTargetForPath(file, docDir);
  requireEq(target, QStringLiteral("D:\\elsewhere\\note.md"),
            QStringLiteral("cross-drive → absolute, native separators"));
}
#endif

void testMarkdownLinkLabelAndTarget() {
  const QString base = QDir::tempPath();
  const QString docDir = base + QStringLiteral("/proj");
  const QString file = docDir + QStringLiteral("/read me.pdf");

  const QString link = FilePathOps::markdownLinkForFile(file, docDir);
  requireEq(link, QStringLiteral("[read me.pdf](<read me.pdf>)"),
            QStringLiteral("Spaces in a Markdown destination require angle brackets"));
}

void testMarkdownLinkAbsoluteFallback() {
  const QString base = QDir::tempPath();
  const QString docDir = base + QStringLiteral("/proj");
  const QString file = base + QStringLiteral("/up here.md");

  const QString link = FilePathOps::markdownLinkForFile(file, docDir);
  const QString expected =
      QStringLiteral("[up here.md](<%1>)").arg(QDir::fromNativeSeparators(QFileInfo(file).absoluteFilePath()));
  requireEq(link, expected,
            QStringLiteral("markdownLinkForFile absolute fallback wraps absolute target"));
}

const InlineNode* findLink(const MarkdownNode& node) {
  for (const auto& in : node.inlines()) {
    if (in.type() == InlineType::Link) return &in;
  }
  for (const auto& child : node.children()) {
    if (const InlineNode* link = findLink(*child)) return link;
  }
  return nullptr;
}

void testGeneratedLinksParseAndResolve() {
  const QString docDir = QDir::tempPath() + QStringLiteral("/muffin-link-doc");
  for (const QString& name : {QStringLiteral("read me.pdf"), QStringLiteral("x](y)[z].pdf"),
                             QStringLiteral("literal%20#&amp;.pdf"), QStringLiteral("nested/示例 (1).md")}) {
    const QString path = QDir(docDir).filePath(name);
    DocumentSession session;
    session.setMarkdownText(FilePathOps::markdownLinkForFile(path, docDir), false);
    const InlineNode* link = findLink(session.document().root());
    require(link, QStringLiteral("Generated link must parse: %1").arg(name));
    requireEq(resolvedUrlForDocumentResource(link->href(), QDir(docDir).filePath(QStringLiteral("doc.md"))).toLocalFile(),
              QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()),
              QStringLiteral("Parsed destination must name the original file"));
    QString label;
    for (const auto& child : link->children()) label += child.text();
    requireEq(label, QFileInfo(path).fileName(), QStringLiteral("Link label must preserve the literal filename"));
  }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testRelativeInsideDocumentDir();
  testRelativeInSubdirectory();
  testParentEscapeFallsBackToAbsolute();
  testEmptyDocumentDirIsAbsolute();
#ifdef Q_OS_WIN
  testDifferentDriveFallsBackToAbsolute();
#endif
  testMarkdownLinkLabelAndTarget();
  testMarkdownLinkAbsoluteFallback();
  testGeneratedLinksParseAndResolve();
  return 0;
}

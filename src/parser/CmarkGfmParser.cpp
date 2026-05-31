#include "parser/CmarkGfmParser.h"

#include "document/InlineNode.h"
#include "parser/CmarkNodeAdapter.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QStringList>

extern "C" {
#include "cmark-gfm-core-extensions.h"
}

namespace muffin {

CmarkGfmParser::CmarkGfmParser() {
  ensureExtensionsRegistered();
}

ParseResult CmarkGfmParser::parseDocument(QStringView markdown, const ParseOptions& options) {
  QElapsedTimer timer;
  timer.start();

  const QByteArray utf8 = markdown.toString().toUtf8();
  cmark_parser* parser = cmark_parser_new(CMARK_OPT_DEFAULT | CMARK_OPT_FOOTNOTES);
  attachExtensions(parser, options);
  cmark_parser_feed(parser, utf8.constData(), static_cast<size_t>(utf8.size()));
  cmark_node* document = cmark_parser_finish(parser);

  CmarkNodeAdapter adapter;
  ParseResult result;
  result.root = adapter.convertBlock(document);
  insertVirtualEmptyParagraphs(markdown, *result.root);
  result.elapsedMs = timer.elapsed();

  cmark_node_free(document);
  cmark_parser_free(parser);
  return result;
}

ParseResult CmarkGfmParser::parseBlock(QStringView markdown, BlockType, const ParseOptions& options) {
  return parseDocument(markdown, options);
}

void CmarkGfmParser::ensureExtensionsRegistered() {
  cmark_gfm_core_extensions_ensure_registered();
}

void CmarkGfmParser::attachExtensions(cmark_parser* parser, const ParseOptions& options) {
  const auto attach = [parser](const char* name) {
    if (cmark_syntax_extension* extension = cmark_find_syntax_extension(name)) {
      cmark_parser_attach_syntax_extension(parser, extension);
    }
  };

  if (options.enableTable) attach("table");
  if (options.enableStrikethrough) attach("strikethrough");
  if (options.enableAutolink) attach("autolink");
  if (options.enableTaskList) attach("tasklist");
  attach("math");
}

void CmarkGfmParser::insertVirtualEmptyParagraphs(QStringView markdown, MarkdownNode& root) const {
  if (root.type() != BlockType::Document || markdown.isEmpty()) {
    return;
  }

  const QString text = markdown.toString();
  const QStringList lines = text.split(QLatin1Char('\n'));
  qsizetype childIndex = 0;
  int previousEndLine = 0;

  while (childIndex < root.children().size()) {
    MarkdownNode* child = root.children().at(static_cast<size_t>(childIndex)).get();
    const SourceRange range = child->sourceRange();
    const int startLine = range.lineStart;
    if (startLine > 0 && startLine - previousEndLine >= 3) {
      const int emptyLine = qMax(1, startLine - 2);
      const int lineIndex = emptyLine - 1;
      if (lineIndex >= 0 && lineIndex < lines.size() && lines.at(lineIndex).trimmed().isEmpty()) {
        root.insertChild(childIndex, createVirtualEmptyParagraph(emptyLine));
        previousEndLine = emptyLine;
        ++childIndex;
        continue;
      }
    }

    previousEndLine = qMax(previousEndLine, range.lineEnd);
    ++childIndex;
  }

  const int totalLines = lines.size();
  if (totalLines - previousEndLine >= 2) {
    const int emptyLine = qMax(1, previousEndLine + 2);
    const int lineIndex = emptyLine - 1;
    if (lineIndex >= 0 && lineIndex < lines.size() && lines.at(lineIndex).trimmed().isEmpty()) {
      root.appendChild(createVirtualEmptyParagraph(emptyLine));
    }
  }
}

std::unique_ptr<MarkdownNode> CmarkGfmParser::createVirtualEmptyParagraph(int line) const {
  auto paragraph = std::make_unique<MarkdownNode>(BlockType::Paragraph);
  paragraph->inlines().push_back(InlineNode::text(QString()));

  SourceRange range;
  range.lineStart = line;
  range.lineEnd = line;
  range.columnStart = 1;
  range.columnEnd = 1;
  paragraph->setSourceRange(range);
  return paragraph;
}

}  // namespace muffin

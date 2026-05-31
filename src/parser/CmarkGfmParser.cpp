#include "parser/CmarkGfmParser.h"

#include "parser/CmarkNodeAdapter.h"

#include <QByteArray>
#include <QElapsedTimer>

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
}

}  // namespace muffin

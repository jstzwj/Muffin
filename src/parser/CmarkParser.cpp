#include "CmarkParser.h"
#include "MathDelimiterScanner.h"
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <extensions/strikethrough.h>
#include <extensions/table.h>
#include <extensions/autolink.h>
#include <extensions/tasklist.h>

namespace Muffin {

bool CmarkParser::s_extensionsRegistered = false;

CmarkParser::CmarkParser() {
    ensureExtensionsRegistered();
}

CmarkParser::~CmarkParser() = default;

void CmarkParser::ensureExtensionsRegistered() {
    if (s_extensionsRegistered) return;
    cmark_gfm_core_extensions_ensure_registered();
    s_extensionsRegistered = true;
}

AstTree CmarkParser::parse(const QString& markdown) {
    return parse(markdown.toUtf8());
}

AstTree CmarkParser::parse(const QByteArray& utf8Data) {
    int options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE;

    cmark_parser* parser = cmark_parser_new(options);

    // Register GFM extensions
    cmark_syntax_extension* ext;
    const char* extNames[] = {"table", "strikethrough", "autolink", "tasklist"};
    for (auto name : extNames) {
        ext = cmark_find_syntax_extension(name);
        if (ext) cmark_parser_attach_syntax_extension(parser, ext);
    }

    cmark_parser_feed(parser, utf8Data.constData(), utf8Data.size());
    cmark_node* root = cmark_parser_finish(parser);
    cmark_parser_free(parser);

    return AstTree(root);
}

ParseResult CmarkParser::parseDocument(const QString& markdown)
{
    ParseResult result;
    result.ast = parse(markdown);
    result.mathSpans = MathDelimiterScanner::scan(markdown, result.ast);
    result.document = MarkdownDocument::fromAst(result.ast, markdown, result.mathSpans);
    return result;
}

ParseResult CmarkParser::parseDocument(const QString& markdown, const MarkdownDocument& previousDocument)
{
    ParseResult result;
    result.ast = parse(markdown);
    result.mathSpans = MathDelimiterScanner::scan(markdown, result.ast);
    result.document = MarkdownDocument::fromAst(result.ast, markdown, result.mathSpans, previousDocument);
    return result;
}

} // namespace Muffin

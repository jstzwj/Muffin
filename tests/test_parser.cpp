#include <QtTest>
#include "core/MdParser.h"
#include "core/MdSerializer.h"
#include "core/MdNode.h"

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm-core-extensions.h>
extern "C" {
#include <registry.h>
}

using namespace Md;

class TestParser : public QObject {
    Q_OBJECT

private slots:
    void testParseParagraph() {
        MdParser parser;
        auto root = parser.parse("Hello world");
        QVERIFY(root != nullptr);
        QCOMPARE(root->blockType(), BlockType::Document);
    }

    void testParseHeading() {
        MdParser parser;
        auto root = parser.parse("# Heading 1\n## Heading 2");
        QVERIFY(root != nullptr);
    }

    void testParseList() {
        MdParser parser;
        auto root = parser.parse("- item 1\n- item 2\n- item 3");
        QVERIFY(root != nullptr);
    }

    void testParseCodeBlock() {
        MdParser parser;
        auto root = parser.parse("```cpp\nint x = 1;\n```");
        QVERIFY(root != nullptr);
    }

    void testParseTable() {
        MdParser parser;
        auto root = parser.parse("| A | B |\n|---|---|\n| 1 | 2 |");
        QVERIFY(root != nullptr);
    }

    void testParseInlineMath() {
        MdParser parser;
        auto root = parser.parse("The formula $x^2 + y^2 = z^2$ is Pythagorean.");
        QVERIFY(root != nullptr);
    }

    void testParseDisplayMath() {
        MdParser parser;
        // Single-line display math
        auto root = parser.parse("$$E=mc^2$$");
        QVERIFY(root != nullptr);
    }

    void testParseMultiLineDisplayMath() {
        MdParser parser;
        auto root = parser.parse("$$\nx^2 + y^2 = z^2\n$$");
        QVERIFY(root != nullptr);
    }

    void testParseFencedMath() {
        MdParser parser;
        auto root = parser.parse("```math\nE=mc^2\n```");
        QVERIFY(root != nullptr);
    }

    void testRawCmarkDisplayMath() {
        // Direct cmark-gfm test with $$ block math
        cmark_gfm_core_extensions_ensure_registered();
        int options = CMARK_OPT_DEFAULT;
        cmark_parser *parser = cmark_parser_new(options);

        cmark_llist *extensions = cmark_list_syntax_extensions(cmark_get_default_mem_allocator());
        for (cmark_llist *it = extensions; it; it = it->next) {
            cmark_parser_attach_syntax_extension(parser,
                static_cast<cmark_syntax_extension *>(it->data));
        }
        cmark_llist_free(cmark_get_default_mem_allocator(), extensions);

        cmark_parser_feed(parser, "$$E=mc^2$$\n", 11);
        cmark_node *root = cmark_parser_finish(parser);

        QVERIFY(root != nullptr);
        cmark_parser_free(parser);
    }

    void testRoundtripParagraph() {
        QString md = "Hello world\n";
        MdParser parser;
        auto root = parser.parse(md);
        QVERIFY(root != nullptr);
        QString result = MdSerializer::toMarkdown(root.get());
        QCOMPARE(result.trimmed(), md.trimmed());
    }

    void testRoundtripHeading() {
        QString md = "# Hello\n";
        MdParser parser;
        auto root = parser.parse(md);
        QVERIFY(root != nullptr);
        QString result = MdSerializer::toMarkdown(root.get());
        QCOMPARE(result.trimmed(), md.trimmed());
    }
};

QTEST_MAIN(TestParser)
// To run without GUI: set QT_QPA_PLATFORM=offscreen
#include "test_parser.moc"

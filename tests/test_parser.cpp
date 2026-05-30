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
        auto result = parser.parse("Hello world");
        QVERIFY(result.mdRoot != nullptr);
        QCOMPARE(result.mdRoot->blockType(), BlockType::Document);
        QVERIFY(result.cmarkRoot != nullptr);
    }

    void testParseHeading() {
        MdParser parser;
        auto result = parser.parse("# Heading 1\n## Heading 2");
        QVERIFY(result.mdRoot != nullptr);
    }

    void testParseList() {
        MdParser parser;
        auto result = parser.parse("- item 1\n- item 2\n- item 3");
        QVERIFY(result.mdRoot != nullptr);
    }

    void testParseCodeBlock() {
        MdParser parser;
        auto result = parser.parse("```cpp\nint x = 1;\n```");
        QVERIFY(result.mdRoot != nullptr);
    }

    void testParseTable() {
        MdParser parser;
        auto result = parser.parse("| A | B |\n|---|---|\n| 1 | 2 |");
        QVERIFY(result.mdRoot != nullptr);
    }

    void testParseInlineMath() {
        MdParser parser;
        auto result = parser.parse("The formula $x^2 + y^2 = z^2$ is Pythagorean.");
        QVERIFY(result.mdRoot != nullptr);
    }

    void testParseDisplayMath() {
        MdParser parser;
        auto result = parser.parse("$$E=mc^2$$");
        QVERIFY(result.mdRoot != nullptr);
    }

    void testParseMultiLineDisplayMath() {
        MdParser parser;
        auto result = parser.parse("$$\nx^2 + y^2 = z^2\n$$");
        QVERIFY(result.mdRoot != nullptr);
    }

    void testParseFencedMath() {
        MdParser parser;
        auto result = parser.parse("```math\nE=mc^2\n```");
        QVERIFY(result.mdRoot != nullptr);
    }

    void testRoundtripParagraph() {
        QString md = "Hello world\n";
        MdParser parser;
        auto result = parser.parse(md);
        QVERIFY(result.cmarkRoot != nullptr);
        QString output = MdSerializer::toMarkdown(result.cmarkRoot);
        QCOMPARE(output.trimmed(), md.trimmed());
    }

    void testRoundtripHeading() {
        QString md = "# Hello\n";
        MdParser parser;
        auto result = parser.parse(md);
        QVERIFY(result.cmarkRoot != nullptr);
        QString output = MdSerializer::toMarkdown(result.cmarkRoot);
        QCOMPARE(output.trimmed(), md.trimmed());
    }
};

QTEST_MAIN(TestParser)
#include "test_parser.moc"

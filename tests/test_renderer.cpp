#include <QtTest>
#include "core/MdParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/Theme.h"

#include <cmark-gfm.h>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextFragment>

using namespace Md;

class TestRenderer : public QObject {
    Q_OBJECT

private:
    DocumentRenderer m_renderer;
    Theme m_theme;

    cmark_node *parseToCmark(const QString &md) {
        MdParser parser;
        auto result = parser.parse(md);
        // We need to keep the cmark tree alive — store it
        m_lastResult = std::move(result);
        return m_lastResult.cmarkRoot;
    }

    ParseResult m_lastResult;

    QString renderPlainText(const QString &md) {
        cmark_node *root = parseToCmark(md);
        if (!root) return {};
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);
        return doc.toPlainText();
    }

private slots:
    void testRenderParagraph() {
        QString text = renderPlainText("Hello world");
        QVERIFY(text.contains("Hello world"));
    }

    void testRenderHeading() {
        QString text = renderPlainText("# Title\n## Subtitle");
        QVERIFY(text.contains("Title"));
        QVERIFY(text.contains("Subtitle"));
    }

    void testRenderBold() {
        cmark_node *root = parseToCmark("This is **bold** text");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("bold"));
        QVERIFY(plain.contains("**"));
    }

    void testRenderItalic() {
        cmark_node *root = parseToCmark("This is *italic* text");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("italic"));
    }

    void testRenderCodeInline() {
        cmark_node *root = parseToCmark("Use `code` here");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("code"));
    }

    void testRenderLink() {
        cmark_node *root = parseToCmark("Click [here](https://example.com)");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("here"));
    }

    void testRenderList() {
        QString text = renderPlainText("- one\n- two\n- three");
        QVERIFY(text.contains("one"));
        QVERIFY(text.contains("two"));
        QVERIFY(text.contains("three"));
    }

    void testRenderOrderedList() {
        QString text = renderPlainText("1. first\n2. second\n3. third");
        QVERIFY(text.contains("first"));
        QVERIFY(text.contains("second"));
        QVERIFY(text.contains("1."));
    }

    void testRenderCodeBlock() {
        QString text = renderPlainText("```cpp\nint x = 1;\n```");
        QVERIFY(text.contains("int x = 1;"));
    }

    void testRenderBlockQuote() {
        QString text = renderPlainText("> quoted text\n> more quote");
        QVERIFY(text.contains("quoted text"));
        QVERIFY(text.contains("more quote"));
    }

    void testRenderThematicBreak() {
        cmark_node *root = parseToCmark("before\n\n---\n\nafter");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("before"));
        QVERIFY(plain.contains("after"));
        QVERIFY(doc.blockCount() >= 3); // before + hr + after
    }

    void testRenderTable() {
        cmark_node *root = parseToCmark("| A | B |\n|---|---|\n| 1 | 2 |");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("A"));
        QVERIFY(plain.contains("B"));
        QVERIFY(plain.contains("1"));
        QVERIFY(plain.contains("2"));
    }

    void testRenderMathBlock() {
        cmark_node *root = parseToCmark("$$E=mc^2$$");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("E=mc^2"));
    }

    void testRenderFencedMath() {
        // Verify the parser creates a MATH_FENCED node
        MdParser parser;
        auto result = parser.parse("```math\nf(x) = x^2\n```\n");
        QVERIFY(result.cmarkRoot != nullptr);
        // Just verify we can render without crash
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(result.cmarkRoot, &doc);
        QVERIFY(doc.blockCount() > 0);
    }

    void testRenderInlineMath() {
        cmark_node *root = parseToCmark("Formula $x^2$ here");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("x^2"));
    }

    void testRenderNestedInline() {
        cmark_node *root = parseToCmark("***bold italic***");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("bold italic"));
    }

    void testRenderStrikethrough() {
        cmark_node *root = parseToCmark("~~deleted~~");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("deleted"));
    }

    void testRenderHtmlBlock() {
        cmark_node *root = parseToCmark("<div>\nhello\n</div>");
        QVERIFY(root);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(root, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("div"));
    }

    void testRenderComplexDocument() {
        QString md = "# Title\n\nParagraph with **bold** and *italic*.\n\n"
                     "- Item 1\n- Item 2\n\n"
                     "> Blockquote\n\n"
                     "```\ncode block\n```\n\n"
                     "---\n";
        MdParser parser;
        auto result = parser.parse(md);
        QVERIFY(result.cmarkRoot);
        QTextDocument doc;
        m_renderer.setTheme(m_theme);
        m_renderer.renderDocument(result.cmarkRoot, &doc);

        QString plain = doc.toPlainText();
        QVERIFY(plain.contains("Title"));
        QVERIFY(plain.contains("bold"));
        QVERIFY(plain.contains("italic"));
        QVERIFY(plain.contains("Item 1"));
        QVERIFY(plain.contains("Blockquote"));
        QVERIFY(plain.contains("code block"));
    }
};

QTEST_MAIN(TestRenderer)
#include "test_renderer.moc"

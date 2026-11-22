#include "render/TreeSitterHighlighter.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

bool hasExactRoleSpan(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role, qsizetype start, qsizetype end) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role && span.start == start && span.end == end) {
      return true;
    }
  }
  return false;
}

struct HighlightFixture {
  QString language;
  QString code;
  QVector<CodeHighlightRole> roles;
};

void requireHighlightsFixture(const TreeSitterHighlighter& highlighter, const HighlightFixture& fixture) {
  require(highlighter.supportsLanguage(fixture.language),
          QStringLiteral("%1 highlighting should be registered").arg(fixture.language));
  const QVector<CodeHighlightSpan> spans = highlighter.highlight(fixture.language, fixture.code);
  require(!spans.isEmpty(), QStringLiteral("%1 should produce highlight spans").arg(fixture.language));
  for (CodeHighlightRole role : fixture.roles) {
    require(hasRole(spans, role),
            QStringLiteral("%1 should highlight requested role %2").arg(fixture.language).arg(static_cast<int>(role)));
  }
}

void testTreeSitterCodeHighlighting() {
  TreeSitterHighlighter highlighter;
  const QVector<HighlightFixture> fixtures{
      {QStringLiteral("mermaid"),
       QStringLiteral("flowchart LR\n%% entry\nA[\"Start\"] --> B\n"),
       {CodeHighlightRole::Type, CodeHighlightRole::Comment, CodeHighlightRole::Operator, CodeHighlightRole::String}},
      {QStringLiteral("mermaid"),
       QStringLiteral("sequenceDiagram\nparticipant Alice\nAlice->>Bob: Hello\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Variable, CodeHighlightRole::Operator,
        CodeHighlightRole::String}},
      {QStringLiteral("python"),
       QStringLiteral("def greet(name):\n    return \"hello \" + name\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("cpp"),
       QStringLiteral("#include <QApplication>\nint main() { return 0; }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("bash"),
       QStringLiteral("#!/usr/bin/env bash\nif [ -n \"$HOME\" ]; then echo \"ok\"; fi\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("c"),
       QStringLiteral("#include <stdio.h>\nint main(void) { return 0; }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Function}},
      {QStringLiteral("csharp"),
       QStringLiteral("public class App { string Name => \"muffin\"; }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("css"),
       QStringLiteral(".note { color: red; margin: 1px; }\n"),
       {CodeHighlightRole::Property, CodeHighlightRole::Number}},
      {QStringLiteral("go"),
       QStringLiteral("package main\nfunc main() { println(\"hi\") }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("html"),
       QStringLiteral("<section class=\"note\">hello</section>\n"),
       {CodeHighlightRole::Type, CodeHighlightRole::String}},
      {QStringLiteral("ini"), QStringLiteral("[core]\nname=muffin\n"), {CodeHighlightRole::Property}},
      {QStringLiteral("java"),
       QStringLiteral("class App { String name() { return \"muffin\"; } }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("javascript"),
       QStringLiteral("function greet(name) { return `hi ${name}`; }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("json"),
       QStringLiteral("{\"name\": \"muffin\", \"count\": 2}\n"),
       {CodeHighlightRole::String, CodeHighlightRole::Number}},
      {QStringLiteral("lua"),
       QStringLiteral("local name = \"muffin\"\nfunction greet() return name end\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("markdown"),
       QStringLiteral("# Title\n\n- `code` [link](https://example.com)\n"),
       {CodeHighlightRole::Type, CodeHighlightRole::Punctuation}},
      {QStringLiteral("objective-c"),
       QStringLiteral("@interface App\n- (void)run;\n@end\n"),
       {CodeHighlightRole::Keyword}},
      {QStringLiteral("php"),
       QStringLiteral("function greet($name) { return \"hi\"; }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("powershell"),
       QStringLiteral("function Test { Write-Host \"hi\" }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("qml"),
       QStringLiteral("Item { property string title: \"Muffin\" }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Property}},
      {QStringLiteral("r"),
       QStringLiteral("name <- \"muffin\"\nprint(name)\n"),
       {CodeHighlightRole::String, CodeHighlightRole::Function}},
      {QStringLiteral("ruby"),
       QStringLiteral("def greet(name)\n  \"hi #{name}\"\nend\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("rust"),
       QStringLiteral("fn main() { let name = \"muffin\"; println!(\"{}\", name); }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("toml"),
       QStringLiteral("name = \"muffin\"\ncount = 2\n"),
       {CodeHighlightRole::String, CodeHighlightRole::Number}},
      {QStringLiteral("typescript"),
       QStringLiteral("function greet(name: string): string { return `hi ${name}`; }\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("tsx"),
       QStringLiteral("export const App = () => <div className=\"note\">Hi</div>;\n"),
       {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("xml"),
       QStringLiteral("<note priority=\"1\">hello</note>\n"),
       {CodeHighlightRole::Type, CodeHighlightRole::String}},
      {QStringLiteral("yaml"),
       QStringLiteral("name: muffin\ncount: 2\n"),
       {CodeHighlightRole::String, CodeHighlightRole::Number}},
  };
  for (const HighlightFixture& fixture : fixtures) {
    requireHighlightsFixture(highlighter, fixture);
  }

  const QStringList aliases{
      QStringLiteral("py"),  QStringLiteral("hxx"), QStringLiteral("sh"),  QStringLiteral("cs"),
      QStringLiteral("js"),  QStringLiteral("ts"),  QStringLiteral("rs"),  QStringLiteral("rb"),
      QStringLiteral("yml"), QStringLiteral("md"),  QStringLiteral("ps1"), QStringLiteral("objc"),
  };
  for (const QString& alias : aliases) {
    require(highlighter.supportsLanguage(alias), QStringLiteral("%1 alias should be registered").arg(alias));
  }

  const QStringList unicodePrefixes{
      QStringLiteral("\u4e2d\u6587"),
      QStringLiteral("\U0001f600"),
      QStringLiteral("\u4e2d\u6587 \U0001f600 e\u0301 \U0001f680\U0001f600"),
      QString(QChar(0xd800)),
      QString(QChar(0xdc00)),
  };
  for (const auto& prefix : unicodePrefixes) {
    const QString cppText = QStringLiteral("// %1\nint main() { return 0; }").arg(prefix);
    const auto spans = highlighter.highlight(QStringLiteral("cpp"), cppText);
    const qsizetype returnStart = cppText.indexOf(QStringLiteral("return"));
    require(hasExactRoleSpan(spans, CodeHighlightRole::Comment, 0, prefix.size() + 3),
            QStringLiteral("comment span preserves complete Unicode source boundaries"));
    require(hasExactRoleSpan(spans, CodeHighlightRole::Keyword, returnStart, returnStart + 6),
            QStringLiteral("C++ keyword span aligns exactly after UTF-16 text"));
    const qsizetype numberStart = cppText.indexOf(QLatin1Char('0'));
    require(hasExactRoleSpan(spans, CodeHighlightRole::Number, numberStart, numberStart + 1),
            QStringLiteral("token near EOF aligns after Unicode source text"));
  }

  require(!highlighter.supportsLanguage(QStringLiteral("text")), QStringLiteral("text should remain unhighlighted"));
  require(!highlighter.supportsLanguage(QStringLiteral("pgp")), QStringLiteral("pgp should remain unhighlighted"));
  require(highlighter.highlight(QStringLiteral("text"), QStringLiteral("plain text")).isEmpty(),
          QStringLiteral("text should not highlight"));
}

void testLongCodeHighlightingPreservesTokenBoundaries() {
  TreeSitterHighlighter highlighter;
  const QString line = QStringLiteral("const char* value = \"hi\\n\"; /* \u6ce8\u91ca \U0001f600 */\n");
  const auto expected = highlighter.highlight(QStringLiteral("cpp"), line);
  const auto spans = highlighter.highlight(QStringLiteral("cpp"), line.repeated(1500));
  require(!expected.isEmpty() && spans.size() == expected.size() * 1500,
          "long code must retain all normalized token spans");
  for (int row = 0; row < 1500; ++row)
    for (qsizetype i = 0; i < expected.size(); ++i) {
      const auto& actual = spans[row * expected.size() + i];
      require(actual.start == row * line.size() + expected[i].start &&
                  actual.end == row * line.size() + expected[i].end && actual.role == expected[i].role,
              "normalizing repeated tokens must preserve roles and Unicode source offsets");
    }
  const QString json = QStringLiteral("{\"message\": \"hi\\n\"}");
  const auto nested = highlighter.highlight(QStringLiteral("json"), json);
  const qsizetype escape = json.indexOf(QStringLiteral("\\n"));
  require(hasExactRoleSpan(nested, CodeHighlightRole::Escape, escape, escape + 2),
          "an escape capture must override its enclosing string capture");
}

} // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testTreeSitterCodeHighlighting);
  RUN_TEST(testLongCodeHighlightingPreservesTokenBoundaries);
#undef RUN_TEST
  return 0;
}

#include "render/TreeSitterHighlighter.h"

#include <tree_sitter/api.h>

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <map>
#include <memory>

int qInitResources_tree_sitter_queries();

extern "C" {
const TSLanguage* tree_sitter_bash(void);
const TSLanguage* tree_sitter_c(void);
const TSLanguage* tree_sitter_cpp(void);
const TSLanguage* tree_sitter_c_sharp(void);
const TSLanguage* tree_sitter_css(void);
const TSLanguage* tree_sitter_go(void);
const TSLanguage* tree_sitter_html(void);
const TSLanguage* tree_sitter_ini(void);
const TSLanguage* tree_sitter_java(void);
const TSLanguage* tree_sitter_javascript(void);
const TSLanguage* tree_sitter_json(void);
const TSLanguage* tree_sitter_lua(void);
const TSLanguage* tree_sitter_markdown(void);
const TSLanguage* tree_sitter_objc(void);
const TSLanguage* tree_sitter_php_only(void);
const TSLanguage* tree_sitter_powershell(void);
const TSLanguage* tree_sitter_python(void);
const TSLanguage* tree_sitter_qmljs(void);
const TSLanguage* tree_sitter_r(void);
const TSLanguage* tree_sitter_ruby(void);
const TSLanguage* tree_sitter_rust(void);
const TSLanguage* tree_sitter_toml(void);
const TSLanguage* tree_sitter_tsx(void);
const TSLanguage* tree_sitter_typescript(void);
const TSLanguage* tree_sitter_xml(void);
const TSLanguage* tree_sitter_yaml(void);
}

namespace muffin {
namespace {

struct LanguageSpec {
  QString canonical;
  const TSLanguage* (*language)();
  QStringList queryPaths;
};

struct CompiledQuery {
  QByteArray text;
  std::unique_ptr<TSQuery, void (*)(TSQuery*)> query{nullptr, ts_query_delete};
};

struct ParserDeleter {
  void operator()(TSParser* parser) const {
    ts_parser_delete(parser);
  }
};

struct TreeDeleter {
  void operator()(TSTree* tree) const {
    ts_tree_delete(tree);
  }
};

struct QueryCursorDeleter {
  void operator()(TSQueryCursor* cursor) const {
    ts_query_cursor_delete(cursor);
  }
};

void addLanguage(
    QHash<QString, LanguageSpec>& specs,
    QString canonical,
    const TSLanguage* (*language)(),
    QStringList queryPaths,
    std::initializer_list<const char*> aliases) {
  const LanguageSpec spec{canonical, language, std::move(queryPaths)};
  specs.insert(canonical, spec);
  for (const char* alias : aliases) {
    specs.insert(QString::fromLatin1(alias), spec);
  }
}

void ensureQueryResourcesRegistered() {
  static const bool registered = [] {
    qInitResources_tree_sitter_queries();
    return true;
  }();
  Q_UNUSED(registered);
}

const QHash<QString, LanguageSpec>& languageSpecs() {
  static const QHash<QString, LanguageSpec> specs = [] {
    QHash<QString, LanguageSpec> result;
    addLanguage(result, QStringLiteral("bash"), tree_sitter_bash, {QStringLiteral(":/tree-sitter/queries/bash/highlights.scm")}, {"sh", "shell", "zsh"});
    addLanguage(result, QStringLiteral("c"), tree_sitter_c, {QStringLiteral(":/tree-sitter/queries/c/highlights.scm")}, {"h"});
    addLanguage(result, QStringLiteral("cpp"), tree_sitter_cpp, {QStringLiteral(":/tree-sitter/queries/c/highlights.scm"), QStringLiteral(":/tree-sitter/queries/cpp/highlights.scm")}, {"c++", "cc", "cxx", "hpp", "hxx"});
    addLanguage(result, QStringLiteral("csharp"), tree_sitter_c_sharp, {QStringLiteral(":/tree-sitter/queries/csharp/highlights.scm")}, {"cs", "c#"});
    addLanguage(result, QStringLiteral("css"), tree_sitter_css, {QStringLiteral(":/tree-sitter/queries/css/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("go"), tree_sitter_go, {QStringLiteral(":/tree-sitter/queries/go/highlights.scm")}, {"golang"});
    addLanguage(result, QStringLiteral("html"), tree_sitter_html, {QStringLiteral(":/tree-sitter/queries/html/highlights.scm")}, {"htm"});
    addLanguage(result, QStringLiteral("ini"), tree_sitter_ini, {QStringLiteral(":/tree-sitter/queries/ini/highlights.scm")}, {"conf"});
    addLanguage(result, QStringLiteral("java"), tree_sitter_java, {QStringLiteral(":/tree-sitter/queries/java/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("javascript"), tree_sitter_javascript, {QStringLiteral(":/tree-sitter/queries/javascript/highlights.scm")}, {"js", "jsx", "mjs", "cjs"});
    addLanguage(result, QStringLiteral("json"), tree_sitter_json, {QStringLiteral(":/tree-sitter/queries/json/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("lua"), tree_sitter_lua, {QStringLiteral(":/tree-sitter/queries/lua/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("markdown"), tree_sitter_markdown, {QStringLiteral(":/tree-sitter/queries/markdown/highlights.scm")}, {"md"});
    addLanguage(result, QStringLiteral("objective-c"), tree_sitter_objc, {QStringLiteral(":/tree-sitter/queries/objc/highlights.scm")}, {"objc", "obj-c", "m", "mm"});
    addLanguage(result, QStringLiteral("php"), tree_sitter_php_only, {QStringLiteral(":/tree-sitter/queries/php/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("powershell"), tree_sitter_powershell, {QStringLiteral(":/tree-sitter/queries/powershell/highlights.scm")}, {"ps1", "psm1"});
    addLanguage(result, QStringLiteral("python"), tree_sitter_python, {QStringLiteral(":/tree-sitter/queries/python/highlights.scm")}, {"py"});
    addLanguage(result, QStringLiteral("qml"), tree_sitter_qmljs, {QStringLiteral(":/tree-sitter/queries/qmljs/highlights.scm")}, {"qmljs"});
    addLanguage(result, QStringLiteral("r"), tree_sitter_r, {QStringLiteral(":/tree-sitter/queries/r/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("ruby"), tree_sitter_ruby, {QStringLiteral(":/tree-sitter/queries/ruby/highlights.scm")}, {"rb"});
    addLanguage(result, QStringLiteral("rust"), tree_sitter_rust, {QStringLiteral(":/tree-sitter/queries/rust/highlights.scm")}, {"rs"});
    addLanguage(result, QStringLiteral("toml"), tree_sitter_toml, {QStringLiteral(":/tree-sitter/queries/toml/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("typescript"), tree_sitter_typescript, {QStringLiteral(":/tree-sitter/queries/javascript/highlights.scm"), QStringLiteral(":/tree-sitter/queries/typescript/highlights.scm")}, {"ts"});
    addLanguage(result, QStringLiteral("tsx"), tree_sitter_tsx, {QStringLiteral(":/tree-sitter/queries/javascript/highlights.scm"), QStringLiteral(":/tree-sitter/queries/typescript/highlights.scm"), QStringLiteral(":/tree-sitter/queries/tsx/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("xml"), tree_sitter_xml, {QStringLiteral(":/tree-sitter/queries/xml/highlights.scm")}, {});
    addLanguage(result, QStringLiteral("yaml"), tree_sitter_yaml, {QStringLiteral(":/tree-sitter/queries/yaml/highlights.scm")}, {"yml"});
    return result;
  }();
  return specs;
}

QByteArray readResource(const QString& path) {
  ensureQueryResourcesRegistered();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  return file.readAll();
}

CompiledQuery* compiledQueryFor(const LanguageSpec& spec) {
  static std::map<QString, std::unique_ptr<CompiledQuery>> cache;
  if (auto it = cache.find(spec.canonical); it != cache.end()) {
    return it->second.get();
  }

  auto compiled = std::make_unique<CompiledQuery>();
  for (const QString& queryPath : spec.queryPaths) {
    const QByteArray queryText = readResource(queryPath);
    if (queryText.isEmpty()) {
      return nullptr;
    }
    compiled->text += queryText;
    compiled->text += '\n';
  }
  if (compiled->text.isEmpty()) {
    return nullptr;
  }

  uint32_t errorOffset = 0;
  TSQueryError errorType = TSQueryErrorNone;
  compiled->query.reset(ts_query_new(
      spec.language(),
      compiled->text.constData(),
      static_cast<uint32_t>(compiled->text.size()),
      &errorOffset,
      &errorType));
  if (!compiled->query) {
    return nullptr;
  }

  CompiledQuery* result = compiled.get();
  cache[spec.canonical] = std::move(compiled);
  return result;
}

QString nodeText(const QString& text, const QVector<qsizetype>& byteToUtf16, TSNode node) {
  const qsizetype start = byteToUtf16.at(qBound<int>(0, static_cast<int>(ts_node_start_byte(node)), byteToUtf16.size() - 1));
  const qsizetype end = byteToUtf16.at(qBound<int>(0, static_cast<int>(ts_node_end_byte(node)), byteToUtf16.size() - 1));
  return text.mid(start, end - start);
}

QString queryStringValue(TSQuery* query, uint32_t id) {
  uint32_t length = 0;
  const char* value = ts_query_string_value_for_id(query, id, &length);
  return QString::fromUtf8(value, static_cast<qsizetype>(length));
}

QString queryCaptureName(TSQuery* query, uint32_t id) {
  uint32_t length = 0;
  const char* value = ts_query_capture_name_for_id(query, id, &length);
  return QString::fromUtf8(value, static_cast<qsizetype>(length));
}

const TSQueryCapture* captureByName(TSQuery* query, const TSQueryMatch& match, uint32_t captureId) {
  const QString name = queryCaptureName(query, captureId);
  for (uint16_t i = 0; i < match.capture_count; ++i) {
    if (queryCaptureName(query, match.captures[i].index) == name) {
      return &match.captures[i];
    }
  }
  return nullptr;
}

bool stringPredicateMatches(
    const QString& op,
    TSQuery* query,
    const TSQueryMatch& match,
    const QVector<TSQueryPredicateStep>& steps,
    const QString& text,
    const QVector<qsizetype>& byteToUtf16) {
  if (steps.size() < 3 || steps.at(1).type != TSQueryPredicateStepTypeCapture) {
    return true;
  }
  const TSQueryCapture* capture = captureByName(query, match, steps.at(1).value_id);
  if (!capture) {
    return false;
  }
  const QString capturedText = nodeText(text, byteToUtf16, capture->node);

  if (op == QStringLiteral("eq?") || op == QStringLiteral("not-eq?")) {
    if (steps.size() < 3 || steps.at(2).type != TSQueryPredicateStepTypeString) {
      return true;
    }
    const bool matched = capturedText == queryStringValue(query, steps.at(2).value_id);
    return op == QStringLiteral("eq?") ? matched : !matched;
  }

  if (op == QStringLiteral("match?") || op == QStringLiteral("not-match?")) {
    if (steps.size() < 3 || steps.at(2).type != TSQueryPredicateStepTypeString) {
      return true;
    }
    const QRegularExpression regex(queryStringValue(query, steps.at(2).value_id));
    const bool matched = regex.isValid() && regex.match(capturedText).hasMatch();
    return op == QStringLiteral("match?") ? matched : !matched;
  }

  if (op == QStringLiteral("any-of?")) {
    for (qsizetype i = 2; i < steps.size(); ++i) {
      if (steps.at(i).type == TSQueryPredicateStepTypeString && capturedText == queryStringValue(query, steps.at(i).value_id)) {
        return true;
      }
    }
    return false;
  }

  return true;
}

bool predicatesMatch(
    TSQuery* query,
    const TSQueryMatch& match,
    const QString& text,
    const QVector<qsizetype>& byteToUtf16) {
  uint32_t stepCount = 0;
  const TSQueryPredicateStep* rawSteps = ts_query_predicates_for_pattern(query, match.pattern_index, &stepCount);
  QVector<TSQueryPredicateStep> steps;
  for (uint32_t i = 0; i < stepCount; ++i) {
    const TSQueryPredicateStep step = rawSteps[i];
    if (step.type == TSQueryPredicateStepTypeDone) {
      if (!steps.isEmpty() && steps.first().type == TSQueryPredicateStepTypeString) {
        const QString op = queryStringValue(query, steps.first().value_id);
        if (!stringPredicateMatches(op, query, match, steps, text, byteToUtf16)) {
          return false;
        }
      }
      steps.clear();
    } else {
      steps.push_back(step);
    }
  }
  return true;
}

CodeHighlightRole roleForCapture(QStringView capture) {
  if (capture.startsWith(QStringLiteral("comment"))) {
    return CodeHighlightRole::Comment;
  }
  if (capture.startsWith(QStringLiteral("keyword")) || capture.startsWith(QStringLiteral("markup.heading")) ||
      capture.startsWith(QStringLiteral("markup.quote"))) {
    return CodeHighlightRole::Keyword;
  }
  if (capture.startsWith(QStringLiteral("text.title"))) {
    return CodeHighlightRole::Type;
  }
  if (capture.startsWith(QStringLiteral("string")) || capture.startsWith(QStringLiteral("character")) ||
      capture.startsWith(QStringLiteral("text.literal")) || capture.startsWith(QStringLiteral("text.uri")) ||
      capture.startsWith(QStringLiteral("text.reference")) || capture.startsWith(QStringLiteral("markup.raw")) ||
      capture.startsWith(QStringLiteral("markup.link"))) {
    return CodeHighlightRole::String;
  }
  if (capture.startsWith(QStringLiteral("number")) || capture.startsWith(QStringLiteral("float"))) {
    return CodeHighlightRole::Number;
  }
  if (capture.startsWith(QStringLiteral("function")) || capture.startsWith(QStringLiteral("method"))) {
    return CodeHighlightRole::Function;
  }
  if (capture.startsWith(QStringLiteral("type")) || capture == QStringLiteral("constructor") ||
      capture.startsWith(QStringLiteral("tag")) || capture.startsWith(QStringLiteral("module")) ||
      capture.startsWith(QStringLiteral("namespace"))) {
    return CodeHighlightRole::Type;
  }
  if (capture.startsWith(QStringLiteral("constant")) || capture.startsWith(QStringLiteral("boolean")) ||
      capture == QStringLiteral("null") || capture == QStringLiteral("none")) {
    return CodeHighlightRole::Constant;
  }
  if (capture.startsWith(QStringLiteral("property")) || capture.startsWith(QStringLiteral("attribute")) ||
      capture.startsWith(QStringLiteral("label")) || capture.startsWith(QStringLiteral("field"))) {
    return CodeHighlightRole::Property;
  }
  if (capture.startsWith(QStringLiteral("operator"))) {
    return CodeHighlightRole::Operator;
  }
  if (capture.startsWith(QStringLiteral("punctuation")) || capture.startsWith(QStringLiteral("delimiter")) ||
      capture.startsWith(QStringLiteral("markup.list"))) {
    return CodeHighlightRole::Punctuation;
  }
  if (capture.startsWith(QStringLiteral("preprocessor")) || capture.startsWith(QStringLiteral("include")) ||
      capture.startsWith(QStringLiteral("annotation"))) {
    return CodeHighlightRole::Preprocessor;
  }
  if (capture.startsWith(QStringLiteral("escape"))) {
    return CodeHighlightRole::Escape;
  }
  if (capture.startsWith(QStringLiteral("variable")) || capture.startsWith(QStringLiteral("identifier")) ||
      capture.startsWith(QStringLiteral("parameter"))) {
    return CodeHighlightRole::Variable;
  }
  return CodeHighlightRole::Plain;
}

int priority(CodeHighlightRole role) {
  switch (role) {
    case CodeHighlightRole::Plain:
      return 0;
    case CodeHighlightRole::Variable:
      return 1;
    case CodeHighlightRole::Punctuation:
      return 2;
    case CodeHighlightRole::Operator:
      return 3;
    case CodeHighlightRole::Property:
      return 4;
    case CodeHighlightRole::Type:
      return 5;
    case CodeHighlightRole::Constant:
      return 6;
    case CodeHighlightRole::Function:
      return 7;
    case CodeHighlightRole::Number:
      return 8;
    case CodeHighlightRole::String:
      return 9;
    case CodeHighlightRole::Keyword:
      return 10;
    case CodeHighlightRole::Preprocessor:
      return 11;
    case CodeHighlightRole::Escape:
      return 12;
    case CodeHighlightRole::Comment:
      return 13;
  }
  return 0;
}

qsizetype utf16OffsetForByte(const QVector<qsizetype>& byteToUtf16, uint32_t byte) {
  if (byteToUtf16.isEmpty()) {
    return 0;
  }
  return byteToUtf16.at(qBound<int>(0, static_cast<int>(byte), byteToUtf16.size() - 1));
}

QVector<qsizetype> byteToUtf16Map(const QString& text, const QByteArray& bytes) {
  QVector<qsizetype> map(bytes.size() + 1, 0);
  qsizetype byteOffset = 0;
  for (qsizetype i = 0; i < text.size(); ++i) {
    const QByteArray encoded(QStringView(text).mid(i, 1).toUtf8());
    const qsizetype nextByteOffset = byteOffset + encoded.size();
    while (byteOffset < nextByteOffset && byteOffset < map.size()) {
      map[byteOffset] = i;
      ++byteOffset;
    }
  }
  while (byteOffset < map.size()) {
    map[byteOffset] = text.size();
    ++byteOffset;
  }
  return map;
}

QVector<CodeHighlightSpan> normalizeSpans(QVector<CodeHighlightSpan> spans, qsizetype textSize) {
  spans.erase(std::remove_if(spans.begin(), spans.end(), [textSize](const CodeHighlightSpan& span) {
                return span.start < 0 || span.end <= span.start || span.start >= textSize;
              }),
              spans.end());
  for (CodeHighlightSpan& span : spans) {
    span.start = qBound<qsizetype>(0, span.start, textSize);
    span.end = qBound<qsizetype>(span.start, span.end, textSize);
  }
  std::sort(spans.begin(), spans.end(), [](const CodeHighlightSpan& left, const CodeHighlightSpan& right) {
    if (left.start != right.start) {
      return left.start < right.start;
    }
    const qsizetype leftLength = left.end - left.start;
    const qsizetype rightLength = right.end - right.start;
    if (leftLength != rightLength) {
      return leftLength < rightLength;
    }
    return priority(left.role) > priority(right.role);
  });

  QVector<qsizetype> boundaries;
  boundaries.reserve(spans.size() * 2);
  for (const CodeHighlightSpan& span : spans) {
    boundaries.push_back(span.start);
    boundaries.push_back(span.end);
  }
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

  QVector<CodeHighlightSpan> result;
  for (qsizetype i = 0; i + 1 < boundaries.size(); ++i) {
    const qsizetype start = boundaries.at(i);
    const qsizetype end = boundaries.at(i + 1);
    CodeHighlightRole bestRole = CodeHighlightRole::Plain;
    int bestPriority = 0;
    for (const CodeHighlightSpan& span : spans) {
      if (span.start <= start && span.end >= end) {
        const int spanPriority = priority(span.role);
        if (spanPriority >= bestPriority) {
          bestPriority = spanPriority;
          bestRole = span.role;
        }
      }
    }
    if (bestRole == CodeHighlightRole::Plain) {
      continue;
    }
    if (!result.isEmpty() && result.last().end == start && result.last().role == bestRole) {
      result.last().end = end;
    } else {
      result.push_back({start, end, bestRole});
    }
  }
  return result;
}

}  // namespace

QVector<CodeHighlightSpan> TreeSitterHighlighter::highlight(const QString& language, const QString& text) const {
  const QString key = language.trimmed().toLower();
  const auto it = languageSpecs().find(key);
  if (it == languageSpecs().end() || text.isEmpty()) {
    return {};
  }

  const QByteArray bytes = text.toUtf8();
  const QVector<qsizetype> byteToUtf16 = byteToUtf16Map(text, bytes);
  const TSLanguage* tsLanguage = it->language();
  std::unique_ptr<TSParser, ParserDeleter> parser(ts_parser_new());
  if (!parser || !ts_parser_set_language(parser.get(), tsLanguage)) {
    return {};
  }

  std::unique_ptr<TSTree, TreeDeleter> tree(
      ts_parser_parse_string(parser.get(), nullptr, bytes.constData(), static_cast<uint32_t>(bytes.size())));
  if (!tree) {
    return {};
  }

  CompiledQuery* query = compiledQueryFor(*it);
  if (!query || !query->query) {
    return {};
  }

  std::unique_ptr<TSQueryCursor, QueryCursorDeleter> cursor(ts_query_cursor_new());
  if (!cursor) {
    return {};
  }
  ts_query_cursor_exec(cursor.get(), query->query.get(), ts_tree_root_node(tree.get()));

  QVector<CodeHighlightSpan> spans;
  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor.get(), &match)) {
    if (!predicatesMatch(query->query.get(), match, text, byteToUtf16)) {
      continue;
    }
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture& capture = match.captures[i];
      const QString captureText = queryCaptureName(query->query.get(), capture.index);
      const CodeHighlightRole role = roleForCapture(QStringView(captureText));
      if (role == CodeHighlightRole::Plain) {
        continue;
      }
      const qsizetype start = utf16OffsetForByte(byteToUtf16, ts_node_start_byte(capture.node));
      const qsizetype end = utf16OffsetForByte(byteToUtf16, ts_node_end_byte(capture.node));
      spans.push_back({start, end, role});
    }
  }
  return normalizeSpans(std::move(spans), text.size());
}

bool TreeSitterHighlighter::supportsLanguage(const QString& language) const {
  return languageSpecs().contains(language.trimmed().toLower());
}

}  // namespace muffin

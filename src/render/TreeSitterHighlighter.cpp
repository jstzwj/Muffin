#include "render/TreeSitterHighlighter.h"

#include <tree_sitter/api.h>

#include <QByteArray>
#include <QHash>

#include <algorithm>
#include <cstring>
#include <memory>

extern "C" {
const TSLanguage* tree_sitter_c(void);
const TSLanguage* tree_sitter_cpp(void);
const TSLanguage* tree_sitter_python(void);
}

namespace muffin {
namespace {

struct LanguageSpec {
  const TSLanguage* (*language)();
  const char* query;
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

struct QueryDeleter {
  void operator()(TSQuery* query) const {
    ts_query_delete(query);
  }
};

struct QueryCursorDeleter {
  void operator()(TSQueryCursor* cursor) const {
    ts_query_cursor_delete(cursor);
  }
};

constexpr const char* pythonQuery = R"TS(
(identifier) @variable
((identifier) @constructor (#match? @constructor "^[A-Z]"))
((identifier) @constant (#match? @constant "^[A-Z][A-Z_]*$"))
(decorator) @function
(call function: (attribute attribute: (identifier) @function.method))
(call function: (identifier) @function)
(function_definition name: (identifier) @function)
(attribute attribute: (identifier) @property)
(type (identifier) @type)
[(none) (true) (false)] @constant.builtin
[(integer) (float)] @number
(comment) @comment
(string) @string
(escape_sequence) @escape
[
  "-" "-=" "!=" "*" "**" "**=" "*=" "/" "//" "//=" "/="
  "&" "&=" "%" "%=" "^" "^=" "+" "->" "+=" "<" "<<"
  "<<=" "<=" "<>" "=" ":=" "==" ">" ">=" ">>" ">>="
  "|" "|=" "~" "and" "in" "is" "not" "or" "is not" "not in"
] @operator
[
  "as" "assert" "async" "await" "break" "class" "continue" "def"
  "del" "elif" "else" "except" "finally" "for" "from" "global"
  "if" "import" "lambda" "nonlocal" "pass" "raise" "return"
  "try" "while" "with" "yield"
] @keyword
)TS";

constexpr const char* cQuery = R"TS(
(identifier) @variable
((identifier) @constant (#match? @constant "^[A-Z][A-Z\\d_]*$"))
[
  "break" "case" "const" "continue" "default" "do" "else" "enum"
  "extern" "for" "if" "inline" "return" "sizeof" "static" "struct"
  "switch" "typedef" "union" "volatile" "while"
] @keyword
[
  "#define" "#elif" "#else" "#endif" "#if" "#ifdef" "#ifndef" "#include"
] @preprocessor
(preproc_directive) @preprocessor
["--" "-" "-=" "->" "=" "!=" "*" "&" "&&" "+" "++" "+=" "<" "==" ">" "||"] @operator
["." ";" "," ":" "(" ")" "[" "]" "{" "}"] @punctuation
(string_literal) @string
(system_lib_string) @string
(null) @constant
(number_literal) @number
(char_literal) @number
(field_identifier) @property
(statement_identifier) @label
(type_identifier) @type
(primitive_type) @type
(sized_type_specifier) @type
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function))
(function_declarator declarator: (identifier) @function)
(preproc_function_def name: (identifier) @function.special)
(comment) @comment
)TS";

constexpr const char* cppQuery = R"TS(
(identifier) @variable
((identifier) @constant (#match? @constant "^[A-Z][A-Z\\d_]*$"))
[
  "break" "case" "const" "continue" "default" "do" "else" "enum"
  "extern" "for" "if" "inline" "return" "sizeof" "static" "struct"
  "switch" "typedef" "union" "volatile" "while"
] @keyword
[
  "#define" "#elif" "#else" "#endif" "#if" "#ifdef" "#ifndef" "#include"
] @preprocessor
(preproc_directive) @preprocessor
["--" "-" "-=" "->" "=" "!=" "*" "&" "&&" "+" "++" "+=" "<" "==" ">" "||"] @operator
["." ";" "," ":" "(" ")" "[" "]" "{" "}"] @punctuation
(string_literal) @string
(system_lib_string) @string
(null) @constant
(number_literal) @number
(char_literal) @number
(field_identifier) @property
(statement_identifier) @label
(type_identifier) @type
(primitive_type) @type
(sized_type_specifier) @type
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function))
(function_declarator declarator: (identifier) @function)
(preproc_function_def name: (identifier) @function.special)
(comment) @comment
(call_expression function: (qualified_identifier name: (identifier) @function))
(template_function name: (identifier) @function)
(template_method name: (field_identifier) @function)
(function_declarator declarator: (qualified_identifier name: (identifier) @function))
(function_declarator declarator: (field_identifier) @function)
((namespace_identifier) @type (#match? @type "^[A-Z]"))
(auto) @type
(this) @variable.builtin
(null "nullptr" @constant)
(module_name (identifier) @module)
[
 "catch" "class" "co_await" "co_return" "co_yield" "constexpr"
 "constinit" "consteval" "delete" "explicit" "final" "friend"
 "mutable" "namespace" "noexcept" "new" "override" "private"
 "protected" "public" "template" "throw" "try" "typename" "using"
 "concept" "requires" "virtual" "import" "export" "module"
] @keyword
(raw_string_literal) @string
)TS";

const QHash<QString, LanguageSpec>& languageSpecs() {
  static const QHash<QString, LanguageSpec> specs = {
      {QStringLiteral("py"), {tree_sitter_python, pythonQuery}},
      {QStringLiteral("python"), {tree_sitter_python, pythonQuery}},
      {QStringLiteral("c"), {tree_sitter_c, cQuery}},
      {QStringLiteral("h"), {tree_sitter_c, cQuery}},
      {QStringLiteral("cpp"), {tree_sitter_cpp, cppQuery}},
      {QStringLiteral("c++"), {tree_sitter_cpp, cppQuery}},
      {QStringLiteral("cc"), {tree_sitter_cpp, cppQuery}},
      {QStringLiteral("cxx"), {tree_sitter_cpp, cppQuery}},
      {QStringLiteral("hpp"), {tree_sitter_cpp, cppQuery}},
  };
  return specs;
}

CodeHighlightRole roleForCapture(QStringView capture) {
  if (capture.startsWith(QStringLiteral("comment"))) {
    return CodeHighlightRole::Comment;
  }
  if (capture.startsWith(QStringLiteral("keyword"))) {
    return CodeHighlightRole::Keyword;
  }
  if (capture.startsWith(QStringLiteral("string"))) {
    return CodeHighlightRole::String;
  }
  if (capture.startsWith(QStringLiteral("number"))) {
    return CodeHighlightRole::Number;
  }
  if (capture.startsWith(QStringLiteral("function"))) {
    return CodeHighlightRole::Function;
  }
  if (capture.startsWith(QStringLiteral("type")) || capture == QStringLiteral("constructor")) {
    return CodeHighlightRole::Type;
  }
  if (capture.startsWith(QStringLiteral("constant"))) {
    return CodeHighlightRole::Constant;
  }
  if (capture.startsWith(QStringLiteral("property"))) {
    return CodeHighlightRole::Property;
  }
  if (capture.startsWith(QStringLiteral("operator"))) {
    return CodeHighlightRole::Operator;
  }
  if (capture.startsWith(QStringLiteral("punctuation")) || capture.startsWith(QStringLiteral("delimiter"))) {
    return CodeHighlightRole::Punctuation;
  }
  if (capture.startsWith(QStringLiteral("preprocessor")) || capture.startsWith(QStringLiteral("include"))) {
    return CodeHighlightRole::Preprocessor;
  }
  if (capture.startsWith(QStringLiteral("escape"))) {
    return CodeHighlightRole::Escape;
  }
  if (capture.startsWith(QStringLiteral("variable"))) {
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

  uint32_t errorOffset = 0;
  TSQueryError errorType = TSQueryErrorNone;
  std::unique_ptr<TSQuery, QueryDeleter> query(ts_query_new(
      tsLanguage,
      it->query,
      static_cast<uint32_t>(std::strlen(it->query)),
      &errorOffset,
      &errorType));
  if (!query) {
    return {};
  }

  std::unique_ptr<TSQueryCursor, QueryCursorDeleter> cursor(ts_query_cursor_new());
  if (!cursor) {
    return {};
  }
  ts_query_cursor_exec(cursor.get(), query.get(), ts_tree_root_node(tree.get()));

  QVector<CodeHighlightSpan> spans;
  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor.get(), &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture& capture = match.captures[i];
      uint32_t captureNameLength = 0;
      const char* captureName = ts_query_capture_name_for_id(query.get(), capture.index, &captureNameLength);
      const QString captureText = QString::fromUtf8(captureName, captureNameLength);
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

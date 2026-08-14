#include "mermaid/MermaidPreprocessor.h"

#include "mermaid/MermaidDiagramDetector.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <stdexcept>

namespace muffin::mermaid {
namespace {

// Defence-in-depth caps for the recursive JSON/YAML transforms (milestone H3).
// A crafted frontmatter / directive config with deeply nested objects or arrays
// must not stack-overflow. 64 is far beyond any legitimate mermaid config; on
// overflow the transform stops recursing (graceful) rather than throwing.
constexpr int kMaxConfigDepth = 64;
// Bounds the work the preprocessor's whole-source regexes (frontmatter,
// directive, HTML-quote) do on untrusted input. Generous for any real diagram.
constexpr qsizetype kMaxPreprocessSize = 1'000'000;

const QRegularExpression& frontMatterRegex() {
  static const QRegularExpression value(
      QStringLiteral(R"(^([^\S\n\r]*)-{3}\s*[\n\r](.*?)[\n\r]\1-{3}\s*[\n\r]+)"),
      QRegularExpression::DotMatchesEverythingOption);
  return value;
}

const QRegularExpression& directiveRegex() {
  static const QRegularExpression value(
      QStringLiteral(R"(%{2}\{\s*(?:(\w+)\s*:|(\w+))\s*(?:(\w+)|((?:(?!\}%{2}).|\r?\n)*))?\s*(?:\}%{2})?)"),
      QRegularExpression::CaseInsensitiveOption);
  return value;
}

QString replaceHtmlAttributeQuotes(const QString& source) {
  static const QRegularExpression tagRegex(QStringLiteral(R"(<(\w+)([^>]*)>)"));
  static const QRegularExpression attributeRegex(QStringLiteral(R"regex(="([^"]*)")regex"));
  QString result;
  qsizetype copied = 0;
  auto matches = tagRegex.globalMatch(source);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    result += source.mid(copied, match.capturedStart() - copied);
    QString attributes = match.captured(2);
    attributes.replace(attributeRegex, QStringLiteral("='\\1'"));
    result += QLatin1Char('<') + match.captured(1) + attributes + QLatin1Char('>');
    copied = match.capturedEnd();
  }
  result += source.mid(copied);
  return result;
}

struct MappedText {
  QString text;
  QVector<qsizetype> sourceOffsets;
};

MappedText cleanupTextWithOffsets(const QString& source) {
  MappedText result;
  result.text.reserve(source.size());
  result.sourceOffsets.reserve(source.size());
  for (qsizetype index = 0; index < source.size(); ++index) {
    if (source.at(index) == QLatin1Char('\r')) {
      result.text += QLatin1Char('\n');
      result.sourceOffsets.push_back(index);
      if (index + 1 < source.size() && source.at(index + 1) == QLatin1Char('\n')) {
        ++index;
      }
      continue;
    }
    result.text += source.at(index);
    result.sourceOffsets.push_back(index);
  }
  // Mermaid normalises quotes inside HTML attributes, but the replacement is
  // length-preserving, so the offset vector remains valid.
  result.text = replaceHtmlAttributeQuotes(result.text);
  return result;
}

void removeMatches(MappedText& mapped, const QRegularExpression& expression) {
  QVector<QPair<qsizetype, qsizetype>> ranges;
  auto matches = expression.globalMatch(mapped.text);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    if (match.capturedLength() > 0) {
      ranges.push_back({match.capturedStart(), match.capturedLength()});
    }
  }
  for (auto it = ranges.crbegin(); it != ranges.crend(); ++it) {
    mapped.text.remove(it->first, it->second);
    mapped.sourceOffsets.remove(it->first, it->second);
  }
}

void removeLeadingWhitespace(MappedText& mapped) {
  qsizetype first = 0;
  while (first < mapped.text.size() && mapped.text.at(first).isSpace()) ++first;
  if (first <= 0) return;
  mapped.text.remove(0, first);
  mapped.sourceOffsets.remove(0, first);
}

bool isJsonTruthy(const QJsonValue& value) {
  if (value.isUndefined() || value.isNull()) return false;
  if (value.isBool()) return value.toBool();
  if (value.isDouble()) return value.toDouble() != 0.0 && !std::isnan(value.toDouble());
  if (value.isString()) return !value.toString().isEmpty();
  return true;
}

QJsonValue yamlScalar(const YAML::Node& node) {
  const QString text = QString::fromStdString(node.Scalar());
  // yaml-cpp reports the non-specific tag "!" for quoted scalars and "?" for
  // plain scalars. js-yaml's JSON_SCHEMA resolves types only for plain scalars.
  if (node.Tag() == "!") return text;
  static const QRegularExpression nullValue(QStringLiteral(R"(^(?:null|Null|NULL|~)$)"));
  static const QRegularExpression boolValue(QStringLiteral(R"(^(?:true|True|TRUE|false|False|FALSE)$)"));
  static const QRegularExpression integer(QStringLiteral(R"(^[-+]?(?:0|[1-9][0-9]*|0x[0-9a-fA-F]+)$)"));
  static const QRegularExpression floating(
      QStringLiteral(R"(^[-+]?(?:(?:[0-9]+\.[0-9]*|\.[0-9]+)(?:[eE][-+]?[0-9]+)?|[0-9]+[eE][-+]?[0-9]+|\.inf|\.Inf|\.INF|\.nan|\.NaN|\.NAN)$)"));
  if (nullValue.match(text).hasMatch()) return QJsonValue::Null;
  if (boolValue.match(text).hasMatch()) return text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
  if (integer.match(text).hasMatch()) {
    bool ok = false;
    const qlonglong number = text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                                 ? text.mid(2).toLongLong(&ok, 16)
                                 : text.toLongLong(&ok, 10);
    if (ok) return static_cast<double>(number);
  }
  if (floating.match(text).hasMatch()) {
    bool ok = false;
    const double number = text.toDouble(&ok);
    if (ok && std::isfinite(number)) return number;
  }
  return text;
}

QJsonValue yamlToJson(const YAML::Node& node, int depth = 0) {
  if (!node || node.IsNull()) return QJsonValue::Null;
  if (depth > kMaxConfigDepth) return QJsonValue::Null;  // deeply-nested YAML defence
  if (node.IsScalar()) return yamlScalar(node);
  if (node.IsSequence()) {
    QJsonArray result;
    for (const YAML::Node& item : node) result.push_back(yamlToJson(item, depth + 1));
    return result;
  }
  if (node.IsMap()) {
    QJsonObject result;
    for (const auto& item : node) {
      result.insert(QString::fromStdString(item.first.as<std::string>()), yamlToJson(item.second, depth + 1));
    }
    return result;
  }
  return QJsonValue::Null;
}

QString jsToString(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
  if (value.isNull() || value.isUndefined()) return QString();
  if (value.isArray()) {
    QStringList parts;
    for (const QJsonValue& item : value.toArray()) parts.push_back(jsToString(item));
    return parts.join(QLatin1Char(','));
  }
  return QStringLiteral("[object Object]");
}

struct FrontMatterResult {
  QString text;
  QString title;
  bool hasTitle = false;
  QJsonObject config;
};

FrontMatterResult extractFrontMatter(const QString& source) {
  const QRegularExpressionMatch match = frontMatterRegex().match(source);
  if (!match.hasMatch()) return {source, {}, false, {}};

  const QString indent = match.captured(1);
  QString yamlBody = match.captured(2);
  if (!indent.isEmpty()) {
    QStringList lines = yamlBody.split(QLatin1Char('\n'));
    for (QString& line : lines) {
      if (line.startsWith(indent)) line.remove(0, indent.size());
    }
    yamlBody = lines.join(QLatin1Char('\n'));
  }

  QJsonObject parsed;
  try {
    const QJsonValue value = yamlToJson(YAML::Load(yamlBody.toStdString()));
    if (value.isObject()) parsed = value.toObject();
  } catch (const YAML::Exception& error) {
    throw std::runtime_error(error.what());
  }

  FrontMatterResult result;
  result.text = source.mid(match.capturedLength());
  const QJsonValue title = parsed.value(QStringLiteral("title"));
  if (isJsonTruthy(title)) {
    result.title = jsToString(title);
    result.hasTitle = true;
  }
  const QJsonValue displayMode = parsed.value(QStringLiteral("displayMode"));
  if (isJsonTruthy(displayMode)) {
    QJsonObject gantt = result.config.value(QStringLiteral("gantt")).toObject();
    gantt.insert(QStringLiteral("displayMode"), jsToString(displayMode));
    result.config.insert(QStringLiteral("gantt"), gantt);
  }
  if (parsed.value(QStringLiteral("config")).isObject()) {
    result.config = parsed.value(QStringLiteral("config")).toObject();
    if (isJsonTruthy(displayMode)) {
      QJsonObject gantt = result.config.value(QStringLiteral("gantt")).toObject();
      gantt.insert(QStringLiteral("displayMode"), jsToString(displayMode));
      result.config.insert(QStringLiteral("gantt"), gantt);
    }
  }
  return result;
}

QJsonValue assignWithDepth(QJsonValue destination, const QJsonValue& source, int depth = 2, int guard = 0) {
  if (guard > kMaxConfigDepth) return source;  // array/object nesting defence
  if (source.isArray() && !destination.isArray()) {
    QJsonValue result = destination;
    for (const QJsonValue& item : source.toArray()) result = assignWithDepth(result, item, depth, guard + 1);
    return result;
  }
  if (source.isArray() && destination.isArray()) {
    QJsonArray result = destination.toArray();
    for (const QJsonValue& item : source.toArray()) {
      if (!result.contains(item)) result.push_back(item);
    }
    return result;
  }
  if (depth <= 0) return source;
  if (!source.isObject() || !destination.isObject()) return source;
  QJsonObject result = destination.toObject();
  const QJsonObject additions = source.toObject();
  for (auto it = additions.constBegin(); it != additions.constEnd(); ++it) {
    const QJsonValue old = result.value(it.key());
    if ((it.value().isObject() || it.value().isArray()) &&
        (old.isUndefined() || old.isObject() || old.isArray())) {
      const QJsonValue initial = old.isUndefined() ? (it.value().isArray() ? QJsonValue(QJsonArray()) : QJsonValue(QJsonObject())) : old;
      result.insert(it.key(), assignWithDepth(initial, it.value(), depth - 1, guard + 1));
    } else if ((!old.isObject() && !old.isArray()) && (!it.value().isObject() && !it.value().isArray())) {
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

bool forbiddenKey(const QString& key);

QJsonObject deepMerge(QJsonObject destination, const QJsonObject& source, int depth = 0) {
  if (depth > kMaxConfigDepth) return destination;  // deeply-nested config defence
  for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
    // es-toolkit's merge blocks prototype-pollution keys.
    if (forbiddenKey(it.key())) continue;
    if (it.value().isObject() && destination.value(it.key()).isObject()) {
      destination.insert(it.key(), deepMerge(destination.value(it.key()).toObject(), it.value().toObject(), depth + 1));
    } else {
      destination.insert(it.key(), it.value());
    }
  }
  return destination;
}

bool forbiddenKey(const QString& key) {
  return key.startsWith(QLatin1String("__")) || key.contains(QLatin1String("proto")) ||
         key.contains(QLatin1String("constr"));
}

bool dictionaryValueAllowed(const QString& key, const QString& value) {
  static const QRegularExpression color(
      QStringLiteral(R"(^#[\da-f]{3,8}$|^rgb\([\d\s%,.]+\)$|^hsl\([\d\s%,.]+\)$|^[a-z]+$)"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression icon(QStringLiteral(R"(^[\w-]+(?::[\w-]+)?$)"));
  if (key == QLatin1String("nodeColors")) return color.match(value).hasMatch();
  return icon.match(value).hasMatch();
}

void sanitizeDictionary(QJsonObject& object, const QString& parentKey) {
  for (auto it = object.begin(); it != object.end();) {
    if (forbiddenKey(it.key()) || !it.value().isString() ||
        !dictionaryValueAllowed(parentKey, it.value().toString())) {
      it = object.erase(it);
    } else {
      ++it;
    }
  }
}

QString sanitizeCss(const QString& value) {
  int starts = 0;
  int ends = 0;
  for (const QChar ch : value) {
    if (starts < ends) return QStringLiteral("{ /* ERROR: Unbalanced CSS */ }");
    if (ch == QLatin1Char('{')) ++starts;
    if (ch == QLatin1Char('}')) ++ends;
  }
  return starts == ends ? value : QStringLiteral("{ /* ERROR: Unbalanced CSS */ }");
}

void sanitizeObject(QJsonObject& object) {
  static const QSet<QString> configKeys = {
#include "mermaid/MermaidConfigKeys.inc"
  };
  for (auto it = object.begin(); it != object.end();) {
    if (forbiddenKey(it.key()) || !configKeys.contains(it.key()) || it.value().isNull()) {
      it = object.erase(it);
      continue;
    }
    if (it.value().isObject()) {
      QJsonObject child = it.value().toObject();
      if (it.key() == QLatin1String("nodeColors") || it.key() == QLatin1String("filenameIcons") ||
          it.key() == QLatin1String("extensionIcons")) {
        sanitizeDictionary(child, it.key());
      } else {
        sanitizeObject(child);
      }
      it.value() = child;
    } else if (it.value().isArray()) {
      QJsonArray array = it.value().toArray();
      for (qsizetype i = 0; i < array.size(); ++i) {
        if (array.at(i).isObject()) {
          QJsonObject child = array.at(i).toObject();
          sanitizeObject(child);
          array.replace(i, child);
        }
      }
      it.value() = array;
    } else if (it.value().isString() &&
               (it.key().contains(QLatin1String("themeCSS")) ||
                it.key().contains(QLatin1String("fontFamily")) ||
                it.key().contains(QLatin1String("altFontFamily")))) {
      it.value() = sanitizeCss(it.value().toString());
    }
    ++it;
  }

  QJsonObject themeVariables = object.value(QStringLiteral("themeVariables")).toObject();
  // Mermaid's directive sanitizer accepts CSS identifiers in theme values;
  // generic family names such as `sans-serif` therefore retain their hyphen.
  static const QRegularExpression safeThemeValue(
      QStringLiteral(R"(^[\d "#%(),.;A-Za-z-]+$)"));
  for (auto it = themeVariables.begin(); it != themeVariables.end(); ++it) {
    if (it.value().isString() && !safeThemeValue.match(it.value().toString()).hasMatch()) {
      it.value() = QString();
    }
  }
  if (!themeVariables.isEmpty()) object.insert(QStringLiteral("themeVariables"), themeVariables);
}

struct Directive {
  QString type;
  QJsonValue args;
};

QVector<Directive> directives(const QString& source) {
  QVector<Directive> result;
  QString text = source.trimmed();
  text.replace(QLatin1Char('\''), QLatin1Char('"'));
  auto matches = directiveRegex().globalMatch(text);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    Directive directive;
    directive.type = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    if (!match.captured(3).isEmpty()) {
      directive.args = match.captured(3).trimmed();
    } else if (!match.captured(4).isEmpty()) {
      QJsonParseError error;
      const QJsonDocument document = QJsonDocument::fromJson(match.captured(4).trimmed().toUtf8(), &error);
      if (error.error != QJsonParseError::NoError) return {};
      directive.args = document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array());
    } else {
      directive.args = QJsonValue::Null;
    }
    result.push_back(std::move(directive));
  }
  return result;
}

QJsonObject detectInit(const QString& source) {
  QVector<QJsonValue> arguments;
  for (const Directive& directive : directives(source)) {
    if (directive.type.compare(QLatin1String("init"), Qt::CaseInsensitive) == 0 ||
        directive.type.compare(QLatin1String("initialize"), Qt::CaseInsensitive) == 0) {
      arguments.push_back(directive.args);
    }
  }
  if (arguments.isEmpty()) return {};
  QJsonValue merged = QJsonObject();
  const bool multiple = arguments.size() > 1;
  for (QJsonValue argument : arguments) {
    // This asymmetry is upstream behavior: detectInit sanitizes the args array
    // only when more than one init directive was found.
    if (multiple && argument.isObject()) {
      QJsonObject object = argument.toObject();
      sanitizeObject(object);
      argument = object;
    }
    merged = assignWithDepth(merged, argument);
  }
  if (!merged.isObject()) return {};
  QJsonObject result = merged.toObject();
  if (result.contains(QStringLiteral("config"))) {
    QString type = detectDiagramType(source);
    if (type == QLatin1String("flowchart-v2")) type = QStringLiteral("flowchart");
    result.insert(type, result.take(QStringLiteral("config")));
  }
  return result;
}

bool hasWrapDirective(const QString& source) {
  for (const Directive& directive : directives(source)) {
    if (directive.type.compare(QLatin1String("wrap"), Qt::CaseInsensitive) == 0) return true;
  }
  return false;
}

void cleanupComments(MappedText& mapped) {
  static const QRegularExpression comments(
      QStringLiteral(R"(^\s*%%(?!\{)[^\n]+\n?)"),
      QRegularExpression::MultilineOption);
  removeMatches(mapped, comments);
  removeLeadingWhitespace(mapped);
}

bool jsTruthy(const QJsonValue& value) {
  if (value.isUndefined() || value.isNull()) return false;
  if (value.isBool()) return value.toBool();
  if (value.isDouble()) {
    const double number = value.toDouble();
    return number != 0.0 && !std::isnan(number);
  }
  if (value.isString()) return !value.toString().isEmpty();
  return true;
}

void sanitizeRenderValue(QJsonValue& value) {
  static const QSet<QString> secureKeys = {
      QStringLiteral("secure"), QStringLiteral("securityLevel"),
      QStringLiteral("startOnLoad"), QStringLiteral("maxTextSize"),
      QStringLiteral("suppressErrorRendering"), QStringLiteral("maxEdges")};

  if (value.isObject()) {
    QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end();) {
      if (secureKeys.contains(it.key()) || it.key().startsWith(QLatin1String("__"))) {
        it = object.erase(it);
        continue;
      }
      if (it.value().isString()) {
        const QString text = it.value().toString();
        if (text.contains(QLatin1Char('<')) || text.contains(QLatin1Char('>')) ||
            text.contains(QLatin1String("url(data:"))) {
          it = object.erase(it);
          continue;
        }
      } else if (it.value().isObject() || it.value().isArray()) {
        QJsonValue child = it.value();
        sanitizeRenderValue(child);
        it.value() = child;
      }
      ++it;
    }
    value = object;
    return;
  }
  if (value.isArray()) {
    QJsonArray array = value.toArray();
    for (qsizetype index = 0; index < array.size(); ++index) {
      QJsonValue child = array.at(index);
      if (child.isString()) {
        const QString text = child.toString();
        if (text.contains(QLatin1Char('<')) || text.contains(QLatin1Char('>')) ||
            text.contains(QLatin1String("url(data:"))) {
          // JavaScript delete leaves a sparse array entry; JSON serialization
          // and Array#toString observe that entry as null/empty respectively.
          array.replace(index, QJsonValue::Null);
          continue;
        }
      }
      if (child.isObject() || child.isArray()) {
        sanitizeRenderValue(child);
        array.replace(index, child);
      }
    }
    value = array;
  }
}

}  // namespace

MermaidPreprocessResult preprocessDiagram(const QString& source) {
  if (source.size() > kMaxPreprocessSize)
    throw std::runtime_error("Maximum mermaid source size exceeded");
  MappedText mapped = cleanupTextWithOffsets(source);
  const QRegularExpressionMatch frontMatterMatch = frontMatterRegex().match(mapped.text);
  FrontMatterResult frontMatter = extractFrontMatter(mapped.text);
  if (frontMatterMatch.hasMatch()) {
    const qsizetype length = frontMatterMatch.capturedLength();
    mapped.text.remove(0, length);
    mapped.sourceOffsets.remove(0, length);
  }
  QJsonObject directive = detectInit(frontMatter.text);
  if (hasWrapDirective(frontMatter.text)) directive.insert(QStringLiteral("wrap"), true);
  removeMatches(mapped, directiveRegex());
  cleanupComments(mapped);

  MermaidPreprocessResult result;
  result.code = mapped.text;
  result.title = frontMatter.title;
  result.hasTitle = frontMatter.hasTitle;
  result.config = deepMerge(frontMatter.config, directive);
  result.codeSourceOffsets = std::move(mapped.sourceOffsets);
  result.codeSourceEndOffset = result.codeSourceOffsets.isEmpty()
      ? source.size()
      : qMin<qsizetype>(source.size(), result.codeSourceOffsets.back() + 1);
  return result;
}

QJsonObject mermaidRenderConfig(const QJsonObject& preprocessedConfig) {
  QJsonObject config = preprocessedConfig;
  sanitizeObject(config);

  const QJsonValue fontFamily = config.value(QStringLiteral("fontFamily"));
  QJsonObject themeVariables = config.value(QStringLiteral("themeVariables")).toObject();
  if (jsTruthy(fontFamily) &&
      !jsTruthy(themeVariables.value(QStringLiteral("fontFamily")))) {
    themeVariables.insert(QStringLiteral("fontFamily"), fontFamily);
    config.insert(QStringLiteral("themeVariables"), themeVariables);
  }

  QJsonValue value(config);
  sanitizeRenderValue(value);
  return value.toObject();
}

}  // namespace muffin::mermaid

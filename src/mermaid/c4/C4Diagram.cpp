#include "mermaid/c4/C4Diagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::c4 {
namespace {

struct Attribute {
  QString value;
  QString key;
  bool keyed = false;
};

[[noreturn]] void fail(C4ErrorKind kind, int line, int column,
                       const QString& token, const QString& message) {
  throw C4ParseError(kind, line, column, token, message);
}

QString sanitizeMetadata(const QString& value) {
  return HtmlSanitizer().sanitizedMermaidText(value.trimmed());
}

int jsParseInt(const QString& value) {
  static const QRegularExpression prefix(
      QStringLiteral(R"(^[\x{0009}-\x{000D}\x{0020}\x{00A0}\x{FEFF}]*([+-]?\d+))"));
  const auto match = prefix.match(value);
  if (!match.hasMatch()) return 0;
  bool ok = false;
  const qlonglong parsed = match.captured(1).toLongLong(&ok, 10);
  if (!ok) return 0;
  return static_cast<int>(std::clamp<qlonglong>(
      parsed, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}

std::optional<Attribute> attributeAt(const QVector<Attribute>& attrs, qsizetype index) {
  if (index < 0 || index >= attrs.size()) return std::nullopt;
  return attrs.at(index);
}

QString valueAt(const QVector<Attribute>& attrs, qsizetype index) {
  const auto value = attributeAt(attrs, index);
  return value ? value->value : QString();
}

void assignOptional(std::optional<QString>& target, const std::optional<Attribute>& value,
                    const QString& expectedKey) {
  if (!value) return;
  if (!value->keyed || value->key == expectedKey) target = value->value;
}

void assignElementDynamic(C4Element& element, const Attribute& value,
                          const QString& fallback) {
  const QString key = value.keyed ? value.key : fallback;
  if (key == QLatin1String("bgColor")) element.backgroundColor = value.value;
  else if (key == QLatin1String("fontColor")) element.fontColor = value.value;
  else if (key == QLatin1String("borderColor")) element.borderColor = value.value;
  else if (key == QLatin1String("shadowing")) element.shadowing = value.value;
  else if (key == QLatin1String("shape")) element.shape = value.value;
  else if (key == QLatin1String("sprite")) element.sprite = value.value;
  else if (key == QLatin1String("tags")) element.tags = value.value;
  else if (key == QLatin1String("link")) element.link = value.value;
  else if (key == QLatin1String("techn")) element.technology = value.value;
  else if (key == QLatin1String("descr")) element.description = value.value;
  else if (key == QLatin1String("type")) element.type = value.value;
  else if (key == QLatin1String("legendText")) element.legendText = value.value;
  else if (key == QLatin1String("legendSprite")) element.legendSprite = value.value;
}

void assignRelationDynamic(C4Relation& relation, const Attribute& value,
                           const QString& fallback) {
  const QString key = value.keyed ? value.key : fallback;
  if (key == QLatin1String("techn")) relation.technology = value.value;
  else if (key == QLatin1String("descr")) relation.description = value.value;
  else if (key == QLatin1String("sprite")) relation.sprite = value.value;
  else if (key == QLatin1String("tags")) relation.tags = value.value;
  else if (key == QLatin1String("link")) relation.link = value.value;
  else if (key == QLatin1String("textColor")) relation.textColor = value.value;
  else if (key == QLatin1String("lineColor")) relation.lineColor = value.value;
  else if (key == QLatin1String("offsetX")) relation.offsetX = jsParseInt(value.value);
  else if (key == QLatin1String("offsetY")) relation.offsetY = jsParseInt(value.value);
}

struct ParsedCall {
  QString name;
  QVector<Attribute> attributes;
  bool opensBoundary = false;
};

bool isKnownCallName(const QString& name) {
  static const QStringList names = {
      QStringLiteral("Person"), QStringLiteral("Person_Ext"),
      QStringLiteral("System"), QStringLiteral("SystemDb"),
      QStringLiteral("SystemQueue"), QStringLiteral("System_Ext"),
      QStringLiteral("SystemDb_Ext"), QStringLiteral("SystemQueue_Ext"),
      QStringLiteral("Container"), QStringLiteral("ContainerDb"),
      QStringLiteral("ContainerQueue"), QStringLiteral("Container_Ext"),
      QStringLiteral("ContainerDb_Ext"), QStringLiteral("ContainerQueue_Ext"),
      QStringLiteral("Component"), QStringLiteral("ComponentDb"),
      QStringLiteral("ComponentQueue"), QStringLiteral("Component_Ext"),
      QStringLiteral("ComponentDb_Ext"), QStringLiteral("ComponentQueue_Ext"),
      QStringLiteral("Enterprise_Boundary"), QStringLiteral("System_Boundary"),
      QStringLiteral("Container_Boundary"), QStringLiteral("Boundary"),
      QStringLiteral("Deployment_Node"), QStringLiteral("Node"),
      QStringLiteral("Node_L"), QStringLiteral("Node_R"),
      QStringLiteral("Rel"), QStringLiteral("BiRel"),
      QStringLiteral("Rel_Up"), QStringLiteral("Rel_U"),
      QStringLiteral("Rel_Down"), QStringLiteral("Rel_D"),
      QStringLiteral("Rel_Left"), QStringLiteral("Rel_L"),
      QStringLiteral("Rel_Right"), QStringLiteral("Rel_R"),
      QStringLiteral("Rel_Back"), QStringLiteral("RelIndex"),
      QStringLiteral("UpdateElementStyle"), QStringLiteral("UpdateRelStyle"),
      QStringLiteral("UpdateLayoutConfig")};
  return names.contains(name);
}

ParsedCall parseCall(const QString& statement, int line) {
  static const QRegularExpression callStart(QStringLiteral(R"(^([A-Za-z_]+)\s*\()"));
  const auto start = callStart.match(statement);
  if (!start.hasMatch())
    fail(C4ErrorKind::Lexer, line, 1, statement,
         QStringLiteral("Lexical error. Unrecognized text."));

  ParsedCall result;
  result.name = start.captured(1);
  if (!isKnownCallName(result.name))
    fail(C4ErrorKind::Lexer, line, 1, result.name,
         QStringLiteral("Lexical error. Unrecognized C4 statement"));
  qsizetype pos = start.capturedEnd();
  const qsizetype size = statement.size();
  bool closed = false;

  while (pos < size) {
    while (pos < size && statement.at(pos) == u' ') ++pos;
    if (pos >= size) break;
    if (statement.at(pos) == u')') {
      ++pos;
      closed = true;
      break;
    }
    if (statement.at(pos) == u',') {
      result.attributes.push_back({QString(), QString(), false});
      ++pos;
      continue;
    }

    Attribute attribute;
    if (statement.at(pos) == u'$') {
      const qsizetype keyStart = ++pos;
      while (pos < size && statement.at(pos) != u'=') ++pos;
      if (pos >= size)
        fail(C4ErrorKind::Parser, line, static_cast<int>(keyStart),
             statement.mid(keyStart), QStringLiteral("Expected key-value attribute"));
      attribute.keyed = true;
      attribute.key = statement.mid(keyStart, pos - keyStart).trimmed();
      ++pos;
      while (pos < size && statement.at(pos) == u' ') ++pos;
      if (pos >= size || statement.at(pos) != u'\"')
        fail(C4ErrorKind::Parser, line, static_cast<int>(pos) + 1,
             pos < size ? QString(statement.at(pos)) : QString(),
             QStringLiteral("Expected quoted attribute value"));
    }

    if (pos < size && statement.at(pos) == u'\"') {
      ++pos;
      const qsizetype valueStart = pos;
      while (pos < size && statement.at(pos) != u'\"') ++pos;
      if (pos >= size)
        fail(C4ErrorKind::Parser, line, static_cast<int>(valueStart),
             statement.mid(valueStart), QStringLiteral("Unterminated C4 string"));
      attribute.value = statement.mid(valueStart, pos - valueStart);
      ++pos;
      while (pos < size && statement.at(pos) == u' ') ++pos;
      result.attributes.push_back(std::move(attribute));
      if (pos < size && statement.at(pos) == u',') {
        ++pos;
        continue;
      }
      if (pos < size && statement.at(pos) == u')') continue;
      fail(C4ErrorKind::Parser, line, static_cast<int>(pos) + 1,
           QString(statement.at(pos)), QStringLiteral("Expected comma or closing parenthesis"));
    }

    const qsizetype valueStart = pos;
    while (pos < size && statement.at(pos) != u',') ++pos;
    if (pos >= size) {
      // The generated lexer deliberately lets an unquoted final attribute consume ')'.
      fail(C4ErrorKind::Parser, line, static_cast<int>(valueStart) + 1,
           statement.mid(valueStart), QStringLiteral("Unterminated unquoted C4 attribute"));
    }
    attribute.value = statement.mid(valueStart, pos - valueStart).trimmed();
    result.attributes.push_back(std::move(attribute));
    ++pos;
  }

  if (!closed)
    fail(C4ErrorKind::Parser, line, static_cast<int>(statement.size()) + 1,
         QString(), QStringLiteral("Expected closing parenthesis"));
  while (pos < size && statement.at(pos).isSpace()) ++pos;
  if (pos < size && statement.at(pos) == u'{') {
    result.opensBoundary = true;
    ++pos;
  }
  while (pos < size && statement.at(pos).isSpace()) ++pos;
  if (pos != size)
    fail(C4ErrorKind::Parser, line, static_cast<int>(pos) + 1,
         statement.mid(pos), QStringLiteral("Unexpected trailing C4 input"));
  return result;
}

class Parser {
public:
  explicit Parser(QString source) : source_(std::move(source)) {
    C4Element global;
    global.alias = QStringLiteral("global");
    global.label = QStringLiteral("global");
    global.type = QStringLiteral("global");
    global.parentBoundary = QString();
    data_.boundaries.push_back(global);
    boundaryStack_.push_back(QStringLiteral("global"));
  }

  C4Data run() {
    source_.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = source_.split(u'\n', Qt::KeepEmptyParts);
    int header = -1;
    for (int i = 0; i < lines.size(); ++i) {
      const QString text = lines.at(i).trimmed();
      if (text.isEmpty() || text.startsWith(QLatin1String("%%"))) continue;
      header = i;
      static const QStringList headers = {
          QStringLiteral("C4Context"), QStringLiteral("C4Container"),
          QStringLiteral("C4Component"), QStringLiteral("C4Dynamic"),
          QStringLiteral("C4Deployment")};
      if (!headers.contains(text))
        fail(C4ErrorKind::Parser, i + 1, 1, text,
             QStringLiteral("Expected C4 diagram header"));
      data_.c4Type = text;
      break;
    }
    if (header < 0)
      fail(C4ErrorKind::Parser, 1, 1, QString(),
           QStringLiteral("Expected C4 diagram header"));

    bool sawStatement = false;
    for (int i = header + 1; i < lines.size(); ++i) {
      QString text = lines.at(i).trimmed();
      if (text.isEmpty() || text.startsWith(QLatin1String("%%"))) continue;
      sawStatement = true;

      if (text.startsWith(QLatin1String("accDescr")) && text.contains(u'{')) {
        const qsizetype brace = text.indexOf(u'{');
        QString value = text.mid(brace + 1);
        while (!value.contains(u'}') && ++i < lines.size()) {
          value += u'\n';
          value += lines.at(i);
        }
        const qsizetype end = value.indexOf(u'}');
        if (end < 0)
          fail(C4ErrorKind::Parser, i + 1, 1, QString(),
               QStringLiteral("Unterminated accDescr block"));
        value = value.left(end).trimmed();
        static const QRegularExpression indent(QStringLiteral("\\n\\s+"));
        value.replace(indent, QStringLiteral("\n"));
        data_.accDescr = sanitizeMetadata(value);
        continue;
      }
      if (text.startsWith(QLatin1String("title "))) {
        data_.title = sanitizeMetadata(text.mid(6));
        continue;
      }
      if (text.startsWith(QLatin1String("accDescription "))) {
        data_.accDescr = sanitizeMetadata(text.mid(15));
        continue;
      }
      if (text.startsWith(QLatin1String("accTitle"))) {
        const qsizetype colon = text.indexOf(u':');
        if (colon < 0) fail(C4ErrorKind::Parser, i + 1, 1, text,
                            QStringLiteral("Expected accTitle colon"));
        // Upstream C4's generated action accidentally writes accTitle into title.
        data_.title = sanitizeMetadata(text.mid(colon + 1));
        continue;
      }
      if (text.startsWith(QLatin1String("accDescr"))) {
        const qsizetype colon = text.indexOf(u':');
        if (colon < 0) fail(C4ErrorKind::Parser, i + 1, 1, text,
                            QStringLiteral("Expected accDescr colon"));
        data_.accDescr = sanitizeMetadata(text.mid(colon + 1));
        continue;
      }
      if (text.startsWith(QLatin1String("direction")) || text.contains(QLatin1String("direction ")))
        fail(C4ErrorKind::Parser, i + 1, 1, text,
             QStringLiteral("Unexpected C4 direction statement"));
      if (text == QLatin1String("}")) {
        if (boundaryStack_.size() <= 1)
          fail(C4ErrorKind::Parser, i + 1, 1, text,
               QStringLiteral("Unexpected boundary close"));
        boundaryStack_.removeLast();
        continue;
      }

      QString statement = lines.at(i).trimmed();
      bool quote = false;
      int parentheses = 0;
      auto scan = [&](const QString& part) {
        for (QChar ch : part) {
          if (ch == u'\"') quote = !quote;
          else if (!quote && ch == u'(') ++parentheses;
          else if (!quote && ch == u')') --parentheses;
        }
      };
      scan(statement);
      while ((quote || parentheses > 0) && i + 1 < lines.size()) {
        statement += u'\n';
        statement += lines.at(++i);
        scan(lines.at(i));
      }
      while (true) {
        try {
          dispatch(parseCall(statement, i + 1));
          break;
        } catch (const C4ParseError& error) {
          const QString message = QString::fromStdString(error.what());
          if (!message.contains(QLatin1String("Unterminated unquoted C4 attribute")) ||
              i + 1 >= lines.size())
            throw;
          // Jison's final unquoted ATTRIBUTE rule is `[^,]+`: it consumes the
          // closing parenthesis and newlines until a later comma appears.
          statement += u'\n';
          statement += lines.at(++i);
        }
      }
    }

    if (!sawStatement)
      fail(C4ErrorKind::Parser, header + 2, 1, QString(),
           QStringLiteral("Expected C4 statement"));
    if (boundaryStack_.size() != 1)
      fail(C4ErrorKind::Parser, lines.size(), 1, QString(),
           QStringLiteral("Expected boundary close"));
    return data_;
  }

private:
  C4Element* findElement(const QString& alias) {
    for (auto& shape : data_.shapes) if (shape.alias == alias) return &shape;
    for (auto& boundary : data_.boundaries) if (boundary.alias == alias) return &boundary;
    return nullptr;
  }

  C4Relation* findRelation(const QString& from, const QString& to) {
    for (auto& relation : data_.relations)
      if (relation.from == from && relation.to == to) return &relation;
    return nullptr;
  }

  C4Element& upsertShape(const QString& alias) {
    for (auto& shape : data_.shapes) if (shape.alias == alias) return shape;
    C4Element shape;
    shape.alias = alias;
    data_.shapes.push_back(shape);
    return data_.shapes.last();
  }

  C4Element& upsertBoundary(const QString& alias) {
    for (auto& boundary : data_.boundaries) if (boundary.alias == alias) return boundary;
    C4Element boundary;
    boundary.alias = alias;
    data_.boundaries.push_back(boundary);
    return data_.boundaries.last();
  }

  void addShape(const ParsedCall& call, const QString& type, bool technology) {
    if (call.attributes.size() < 2) return;
    const QString alias = valueAt(call.attributes, 0);
    const QString label = valueAt(call.attributes, 1);
    C4Element& shape = upsertShape(alias);
    shape.label = label;
    shape.typeC4Shape = type;
    shape.parentBoundary = boundaryStack_.last();
    shape.technology.clear();
    shape.description.clear();
    if (technology) {
      if (const auto value = attributeAt(call.attributes, 2)) assignElementDynamic(shape, *value, QStringLiteral("techn"));
      if (const auto value = attributeAt(call.attributes, 3)) assignElementDynamic(shape, *value, QStringLiteral("descr"));
      if (const auto value = attributeAt(call.attributes, 4)) assignElementDynamic(shape, *value, QStringLiteral("sprite"));
      if (const auto value = attributeAt(call.attributes, 5)) assignElementDynamic(shape, *value, QStringLiteral("tags"));
      if (const auto value = attributeAt(call.attributes, 6)) assignElementDynamic(shape, *value, QStringLiteral("link"));
    } else {
      if (const auto value = attributeAt(call.attributes, 2)) assignElementDynamic(shape, *value, QStringLiteral("descr"));
      if (const auto value = attributeAt(call.attributes, 3)) assignElementDynamic(shape, *value, QStringLiteral("sprite"));
      if (const auto value = attributeAt(call.attributes, 4)) assignElementDynamic(shape, *value, QStringLiteral("tags"));
      if (const auto value = attributeAt(call.attributes, 5)) assignElementDynamic(shape, *value, QStringLiteral("link"));
    }
  }

  void addBoundary(const ParsedCall& call, const QString& defaultType,
                   const QString& nodeType = QString()) {
    if (call.attributes.size() < 2) return;
    C4Element& boundary = upsertBoundary(valueAt(call.attributes, 0));
    boundary.label = valueAt(call.attributes, 1);
    boundary.type = defaultType;
    boundary.description.clear();
    boundary.nodeType = nodeType;
    boundary.parentBoundary = boundaryStack_.last();
    if (const auto value = attributeAt(call.attributes, 2)) assignElementDynamic(boundary, *value, QStringLiteral("type"));
    if (!nodeType.isEmpty()) {
      if (const auto value = attributeAt(call.attributes, 3)) assignElementDynamic(boundary, *value, QStringLiteral("descr"));
      if (const auto value = attributeAt(call.attributes, 4)) assignElementDynamic(boundary, *value, QStringLiteral("sprite"));
      if (const auto value = attributeAt(call.attributes, 5)) assignElementDynamic(boundary, *value, QStringLiteral("tags"));
      if (const auto value = attributeAt(call.attributes, 6)) assignElementDynamic(boundary, *value, QStringLiteral("link"));
    } else {
      if (const auto value = attributeAt(call.attributes, 3)) assignElementDynamic(boundary, *value, QStringLiteral("tags"));
      if (const auto value = attributeAt(call.attributes, 4)) assignElementDynamic(boundary, *value, QStringLiteral("link"));
    }
    if (!call.opensBoundary)
      fail(C4ErrorKind::Parser, 1, 1, call.name,
           QStringLiteral("Expected opening boundary brace"));
    boundaryStack_.push_back(boundary.alias);
  }

  void addRelation(const ParsedCall& call, const QString& type, qsizetype offset = 0) {
    if (call.attributes.size() < offset + 3) return;
    const QString from = valueAt(call.attributes, offset);
    const QString to = valueAt(call.attributes, offset + 1);
    C4Relation* relation = findRelation(from, to);
    if (!relation) {
      C4Relation value;
      data_.relations.push_back(value);
      relation = &data_.relations.last();
    }
    relation->type = type;
    relation->from = from;
    relation->to = to;
    relation->label = valueAt(call.attributes, offset + 2);
    relation->technology.clear();
    relation->description.clear();
    if (const auto value = attributeAt(call.attributes, offset + 3)) assignRelationDynamic(*relation, *value, QStringLiteral("techn"));
    if (const auto value = attributeAt(call.attributes, offset + 4)) assignRelationDynamic(*relation, *value, QStringLiteral("descr"));
    if (const auto value = attributeAt(call.attributes, offset + 5)) assignRelationDynamic(*relation, *value, QStringLiteral("sprite"));
    if (const auto value = attributeAt(call.attributes, offset + 6)) assignRelationDynamic(*relation, *value, QStringLiteral("tags"));
    if (const auto value = attributeAt(call.attributes, offset + 7)) assignRelationDynamic(*relation, *value, QStringLiteral("link"));
  }

  void updateElement(const ParsedCall& call) {
    C4Element* element = findElement(valueAt(call.attributes, 0));
    if (!element) return;
    static const QStringList fields = {QStringLiteral("bgColor"), QStringLiteral("fontColor"),
        QStringLiteral("borderColor"), QStringLiteral("shadowing"), QStringLiteral("shape"),
        QStringLiteral("sprite"), QStringLiteral("techn"), QStringLiteral("legendText"),
        QStringLiteral("legendSprite")};
    for (qsizetype i = 1; i < call.attributes.size() && i <= fields.size(); ++i)
      assignElementDynamic(*element, call.attributes.at(i), fields.at(i - 1));
  }

  void updateRelation(const ParsedCall& call) {
    C4Relation* relation = findRelation(valueAt(call.attributes, 0), valueAt(call.attributes, 1));
    if (!relation) return;
    static const QStringList fields = {QStringLiteral("textColor"), QStringLiteral("lineColor"),
                                       QStringLiteral("offsetX"), QStringLiteral("offsetY")};
    for (qsizetype i = 2; i < call.attributes.size() && i - 2 < fields.size(); ++i)
      assignRelationDynamic(*relation, call.attributes.at(i), fields.at(i - 2));
  }

  void dispatch(const ParsedCall& call) {
    const QString& name = call.name;
    if (name == QLatin1String("Person")) addShape(call, QStringLiteral("person"), false);
    else if (name == QLatin1String("Person_Ext")) addShape(call, QStringLiteral("external_person"), false);
    else if (name == QLatin1String("System")) addShape(call, QStringLiteral("system"), false);
    else if (name == QLatin1String("SystemDb")) addShape(call, QStringLiteral("system_db"), false);
    else if (name == QLatin1String("SystemQueue")) addShape(call, QStringLiteral("system_queue"), false);
    else if (name == QLatin1String("System_Ext")) addShape(call, QStringLiteral("external_system"), false);
    else if (name == QLatin1String("SystemDb_Ext")) addShape(call, QStringLiteral("external_system_db"), false);
    else if (name == QLatin1String("SystemQueue_Ext")) addShape(call, QStringLiteral("external_system_queue"), false);
    else if (name == QLatin1String("Container")) addShape(call, QStringLiteral("container"), true);
    else if (name == QLatin1String("ContainerDb")) addShape(call, QStringLiteral("container_db"), true);
    else if (name == QLatin1String("ContainerQueue")) addShape(call, QStringLiteral("container_queue"), true);
    else if (name == QLatin1String("Container_Ext")) addShape(call, QStringLiteral("external_container"), true);
    else if (name == QLatin1String("ContainerDb_Ext")) addShape(call, QStringLiteral("external_container_db"), true);
    else if (name == QLatin1String("ContainerQueue_Ext")) addShape(call, QStringLiteral("external_container_queue"), true);
    else if (name == QLatin1String("Component")) addShape(call, QStringLiteral("component"), true);
    else if (name == QLatin1String("ComponentDb")) addShape(call, QStringLiteral("component_db"), true);
    else if (name == QLatin1String("ComponentQueue")) addShape(call, QStringLiteral("component_queue"), true);
    else if (name == QLatin1String("Component_Ext")) addShape(call, QStringLiteral("external_component"), true);
    else if (name == QLatin1String("ComponentDb_Ext")) addShape(call, QStringLiteral("external_component_db"), true);
    else if (name == QLatin1String("ComponentQueue_Ext")) addShape(call, QStringLiteral("external_component_queue"), true);
    else if (name == QLatin1String("Enterprise_Boundary")) addBoundary(call, QStringLiteral("ENTERPRISE"));
    else if (name == QLatin1String("System_Boundary")) addBoundary(call, QStringLiteral("SYSTEM"));
    else if (name == QLatin1String("Container_Boundary")) addBoundary(call, QStringLiteral("CONTAINER"));
    else if (name == QLatin1String("Boundary")) addBoundary(call, QStringLiteral("system"));
    else if (name == QLatin1String("Deployment_Node")) addBoundary(call, QStringLiteral("node"), QStringLiteral("node"));
    else if (name == QLatin1String("Node") || name == QLatin1String("Node_L") || name == QLatin1String("Node_R"))
      addBoundary(call, QStringLiteral("node"), name == QLatin1String("Node_L") ? QStringLiteral("nodeL") : name == QLatin1String("Node_R") ? QStringLiteral("nodeR") : QStringLiteral("node"));
    else if (name == QLatin1String("Rel")) addRelation(call, QStringLiteral("rel"));
    else if (name == QLatin1String("BiRel")) addRelation(call, QStringLiteral("birel"));
    else if (name == QLatin1String("Rel_Up") || name == QLatin1String("Rel_U")) addRelation(call, QStringLiteral("rel_u"));
    else if (name == QLatin1String("Rel_Down") || name == QLatin1String("Rel_D")) addRelation(call, QStringLiteral("rel_d"));
    else if (name == QLatin1String("Rel_Left") || name == QLatin1String("Rel_L")) addRelation(call, QStringLiteral("rel_l"));
    else if (name == QLatin1String("Rel_Right") || name == QLatin1String("Rel_R")) addRelation(call, QStringLiteral("rel_r"));
    else if (name == QLatin1String("Rel_Back")) addRelation(call, QStringLiteral("rel_b"));
    else if (name == QLatin1String("RelIndex")) addRelation(call, QStringLiteral("rel"), 1);
    else if (name == QLatin1String("UpdateElementStyle")) updateElement(call);
    else if (name == QLatin1String("UpdateRelStyle")) updateRelation(call);
    else if (name == QLatin1String("UpdateLayoutConfig")) {
      const int shapes = jsParseInt(valueAt(call.attributes, 0));
      const int boundaries = jsParseInt(valueAt(call.attributes, 1));
      if (shapes >= 1) data_.shapeInRow = shapes;
      if (boundaries >= 1) data_.boundaryInRow = boundaries;
    } else {
      fail(C4ErrorKind::Lexer, 1, 1, name,
           QStringLiteral("Lexical error. Unrecognized C4 statement"));
    }
  }

  QString source_;
  C4Data data_;
  QVector<QString> boundaryStack_;
};

}  // namespace

C4ParseError::C4ParseError(C4ErrorKind kindValue, int lineValue, int columnValue,
                           QString tokenValue, const QString& message)
    : std::runtime_error(message.toStdString()), kind(kindValue), line(lineValue),
      column(columnValue), token(std::move(tokenValue)) {}

C4Data C4Diagram::parse(const QString& source, bool wrap) {
  C4Data data = Parser(source).run();
  data.wrap = wrap;
  return data;
}

}  // namespace muffin::mermaid::c4

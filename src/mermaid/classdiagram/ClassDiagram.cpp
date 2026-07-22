#include "mermaid/classdiagram/ClassDiagram.h"

#include <QJsonArray>
#include <QMap>
#include <algorithm>

namespace muffin::mermaid::classdiagram {
namespace {

QString htmlText(QString text) {
  return text.replace(QLatin1Char('<'), QStringLiteral("&lt;"))
      .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
}

QString memberHtmlText(QString text) {
  qsizetype cursor = 0;
  while ((cursor = text.indexOf(QLatin1Char('~'), cursor)) >= 0) {
    const qsizetype closing = text.indexOf(QLatin1Char('~'), cursor + 1);
    if (closing < 0) break;
    text.replace(closing, 1, QStringLiteral(">"));
    text.replace(cursor, 1, QStringLiteral("<"));
    cursor = closing + 1;
  }
  return htmlText(std::move(text));
}

ClassSourceSpan spanForOffset(const QString& source, qsizetype offset, qsizetype length = 0) {
  ClassSourceSpan span{offset, length, 1, 0};
  for (qsizetype i = 0; i < std::min(offset, source.size()); ++i) {
    if (source[i] == QLatin1Char('\n')) { ++span.line; span.column = 0; }
    else ++span.column;
  }
  return span;
}

[[noreturn]] void raise(const QString& source, qsizetype offset, ClassErrorStage stage,
                        ClassErrorCode code, QString production, QString actual,
                        QStringList expected, QString detail = {}) {
  throw ClassParseError({stage, code, spanForOffset(source, offset), std::move(production),
                         std::move(actual), std::move(expected), std::move(detail)});
}

struct ParsedName {
  QString id;
  QString type;
  qsizetype offset = -1;
};

ClassMember parseMember(QString input) {
  input = input.trimmed();
  ClassMember member;
  member.memberType = input.contains(QLatin1Char(')')) ? QStringLiteral("method")
                                                       : QStringLiteral("attribute");
  if (!input.isEmpty() && QStringView(u"#+~-").contains(input.front())) {
    member.visibility = input.front();
    input.remove(0, 1);
  }
  input = input.trimmed();
  if (!input.isEmpty() && (input.back() == QLatin1Char('$') || input.back() == QLatin1Char('*'))) {
    member.classifier = input.back();
    input.chop(1);
    input = input.trimmed();
  }
  if (member.memberType == QLatin1String("method")) {
    const qsizetype open = input.indexOf(QLatin1Char('('));
    const qsizetype close = input.lastIndexOf(QLatin1Char(')'));
    member.id = input.left(open).trimmed();
    member.parameters = input.mid(open + 1, close - open - 1).trimmed();
    member.returnType = input.mid(close + 1).trimmed();
    member.text = (member.visibility.isEmpty() ? QString{} : QStringLiteral("\\") + member.visibility) +
        memberHtmlText(member.id) + QLatin1Char('(') +
        memberHtmlText(member.parameters) + QLatin1Char(')');
    if (!member.returnType.isEmpty())
      member.text += QStringLiteral(" : ") + memberHtmlText(member.returnType);
  } else {
    member.id = input;
    member.text = (member.visibility.isEmpty() ? QString{} : QStringLiteral("\\") + member.visibility) +
        memberHtmlText(member.id);
  }
  return member;
}

class Parser {
public:
  Parser(QString source, ClassLimits limits)
      : source_(std::move(source)), limits_(limits), tokens_(ClassTokenizer(source_).tokenize()) {}

  ClassDiagramData parse() {
    if (source_.size() > limits_.maxTextSize)
      raise(source_, 0, ClassErrorStage::Resource, ClassErrorCode::LimitExceeded,
            QStringLiteral("start"), QStringLiteral("text"), {}, QStringLiteral("Maximum class diagram text size exceeded"));
    for (const ClassToken& token : tokens_)
      if (token.kind == ClassTokenKind::Invalid)
        raise(source_, token.offset, ClassErrorStage::Lexer, ClassErrorCode::UnexpectedToken,
              QStringLiteral("token"), token.text, {}, QStringLiteral("Invalid class diagram token"));

    bool headerSeen = false;
    bool accDescription = false;
    ClassTokenCursor document(tokens_);
    while (!document.atEnd() && document.peek().kind != ClassTokenKind::Eof) {
      ClassTokenCursor line = document.consumeLine();
      const QString text = line.raw(source_);
      const qsizetype lineOffset = line.atEnd() ? document.peek().offset : line.peek().offset;
      if (accDescription) {
        if (line.match(ClassTokenKind::RBrace) && line.atEnd()) {
          accDescription = false;
        } else {
          if (!data_.accDescription.isEmpty()) data_.accDescription += QLatin1Char('\n');
          data_.accDescription += text;
        }
        continue;
      }
      if (line.atEnd()) continue;
      if (!headerSeen) {
        if (line.match(ClassTokenKind::Header) && line.atEnd()) {
          headerSeen = true;
          continue;
        }
        raise(source_, lineOffset, ClassErrorStage::Detector, ClassErrorCode::MissingHeader,
              QStringLiteral("graphConfig"), text, {QStringLiteral("CLASS_DIAGRAM")});
      }
      if (line.peek().kind == ClassTokenKind::Word &&
          line.peek().text == QLatin1String("accTitle")) {
        line.consume();
        if (!line.match(ClassTokenKind::Colon))
          unexpected(line, lineOffset, QStringLiteral("acc_title"), {QStringLiteral("COLON")});
        data_.accTitle = line.raw(source_);
        continue;
      }
      if (line.peek().kind == ClassTokenKind::Word &&
          line.peek().text == QLatin1String("accDescr")) {
        line.consume();
        if (line.match(ClassTokenKind::Colon)) {
          data_.accDescription = line.raw(source_);
        } else if (line.match(ClassTokenKind::LBrace) && line.atEnd()) {
          accDescription = true;
          data_.accDescription.clear();
        } else {
          unexpected(line, lineOffset, QStringLiteral("acc_descr"),
                     {QStringLiteral("COLON"), QStringLiteral("STRUCT_START")});
        }
        continue;
      }
      if (line.matchWord(u"direction") && !line.atEnd()) {
        const QString direction = line.consume().text;
        if (direction == QLatin1String("TB") || direction == QLatin1String("BT") ||
            direction == QLatin1String("LR") || direction == QLatin1String("RL"))
          data_.direction = direction;
        continue;
      }
      if (line.matchWord(u"title")) continue;
      parseStatement(line, lineOffset);
    }
    if (!headerSeen)
      raise(source_, 0, ClassErrorStage::Detector, ClassErrorCode::MissingHeader,
            QStringLiteral("graphConfig"), QStringLiteral("EOF"), {QStringLiteral("CLASS_DIAGRAM")});
    if (accDescription || !scopes_.isEmpty())
      raise(source_, source_.size(), ClassErrorStage::Parser, ClassErrorCode::MissingClosingBrace,
            QStringLiteral("classStatement"), QStringLiteral("EOF_IN_STRUCT"),
            {QStringLiteral("STRUCT_STOP")});
    return data_;
  }

private:
  enum class ScopeKind { Class, Namespace };
  struct Scope { ScopeKind kind; QString id; };

  ClassNode& addClass(const QString& id, const QString& type,
                      qsizetype offset) {
    auto found = std::find_if(data_.classes.begin(), data_.classes.end(),
                              [&](const ClassNode& value) { return value.id == id; });
    if (found != data_.classes.end()) return *found;
    if (data_.classes.size() >= limits_.maxClasses)
      raise(source_, offset, ClassErrorStage::Resource, ClassErrorCode::LimitExceeded,
            QStringLiteral("classIdentifier"), id, {}, QStringLiteral("Maximum class count exceeded"));
    ClassNode node;
    node.id = id;
    node.type = type;
    node.label = id;
    node.text = type.isEmpty() ? id : id + QStringLiteral("&lt;") + type + QStringLiteral("&gt;");
    if (!namespaceStack_.isEmpty()) node.parent = namespaceStack_.back();
    data_.classes.append(std::move(node));
    if (!namespaceStack_.isEmpty()) namespaceById(namespaceStack_.back()).classKeys.append(id);
    return data_.classes.back();
  }

  ClassNode* findClass(const QString& id) {
    auto found = std::find_if(data_.classes.begin(), data_.classes.end(),
                              [&](const ClassNode& value) { return value.id == id; });
    return found == data_.classes.end() ? nullptr : &*found;
  }

  ClassNamespace& namespaceById(const QString& id) {
    auto found = std::find_if(data_.namespaces.begin(), data_.namespaces.end(),
                              [&](const ClassNamespace& value) { return value.id == id; });
    return *found;
  }

  void addNamespace(QString id, QString label, qsizetype offset) {
    if (namespaceStack_.size() >= limits_.maxNamespaceDepth)
      raise(source_, offset, ClassErrorStage::Resource, ClassErrorCode::LimitExceeded,
            QStringLiteral("namespaceStatement"), id, {},
            QStringLiteral("Maximum namespace depth exceeded"));
    const QString qualified = namespaceStack_.isEmpty() ? id : namespaceStack_.back() + QLatin1Char('.') + id;
    const QStringList parts = qualified.split(QLatin1Char('.'));
    QString current;
    for (qsizetype index = 0; index < parts.size(); ++index) {
      const QString previous = current;
      current = current.isEmpty() ? parts[index] : current + QLatin1Char('.') + parts[index];
      auto found = std::find_if(data_.namespaces.begin(), data_.namespaces.end(),
                                [&](const ClassNamespace& value) { return value.id == current; });
      if (found == data_.namespaces.end()) {
        if (data_.namespaces.size() >= limits_.maxNamespaces)
          raise(source_, offset, ClassErrorStage::Resource, ClassErrorCode::LimitExceeded,
                QStringLiteral("namespaceStatement"), id, {},
                QStringLiteral("Maximum namespace count exceeded"));
        ClassNamespace item;
        item.id = current;
        item.label = parts[index];
        item.parent = previous;
        item.explicitDeclaration = index == parts.size() - 1;
        data_.namespaces.append(std::move(item));
        if (!previous.isEmpty()) namespaceById(previous).childKeys.append(current);
      } else if (index == parts.size() - 1) {
        found->explicitDeclaration = true;
      }
    }
    ClassNamespace& item = namespaceById(qualified);
    if (!label.isEmpty()) item.label = label;
    namespaceStack_.append(qualified);
    scopes_.append({ScopeKind::Namespace, qualified});
  }

  void addAnnotation(ClassNode& node, const QString& annotation,
                     qsizetype offset) {
    if (memberCount_ >= limits_.maxMembers)
      raise(source_, offset, ClassErrorStage::Resource, ClassErrorCode::LimitExceeded,
            QStringLiteral("annotationStatement"), annotation, {},
            QStringLiteral("Maximum class member count exceeded"));
    node.annotations.append(annotation);
    ++memberCount_;
  }

  void addMember(ClassNode& node, const QString& text, qsizetype offset) {
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QLatin1String("<<")) && trimmed.endsWith(QLatin1String(">>"))) {
      addAnnotation(node, trimmed.mid(2, trimmed.size() - 4), offset);
      return;
    }
    if (memberCount_ >= limits_.maxMembers)
      raise(source_, offset, ClassErrorStage::Resource, ClassErrorCode::LimitExceeded,
            QStringLiteral("memberStatement"), trimmed, {},
            QStringLiteral("Maximum class member count exceeded"));
    const ClassMember member = parseMember(trimmed);
    if (member.memberType == QLatin1String("method")) {
      node.methods.append(member);
      ++memberCount_;
    } else if (!member.id.isEmpty()) {
      node.members.append(member);
      ++memberCount_;
    }
  }

  void addNote(QString text, QString className, qsizetype offset) {
    if (data_.notes.size() >= limits_.maxNotes)
      raise(source_, offset, ClassErrorStage::Resource, ClassErrorCode::LimitExceeded,
            QStringLiteral("noteStatement"), text, {},
            QStringLiteral("Maximum class note count exceeded"));
    ClassNote note;
    note.index = data_.notes.size();
    note.id = QStringLiteral("note%1").arg(note.index);
    note.className = std::move(className);
    note.text = std::move(text);
    if (!namespaceStack_.isEmpty()) {
      note.parent = namespaceStack_.back();
      namespaceById(note.parent).noteKeys.append(note.id);
    }
    data_.notes.append(std::move(note));
  }

  ParsedName parseName(ClassTokenCursor& cursor) const {
    if (cursor.atEnd() || (cursor.peek().kind != ClassTokenKind::Word &&
                           cursor.peek().kind != ClassTokenKind::Backtick))
      return {};
    ParsedName name;
    const ClassToken& token = cursor.consume();
    name.id = token.text;
    name.offset = token.offset;
    if (!cursor.atEnd() && cursor.peek().kind == ClassTokenKind::Generic) {
      const ClassToken& generic = cursor.consume();
      name.type = generic.text;
    }
    return name;
  }

  [[noreturn]] void unexpected(const ClassTokenCursor& cursor, qsizetype fallback,
                               QString production, QStringList expected = {}) const {
    const qsizetype offset = cursor.atEnd() ? fallback : cursor.peek().offset;
    const QString actual = cursor.atEnd() ? QStringLiteral("NEWLINE")
                                          : classTokenName(cursor.peek().kind);
    raise(source_, offset, ClassErrorStage::Parser, ClassErrorCode::UnexpectedToken,
          std::move(production), actual, std::move(expected));
  }

  void parseStatement(ClassTokenCursor line, qsizetype offset) {
    if (!line.atEnd() && line.peek().kind == ClassTokenKind::RBrace) {
      line.consume();
      if (scopes_.isEmpty())
        raise(source_, offset, ClassErrorStage::Lexer, ClassErrorCode::UnexpectedToken,
              QStringLiteral("statement"), QStringLiteral("STRUCT_STOP"), {});
      const Scope scope = scopes_.takeLast();
      if (scope.kind == ScopeKind::Namespace) namespaceStack_.removeLast();
      if (!line.atEnd()) parseStatement(line, line.peek().offset);
      return;
    }
    for (qsizetype index = line.position(); index < line.endPosition(); ++index) {
      if (tokens_[index].kind != ClassTokenKind::RBrace) continue;
      ClassTokenCursor statement(tokens_, line.position(), index);
      ClassTokenCursor closing(tokens_, index, line.endPosition());
      parseStatement(statement, offset);
      parseStatement(closing, tokens_[index].offset);
      return;
    }
    if (!scopes_.isEmpty() && scopes_.back().kind == ScopeKind::Class) {
      addMember(*findClass(scopes_.back().id), line.raw(source_), offset);
      return;
    }
    if (line.matchWord(u"namespace")) {
      parseNamespace(line, offset);
      return;
    }
    if (line.matchWord(u"class")) {
      parseClass(line, offset);
      return;
    }
    if (line.match(ClassTokenKind::AnnotationStart)) {
      if (line.atEnd()) unexpected(line, offset, QStringLiteral("annotationStatement"));
      const QString annotation = line.consume().text;
      if (!line.match(ClassTokenKind::AnnotationEnd))
        unexpected(line, offset, QStringLiteral("annotationStatement"),
                   {QStringLiteral("ANNOTATION_END")});
      const ParsedName name = parseName(line);
      if (name.id.isEmpty() || !line.atEnd())
        unexpected(line, offset, QStringLiteral("annotationStatement"),
                   {QStringLiteral("className")});
      addAnnotation(addClass(name.id, name.type, name.offset), annotation, offset);
      return;
    }
    if (line.matchWord(u"note")) {
      parseNote(line, offset);
      return;
    }
    if (line.matchWord(u"style")) { parseStyle(line, offset); return; }
    if (line.matchWord(u"classDef")) { parseClassDef(line, offset); return; }
    if (line.matchWord(u"cssClass")) { parseCssClass(line, offset); return; }
    if (!line.atEnd() && line.peek().kind == ClassTokenKind::Word &&
        (line.peek().text == QLatin1String("link") ||
         line.peek().text == QLatin1String("callback") ||
         line.peek().text == QLatin1String("click"))) {
      parseInteraction(line, offset);
      return;
    }

    ClassTokenCursor memberProbe = line;
    const ParsedName memberName = parseName(memberProbe);
    if (!memberName.id.isEmpty() && memberProbe.match(ClassTokenKind::Colon)) {
      addMember(addClass(memberName.id, memberName.type, memberName.offset),
                memberProbe.raw(source_), offset);
      return;
    }
    if (parseRelation(line, offset)) return;
    const ParsedName name = parseName(line);
    if (name.id.isEmpty() || !line.atEnd() || name.id.startsWith(QLatin1Char('+')))
      raise(source_, offset, ClassErrorStage::Parser, ClassErrorCode::UnexpectedToken,
            QStringLiteral("statement"), line.raw(source_),
            {QStringLiteral("classStatement"), QStringLiteral("relationStatement")});
  }

  void parseNamespace(ClassTokenCursor cursor, qsizetype offset) {
    QString namespaceName;
    while (!cursor.atEnd() && (cursor.peek().kind == ClassTokenKind::Word ||
                               cursor.peek().kind == ClassTokenKind::Backtick))
      namespaceName += cursor.consume().text;
    if (namespaceName.isEmpty())
      unexpected(cursor, offset, QStringLiteral("namespaceIdentifier"),
                 {QStringLiteral("namespaceName")});
    QString label;
    if (cursor.match(ClassTokenKind::LBracket)) {
      if (cursor.atEnd() || (cursor.peek().kind != ClassTokenKind::String &&
                             cursor.peek().kind != ClassTokenKind::Word))
        unexpected(cursor, offset, QStringLiteral("classLabel"), {QStringLiteral("STR")});
      label = cursor.consume().text;
      if (!cursor.match(ClassTokenKind::RBracket))
        unexpected(cursor, offset, QStringLiteral("classLabel"), {QStringLiteral("SQE")});
    }
    if (!cursor.match(ClassTokenKind::LBrace))
      unexpected(cursor, offset, QStringLiteral("namespaceStatement"),
                 {QStringLiteral("STRUCT_START")});
    addNamespace(namespaceName, label, offset);
    if (!cursor.atEnd()) parseStatement(cursor, cursor.peek().offset);
  }

  void parseClass(ClassTokenCursor cursor, qsizetype offset) {
    const ParsedName name = parseName(cursor);
    if (name.id.isEmpty())
      raise(source_, offset + 5, ClassErrorStage::Parser, ClassErrorCode::UnexpectedToken,
            QStringLiteral("classIdentifier"), QStringLiteral("NEWLINE"),
            {QStringLiteral("className")});
    ClassNode& node = addClass(name.id, name.type, name.offset);
    if (!cursor.atEnd() && cursor.peek().kind == ClassTokenKind::LBracket) {
      const ClassToken openingBracket = cursor.consume();
      if (cursor.atEnd() || (cursor.peek().kind != ClassTokenKind::String &&
                             cursor.peek().kind != ClassTokenKind::Word))
        raise(source_, openingBracket.offset,
              ClassErrorStage::Parser, ClassErrorCode::InvalidClassLabel,
              QStringLiteral("classLabel"), cursor.raw(source_),
              {QStringLiteral("STR"), QStringLiteral("SQE")});
      node.label = cursor.consume().text;
      if (!cursor.match(ClassTokenKind::RBracket))
        raise(source_, openingBracket.offset,
              ClassErrorStage::Parser, ClassErrorCode::InvalidClassLabel,
              QStringLiteral("classLabel"), cursor.raw(source_), {QStringLiteral("SQE")});
      if (!node.type.isEmpty()) {
        const QString suffix =
            QStringLiteral("<") + node.type + QStringLiteral(">");
        if (node.label.endsWith(suffix)) node.label.chop(suffix.size());
      }
      node.text = node.label + (node.type.isEmpty() ? QString{}
          : QStringLiteral("<") + node.type + QStringLiteral(">"));
    }
    if (cursor.match(ClassTokenKind::StyleSeparator)) {
      if (cursor.atEnd() || cursor.peek().kind != ClassTokenKind::Word)
        unexpected(cursor, offset, QStringLiteral("classStatement"),
                   {QStringLiteral("alphaNumToken")});
      node.cssClasses += QLatin1Char(' ') + cursor.consume().text;
      if (!cursor.atEnd() && cursor.peek().kind == ClassTokenKind::StyleSeparator)
        unexpected(cursor, offset, QStringLiteral("classStatement"),
                   {QStringLiteral("NEWLINE"), QStringLiteral("STRUCT_START")});
    }
    if (cursor.match(ClassTokenKind::AnnotationStart)) {
      if (cursor.atEnd()) unexpected(cursor, offset, QStringLiteral("classStatement"));
      node.annotations.append(cursor.consume().text);
      if (!cursor.match(ClassTokenKind::AnnotationEnd))
        unexpected(cursor, offset, QStringLiteral("classStatement"),
                   {QStringLiteral("ANNOTATION_END")});
    }
    if (cursor.match(ClassTokenKind::LBrace)) {
      if (cursor.match(ClassTokenKind::RBrace) && cursor.atEnd()) return;
      scopes_.append({ScopeKind::Class, node.id});
      if (!cursor.atEnd()) addMember(node, cursor.raw(source_), offset);
      return;
    }
    if (!cursor.atEnd()) unexpected(cursor, offset, QStringLiteral("classStatement"));
  }

  struct RelationParts { QString left; QString line; QString right; };

  static bool splitRelation(const QString& value, RelationParts& parts) {
    qsizetype lineOffset = value.indexOf(QLatin1String("--"));
    QString line = QStringLiteral("--");
    const qsizetype dottedOffset = value.indexOf(QLatin1String(".."));
    if (lineOffset < 0 || (dottedOffset >= 0 && dottedOffset < lineOffset)) {
      lineOffset = dottedOffset;
      line = QStringLiteral("..");
    }
    if (lineOffset < 0 || value.indexOf(QLatin1String("--"), lineOffset + 2) >= 0 ||
        value.indexOf(QLatin1String(".."), lineOffset + 2) >= 0)
      return false;
    const QString left = value.left(lineOffset);
    const QString right = value.mid(lineOffset + 2);
    const QStringList leftMarkers = {QString{}, QStringLiteral("<|"), QStringLiteral("*"),
                                     QStringLiteral("o"), QStringLiteral("<"), QStringLiteral("()")};
    const QStringList rightMarkers = {QString{}, QStringLiteral("|>"), QStringLiteral("*"),
                                      QStringLiteral("o"), QStringLiteral(">"), QStringLiteral("()")};
    if (!leftMarkers.contains(left) || !rightMarkers.contains(right)) return false;
    parts = {left, line, right};
    return true;
  }

  static void setRelationType(QJsonValue& target, const QString& marker) {
    if (marker == QLatin1String("<|") || marker == QLatin1String("|>")) target = 1;
    else if (marker == QLatin1String("*")) target = 2;
    else if (marker == QLatin1String("o")) target = 0;
    else if (marker == QLatin1String("<") || marker == QLatin1String(">")) target = 3;
    else if (marker == QLatin1String("()")) target = 4;
  }

  bool parseRelation(ClassTokenCursor line, qsizetype offset) {
    ClassTokenCursor cursor = line;
    const ParsedName sourceName = parseName(cursor);
    if (sourceName.id.isEmpty()) return false;
    QString sourceTitle = QStringLiteral("none");
    if (!cursor.atEnd() && cursor.peek().kind == ClassTokenKind::String)
      sourceTitle = cursor.consume().text;

    const qsizetype relationStart = cursor.position();
    for (qsizetype relationEnd = relationStart + 1;
         relationEnd <= std::min(relationStart + 3, cursor.endPosition()); ++relationEnd) {
      QString relationText;
      for (qsizetype index = relationStart; index < relationEnd; ++index)
        relationText += tokens_[index].text;
      RelationParts parts;
      if (!splitRelation(relationText, parts)) continue;

      ClassTokenCursor candidate(tokens_, relationEnd, cursor.endPosition());
      QString targetTitle = QStringLiteral("none");
      if (!candidate.atEnd() && candidate.peek().kind == ClassTokenKind::String)
        targetTitle = candidate.consume().text;
      const ParsedName targetName = parseName(candidate);
      if (targetName.id.isEmpty()) continue;
      QString title;
      if (candidate.match(ClassTokenKind::Colon)) title = candidate.raw(source_);
      if (!candidate.atEnd() && title.isEmpty()) continue;

      if (data_.relations.size() >= limits_.maxRelations)
        raise(source_, offset, ClassErrorStage::Resource, ClassErrorCode::LimitExceeded,
              QStringLiteral("relationStatement"), line.raw(source_), {},
              QStringLiteral("Maximum relation count exceeded"));

      ClassRelation relation;
      relation.id1 = sourceName.id;
      relation.id2 = targetName.id;
      relation.relationTitle1 = sourceTitle;
      relation.relationTitle2 = targetTitle;
      relation.title = title;
      relation.lineType = parts.line == QLatin1String("..") ? 1 : 0;
      setRelationType(relation.type1, parts.left);
      setRelationType(relation.type2, parts.right);

      const bool leftLollipop = relation.type1.isDouble() && relation.type1.toInt() == 4;
      const bool rightLollipop = relation.type2.isDouble() && relation.type2.toInt() == 4;
      if (leftLollipop && relation.type2.isString()) {
        addClass(relation.id2, targetName.type, targetName.offset);
        const QString interfaceId = QStringLiteral("interface%1").arg(interfaceCount_++);
        data_.interfaces.append({interfaceId, relation.id1, relation.id2});
        relation.id1 = interfaceId;
      } else if (rightLollipop && relation.type1.isString()) {
        addClass(relation.id1, sourceName.type, sourceName.offset);
        const QString interfaceId = QStringLiteral("interface%1").arg(interfaceCount_++);
        data_.interfaces.append({interfaceId, relation.id2, relation.id1});
        relation.id2 = interfaceId;
      } else {
        addClass(relation.id1, sourceName.type, sourceName.offset);
        addClass(relation.id2, targetName.type, targetName.offset);
      }
      data_.relations.append(std::move(relation));
      return true;
    }

    QString relationText;
    for (qsizetype index = relationStart; index < cursor.endPosition(); ++index)
      relationText += tokens_[index].text;
    RelationParts incomplete;
    if (splitRelation(relationText, incomplete))
      raise(source_, source_.isEmpty() ? 0 : line.raw(source_).size() + offset,
            ClassErrorStage::Parser, ClassErrorCode::MissingRelationTarget,
            QStringLiteral("relationStatement"), QStringLiteral("NEWLINE"),
            {QStringLiteral("className")});
    return false;
  }

  void parseNote(ClassTokenCursor cursor, qsizetype offset) {
    QString className;
    if (cursor.matchWord(u"for")) {
      const ParsedName name = parseName(cursor);
      if (name.id.isEmpty())
        unexpected(cursor, offset, QStringLiteral("noteStatement"), {QStringLiteral("className")});
      className = name.id;
    }
    if (cursor.atEnd())
      unexpected(cursor, offset, QStringLiteral("noteStatement"), {QStringLiteral("noteText")});
    if (cursor.peek().kind == ClassTokenKind::String ||
        cursor.peek().kind == ClassTokenKind::Backtick) {
      const QString text = cursor.consume().text;
      if (!cursor.atEnd()) unexpected(cursor, offset, QStringLiteral("noteStatement"));
      addNote(text, className, offset);
    } else {
      addNote(cursor.raw(source_), className, offset);
    }
  }

  void parseStyle(ClassTokenCursor cursor, qsizetype offset) {
    const ParsedName name = parseName(cursor);
    if (name.id.isEmpty())
      unexpected(cursor, offset, QStringLiteral("styleStatement"), {QStringLiteral("ALPHA")});
    ClassNode* node = findClass(name.id);
    const QStringList styles = cursor.parseList(source_);
    if (node) node->styles.append(styles);
  }

  void parseClassDef(ClassTokenCursor cursor, qsizetype offset) {
    QStringList names;
    while (!cursor.atEnd()) {
      if (cursor.peek().kind != ClassTokenKind::Word)
        unexpected(cursor, offset, QStringLiteral("classList"), {QStringLiteral("ALPHA")});
      const ClassToken name = cursor.consume();
      names.append(name.text);
      if (!cursor.match(ClassTokenKind::Comma)) break;
    }
    if (names.isEmpty() || cursor.atEnd())
      unexpected(cursor, offset, QStringLiteral("classDefStatement"),
                 {QStringLiteral("stylesOpt")});
    const QStringList styles = cursor.parseList(source_);
    for (const QString& name : names) {
      styleClasses_.insert(name, styles);
      for (ClassNode& node : data_.classes)
        if (node.cssClasses.contains(name)) node.styles.append(styles);
    }
  }

  void parseCssClass(ClassTokenCursor cursor, qsizetype offset) {
    if (cursor.atEnd() || cursor.peek().kind != ClassTokenKind::String)
      unexpected(cursor, offset, QStringLiteral("cssClassStatement"), {QStringLiteral("STR")});
    const QString ids = cursor.consume().text;
    if (cursor.atEnd())
      unexpected(cursor, offset, QStringLiteral("cssClassStatement"), {QStringLiteral("ALPHA")});
    const QString css = cursor.consume().text;
    if (!cursor.atEnd()) unexpected(cursor, offset, QStringLiteral("cssClassStatement"));
    for (const QString& id : ids.split(QLatin1Char(',')))
      if (ClassNode* node = findClass(id.trimmed()))
        node->cssClasses += QLatin1Char(' ') + css;
  }

  void parseInteraction(ClassTokenCursor cursor, qsizetype offset) {
    const QString command = cursor.consume().text;
    const ParsedName name = parseName(cursor);
    if (name.id.isEmpty())
      unexpected(cursor, offset, QStringLiteral("clickStatement"), {QStringLiteral("className")});
    ClassNode* node = findClass(name.id);
    if (!node) return;

    if (command == QLatin1String("link") ||
        (command == QLatin1String("click") && cursor.matchWord(u"href"))) {
      if (cursor.atEnd() || cursor.peek().kind != ClassTokenKind::String)
        unexpected(cursor, offset, QStringLiteral("clickStatement"), {QStringLiteral("STR")});
      node->link = cursor.consume().text;
      if (!cursor.atEnd() && cursor.peek().kind == ClassTokenKind::String)
        node->tooltip = cursor.consume().text;
      node->linkTarget = !cursor.atEnd() && cursor.peek().text.startsWith(QLatin1Char('_'))
          ? cursor.consume().text : QStringLiteral("_blank");
      if (!cursor.atEnd()) unexpected(cursor, offset, QStringLiteral("clickStatement"));
    } else {
      node->haveCallback = true;
      if (command == QLatin1String("callback")) {
        if (cursor.atEnd() || cursor.peek().kind != ClassTokenKind::String)
          unexpected(cursor, offset, QStringLiteral("clickStatement"), {QStringLiteral("STR")});
        cursor.consume();
        if (!cursor.atEnd() && cursor.peek().kind == ClassTokenKind::String)
          node->tooltip = cursor.consume().text;
        if (!cursor.atEnd()) unexpected(cursor, offset, QStringLiteral("clickStatement"));
      } else {
        if (cursor.matchWord(u"call") && cursor.atEnd())
          unexpected(cursor, offset, QStringLiteral("clickStatement"),
                     {QStringLiteral("CALLBACK_NAME")});
        if (!cursor.atEnd() && tokens_[cursor.endPosition() - 1].kind == ClassTokenKind::String)
          node->tooltip = tokens_[cursor.endPosition() - 1].text;
        while (!cursor.atEnd()) cursor.consume();
      }
    }
    node->cssClasses += QStringLiteral(" clickable");
  }

  QString source_;
  ClassLimits limits_;
  QVector<ClassToken> tokens_;
  ClassDiagramData data_;
  QVector<Scope> scopes_;
  QStringList namespaceStack_;
  QMap<QString, QStringList> styleClasses_;
  int interfaceCount_ = 0;
  int memberCount_ = 0;
};

QJsonObject memberJson(const ClassMember& member) {
  return {{QStringLiteral("id"), member.id}, {QStringLiteral("memberType"), member.memberType},
          {QStringLiteral("visibility"), member.visibility}, {QStringLiteral("classifier"), member.classifier},
          {QStringLiteral("parameters"), member.parameters}, {QStringLiteral("returnType"), member.returnType},
          {QStringLiteral("text"), member.text}};
}

}  // namespace

ClassParseError::ClassParseError(ClassDiagnostic diagnostic)
    : std::runtime_error(formatClassDiagnostic(diagnostic).toUtf8().constData()),
      diagnostic_(std::move(diagnostic)) {}

ClassDiagram ClassDiagram::parse(const QString& source, ClassLimits limits) {
  ClassDiagram diagram;
  diagram.data_ = Parser(source, limits).parse();
  return diagram;
}

QJsonObject ClassDiagram::toJson() const {
  QJsonArray classes;
  for (const ClassNode& node : data_.classes) {
    QJsonArray methods, members;
    for (const ClassMember& value : node.methods) methods.append(memberJson(value));
    for (const ClassMember& value : node.members) members.append(memberJson(value));
    classes.append(QJsonObject{{QStringLiteral("id"), node.id}, {QStringLiteral("type"), node.type},
        {QStringLiteral("label"), node.label}, {QStringLiteral("text"), node.text},
        {QStringLiteral("cssClasses"), node.cssClasses}, {QStringLiteral("methods"), methods},
        {QStringLiteral("members"), members}, {QStringLiteral("annotations"), QJsonArray::fromStringList(node.annotations)},
        {QStringLiteral("styles"), QJsonArray::fromStringList(node.styles)},
        {QStringLiteral("parent"), node.parent.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(node.parent)},
        {QStringLiteral("link"), node.link.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(node.link)},
        {QStringLiteral("linkTarget"), node.linkTarget.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(node.linkTarget)},
        {QStringLiteral("haveCallback"), node.haveCallback},
        {QStringLiteral("tooltip"), node.tooltip.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(node.tooltip)}});
  }
  QJsonArray relations;
  for (const ClassRelation& relation : data_.relations)
    relations.append(QJsonObject{{QStringLiteral("id1"), relation.id1}, {QStringLiteral("id2"), relation.id2},
        {QStringLiteral("relationTitle1"), relation.relationTitle1},
        {QStringLiteral("relationTitle2"), relation.relationTitle2}, {QStringLiteral("title"), relation.title},
        {QStringLiteral("relation"), QJsonObject{{QStringLiteral("type1"), relation.type1},
            {QStringLiteral("type2"), relation.type2}, {QStringLiteral("lineType"), relation.lineType}}}});
  QJsonArray notes;
  for (const ClassNote& note : data_.notes)
    notes.append(QJsonObject{{QStringLiteral("id"), note.id},
        {QStringLiteral("class"), note.className.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(note.className)},
        {QStringLiteral("text"), note.text}, {QStringLiteral("index"), note.index},
        {QStringLiteral("parent"), note.parent.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(note.parent)}});
  QJsonArray namespaces;
  for (const ClassNamespace& value : data_.namespaces)
    namespaces.append(QJsonObject{{QStringLiteral("id"), value.id}, {QStringLiteral("label"), value.label},
        {QStringLiteral("parent"), value.parent.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value.parent)},
        {QStringLiteral("explicit"), value.explicitDeclaration},
        {QStringLiteral("classKeys"), QJsonArray::fromStringList(value.classKeys)},
        {QStringLiteral("noteKeys"), QJsonArray::fromStringList(value.noteKeys)},
        {QStringLiteral("childKeys"), QJsonArray::fromStringList(value.childKeys)}});
  return {{QStringLiteral("title"), data_.title}, {QStringLiteral("accTitle"), data_.accTitle},
          {QStringLiteral("accDescription"), data_.accDescription}, {QStringLiteral("direction"), data_.direction},
          {QStringLiteral("classes"), classes}, {QStringLiteral("relations"), relations},
          {QStringLiteral("notes"), notes}, {QStringLiteral("namespaces"), namespaces}};
}

QString classErrorStageName(ClassErrorStage stage) {
  switch (stage) {
    case ClassErrorStage::Detector: return QStringLiteral("detector");
    case ClassErrorStage::Lexer: return QStringLiteral("lexer");
    case ClassErrorStage::Parser: return QStringLiteral("parser");
    case ClassErrorStage::Semantic: return QStringLiteral("semantic");
    case ClassErrorStage::Resource: return QStringLiteral("resource");
  }
  return {};
}

QString classErrorCodeName(ClassErrorCode code) {
  switch (code) {
    case ClassErrorCode::MissingHeader: return QStringLiteral("missing-header");
    case ClassErrorCode::UnexpectedToken: return QStringLiteral("unexpected-token");
    case ClassErrorCode::MissingClosingBrace: return QStringLiteral("missing-closing-brace");
    case ClassErrorCode::MissingRelationTarget: return QStringLiteral("missing-relation-target");
    case ClassErrorCode::InvalidClassLabel: return QStringLiteral("invalid-class-label");
    case ClassErrorCode::LimitExceeded: return QStringLiteral("limit-exceeded");
  }
  return {};
}

QString formatClassDiagnostic(const ClassDiagnostic& diagnostic) {
  return QStringLiteral("%1 at %2:%3 (%4)")
      .arg(diagnostic.detail.isEmpty() ? classErrorCodeName(diagnostic.code) : diagnostic.detail)
      .arg(diagnostic.span.line).arg(diagnostic.span.column).arg(classErrorStageName(diagnostic.stage));
}

}  // namespace muffin::mermaid::classdiagram

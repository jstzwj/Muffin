#pragma once

#include "mermaid/classdiagram/ClassTokenizer.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::classdiagram {

enum class ClassErrorStage { Detector, Lexer, Parser, Semantic, Resource };
enum class ClassErrorCode {
  MissingHeader,
  UnexpectedToken,
  MissingClosingBrace,
  MissingRelationTarget,
  InvalidClassLabel,
  LimitExceeded,
};

struct ClassSourceSpan {
  qsizetype offset = -1;
  qsizetype length = 0;
  int line = 1;
  int column = 0;
};

struct ClassDiagnostic {
  ClassErrorStage stage = ClassErrorStage::Parser;
  ClassErrorCode code = ClassErrorCode::UnexpectedToken;
  ClassSourceSpan span;
  QString production;
  QString actual;
  QStringList expected;
  QString detail;
};

class ClassParseError final : public std::runtime_error {
public:
  explicit ClassParseError(ClassDiagnostic diagnostic);
  const ClassDiagnostic& diagnostic() const noexcept { return diagnostic_; }

private:
  ClassDiagnostic diagnostic_;
};

struct ClassMember {
  QString id;
  QString memberType;
  QString visibility;
  QString classifier;
  QString parameters;
  QString returnType;
  QString text;
};

struct ClassNode {
  QString id;
  QString type;
  QString label;
  QString text;
  QString cssClasses = QStringLiteral("default");
  QVector<ClassMember> methods;
  QVector<ClassMember> members;
  QStringList annotations;
  QStringList styles;
  QString parent;
  QString link;
  QString linkTarget;
  bool haveCallback = false;
  QString tooltip;
};

struct ClassRelation {
  QString id1;
  QString id2;
  QString relationTitle1 = QStringLiteral("none");
  QString relationTitle2 = QStringLiteral("none");
  QString title;
  QJsonValue type1 = QStringLiteral("none");
  QJsonValue type2 = QStringLiteral("none");
  int lineType = 0;
  QStringList style;  // linkStyle declarations (key:value), applied at paint
};

struct ClassNote {
  QString id;
  QString className;
  QString text;
  int index = 0;
  QString parent;
};

struct ClassNamespace {
  QString id;
  QString label;
  QString parent;
  bool explicitDeclaration = true;
  QStringList classKeys;
  QStringList noteKeys;
  QStringList childKeys;
};

struct ClassInterface {
  QString id;
  QString label;
  QString classId;
};

struct ClassDiagramData {
  QString title;
  QString accTitle;
  QString accDescription;
  QString direction = QStringLiteral("TB");
  QVector<ClassNode> classes;
  QVector<ClassRelation> relations;
  QVector<ClassNote> notes;
  QVector<ClassNamespace> namespaces;
  QVector<ClassInterface> interfaces;
  // classDef table (id -> declarations), exposed so the runtime style cascade
  // (MermaidStyleResolve) can resolve edge classDef. Node classDef is also
  // folded into ClassNode.styles at parse time.
  QHash<QString, QStringList> classDefs;
};

struct ClassLimits {
  int maxClasses = 1000;
  int maxRelations = 5000;
  int maxNamespaces = 1000;
  int maxNotes = 1000;
  int maxMembers = 10000;
  int maxNamespaceDepth = 64;
  int maxTextSize = 100000;
};

class ClassDiagram {
public:
  static ClassDiagram parse(const QString& source, ClassLimits limits = {});
  const ClassDiagramData& data() const { return data_; }
  QJsonObject toJson() const;

private:
  ClassDiagramData data_;
};

QString classErrorStageName(ClassErrorStage stage);
QString classErrorCodeName(ClassErrorCode code);
QString formatClassDiagnostic(const ClassDiagnostic& diagnostic);

}  // namespace muffin::mermaid::classdiagram

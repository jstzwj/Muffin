#pragma once

#include <QString>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::eventmodeling {

enum class EventModelingErrorKind { Lexer, Parser };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(EventModelingErrorKind kind) {
  switch (kind) {
  case EventModelingErrorKind::Lexer: return u"lexer";
  case EventModelingErrorKind::Parser: return u"parse";
  }
  return u"parse";
}

class EventModelingParseError final : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  EventModelingParseError(EventModelingErrorKind kind, int line, int column, const QString& message);

  EventModelingErrorKind kind;
  int line = 1;
  int column = 1;
};

struct EventModelingFrame {
  bool reset = false;
  QString name;
  QString modelEntityType;
  QString entityIdentifier;
  QVector<QString> sourceFrames;
  QString dataReference;
  QString dataType;
  QString dataInlineValue;
};

struct EventModelingDataEntity {
  QString name;
  QString dataType;
  QString dataBlockValue;
};

struct EventModelingNote {
  QString sourceFrame;
  QString dataType;
  QString dataBlockValue;
};

struct EventModelingGwtStatement {
  QString modelEntityType;
  QString entityIdentifier;
};

struct EventModelingGwt {
  QString sourceFrame;
  QVector<EventModelingGwtStatement> given;
  QVector<EventModelingGwtStatement> when;
  QVector<EventModelingGwtStatement> then;
};

struct EventModelingData {
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<QString> modelEntities;
  QVector<EventModelingFrame> frames;
  QVector<EventModelingDataEntity> dataEntities;
  QVector<EventModelingNote> notes;
  QVector<EventModelingGwt> gwt;
};

class EventModelingDiagram {
public:
  static EventModelingData parse(const QString& source);
};

} // namespace muffin::mermaid::eventmodeling

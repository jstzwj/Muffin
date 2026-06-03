#pragma once

#include "math/MathSettings.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace muffin::math {

class MathMacroExpander {
public:
  struct MacroToken {
    QString text;
    qsizetype position = 0;
    qsizetype endPosition = 0;
    bool noExpand = false;
    bool treatAsRelax = false;
  };

  explicit MathMacroExpander(MathSettings settings = {});

  QString expand(QString input);
  void defineMacro(QString name, QString replacement);
  void defineMacro(QString name, QString replacement, int numArgs);

private:
  class TokenStream;

  struct Macro {
    QString replacement;
    int numArgs = 0;
    bool unexpandable = false;
  };

  void beginGroup();
  void endGroup();
  void endGroups();
  bool hasMacro(const QString& name) const;
  Macro macro(const QString& name) const;
  void setMacro(const QString& name, const Macro& macro, bool global = false);
  void undefineMacro(const QString& name, bool global = false);
  QString expandOnce(QString input, bool* changed);
  bool expandOnce(TokenStream& stream);
  MacroToken expandNextToken(TokenStream& stream);
  void countExpansion(int amount, qsizetype position, qsizetype endPosition);

  QHash<QString, Macro> macros_;
  QHash<QString, Macro> builtins_;
  QVector<QHash<QString, std::optional<Macro>>> undoStack_;
  MathSettings settings_;
  int expansionCount_ = 0;
};

}  // namespace muffin::math

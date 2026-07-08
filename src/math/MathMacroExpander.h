#pragma once

#include "math/MathSettings.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <memory>
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

  explicit MathMacroExpander(MathSettings settings = {}, std::shared_ptr<int> sharedExpansionCount = nullptr);

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
  // Shared across re-entrant \edef/\xdef sub-expanders so a fresh sub-expander can't reset the
  // maxExpand budget (DoS bypass). The outermost expander owns a fresh counter (make_shared<int>(0)).
  std::shared_ptr<int> expansionCount_;
  int expandDepth_ = 0;  // bounds \expandafter's C++ recursion in expandOnce
};

}  // namespace muffin::math

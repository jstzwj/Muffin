#include "math/MathFunctionRegistry.h"

#include <QSet>

#include <algorithm>

namespace muffin::math {
namespace {

using Arg = MathFunctionArgType;
using Builder = MathFunctionBuilderKind;
using Handler = MathFunctionHandlerKind;

MathFunctionSpec spec(QString typeName,
                      QVector<QString> names,
                      int numArgs,
                      Handler handlerKind,
                      Builder builderKind,
                      MathNodeType resultType,
                      QVector<Arg> argTypes = {},
                      int numOptionalArgs = 0,
                      bool allowedInArgument = false,
                      bool allowedInText = false,
                      bool allowedInMath = true,
                      bool infix = false,
                      bool primitive = false,
                      bool requiresTrust = false,
                      QString strictCategory = {},
                      int delimiterSize = 0,
                      MathNodeType delimiterNodeType = MathNodeType::Ord) {
  MathFunctionSpec function;
  function.typeName = std::move(typeName);
  function.names = std::move(names);
  function.numArgs = numArgs;
  function.numOptionalArgs = numOptionalArgs;
  function.argTypes = std::move(argTypes);
  function.allowedInArgument = allowedInArgument;
  function.allowedInText = allowedInText;
  function.allowedInMath = allowedInMath;
  function.infix = infix;
  function.primitive = primitive;
  function.requiresTrust = requiresTrust;
  function.strictCategory = std::move(strictCategory);
  function.handlerKind = handlerKind;
  function.builderKind = builderKind;
  function.resultType = resultType;
  function.delimiterSize = delimiterSize;
  function.delimiterNodeType = delimiterNodeType;
  return function;
}

QVector<QString> names(std::initializer_list<const char*> values) {
  QVector<QString> result;
  result.reserve(static_cast<int>(values.size()));
  for (const char* value : values) {
    result.push_back(QString::fromLatin1(value));
  }
  return result;
}

void add(QHash<QString, MathFunctionSpec>& functions, MathFunctionSpec function) {
  for (const QString& name : function.names) {
    functions.insert(name, function);
  }
}

QHash<QString, MathFunctionSpec> makeFunctions() {
  QHash<QString, MathFunctionSpec> functions;

  add(functions, spec(QStringLiteral("genfrac"),
                      names({"\\frac", "\\dfrac", "\\tfrac", "\\binom", "\\dbinom", "\\tbinom"}),
                      2,
                      Handler::Fraction,
                      Builder::Fraction,
                      MathNodeType::Fraction,
                      {Arg::Math, Arg::Math},
                      0,
                      true));
  add(functions, spec(QStringLiteral("genfrac"),
                      names({"\\genfrac"}),
                      6,
                      Handler::Fraction,
                      Builder::Fraction,
                      MathNodeType::Fraction,
                      {Arg::Primitive, Arg::Primitive, Arg::Size, Arg::Raw, Arg::Math, Arg::Math},
                      0,
                      true));
  add(functions, spec(QStringLiteral("infix"),
                      names({"\\over", "\\choose", "\\atop", "\\brace", "\\brack"}),
                      0,
                      Handler::Fraction,
                      Builder::Fraction,
                      MathNodeType::Fraction,
                      {},
                      0,
                      false,
                      false,
                      true,
                      true,
                      true));
  add(functions, spec(QStringLiteral("infix"),
                      names({"\\above"}),
                      1,
                      Handler::Fraction,
                      Builder::Fraction,
                      MathNodeType::Fraction,
                      {Arg::Size},
                      0,
                      false,
                      false,
                      true,
                      true,
                      true));
  add(functions, spec(QStringLiteral("sqrt"),
                      names({"\\sqrt"}),
                      1,
                      Handler::Sqrt,
                      Builder::Sqrt,
                      MathNodeType::Sqrt,
                      {Arg::Original},
                      1));
  add(functions, spec(QStringLiteral("accent"),
                      names({"\\acute", "\\grave", "\\ddot", "\\tilde", "\\bar", "\\breve", "\\check", "\\hat",
                             "\\vec", "\\dot", "\\mathring", "\\widehat", "\\widetilde", "\\widecheck",
                             "\\overrightarrow", "\\overleftarrow", "\\overleftrightarrow"}),
                      1,
                      Handler::Accent,
                      Builder::Accent,
                      MathNodeType::Accent,
                      {Arg::Original}));
  add(functions, spec(QStringLiteral("accent"),
                      names({"\\'", "\\`", "\\^", "\\~", "\\=", "\\u", "\\.", "\\\"", "\\c", "\\r", "\\H", "\\v",
                             "\\textcircled"}),
                      1,
                      Handler::Accent,
                      Builder::Accent,
                      MathNodeType::Accent,
                      {Arg::Primitive},
                      0,
                      false,
                      true,
                      true,
                      false,
                      false,
                      false,
                      QStringLiteral("mathVsTextAccents")));
  add(functions, spec(QStringLiteral("accentUnder"),
                      names({"\\underleftarrow", "\\underrightarrow", "\\underleftrightarrow", "\\undergroup",
                             "\\underlinesegment", "\\utilde"}),
                      1,
                      Handler::AccentUnder,
                      Builder::AccentUnder,
                      MathNodeType::AccentUnder,
                      {Arg::Original}));
  add(functions, spec(QStringLiteral("horizBrace"),
                      names({"\\overbrace", "\\underbrace", "\\overbracket", "\\underbracket"}),
                      1,
                      Handler::HorizBrace,
                      Builder::HorizBrace,
                      MathNodeType::HorizBrace,
                      {Arg::Original}));
  add(functions, spec(QStringLiteral("underline"),
                      names({"\\underline"}),
                      1,
                      Handler::Underline,
                      Builder::Underline,
                      MathNodeType::Underline,
                      {Arg::Original},
                      0,
                      false,
                      true));
  add(functions, spec(QStringLiteral("overline"),
                      names({"\\overline"}),
                      1,
                      Handler::Overline,
                      Builder::Overline,
                      MathNodeType::Overline,
                      {Arg::Original}));
  add(functions, spec(QStringLiteral("phantom"),
                      names({"\\phantom", "\\hphantom", "\\vphantom"}),
                      1,
                      Handler::Phantom,
                      Builder::Phantom,
                      MathNodeType::Phantom,
                      {Arg::Original},
                      0,
                      false,
                      true));
  add(functions, spec(QStringLiteral("smash"),
                      names({"\\smash"}),
                      1,
                      Handler::Smash,
                      Builder::Smash,
                      MathNodeType::Smash,
                      {Arg::Raw, Arg::Original},
                      1,
                      false,
                      true));
  add(functions, spec(QStringLiteral("rule"),
                      names({"\\rule"}),
                      2,
                      Handler::Rule,
                      Builder::Rule,
                      MathNodeType::Rule,
                      {Arg::Size, Arg::Size, Arg::Size},
                      1,
                      false,
                      true));
  add(functions, spec(QStringLiteral("kern"),
                      names({"\\kern", "\\mkern", "\\hskip", "\\mskip"}),
                      1,
                      Handler::Kern,
                      Builder::Kern,
                      MathNodeType::Kern,
                      {Arg::Size},
                      0,
                      false,
                      true,
                      true,
                      false,
                      true,
                      false,
                      QStringLiteral("mathVsTextUnits")));
  add(functions, spec(QStringLiteral("raisebox"),
                      names({"\\raisebox"}),
                      2,
                      Handler::RaiseBox,
                      Builder::RaiseBox,
                      MathNodeType::RaiseBox,
                      {Arg::Size, Arg::HBox},
                      0,
                      false,
                      true));
  add(functions, spec(QStringLiteral("vcenter"),
                      names({"\\vcenter"}),
                      1,
                      Handler::VCenter,
                      Builder::VCenter,
                      MathNodeType::VCenter,
                      {Arg::Original},
                      0,
                      false,
                      false));
  add(functions, spec(QStringLiteral("lap"),
                      names({"\\mathllap", "\\mathrlap", "\\mathclap"}),
                      1,
                      Handler::Lap,
                      Builder::Lap,
                      MathNodeType::Lap,
                      {Arg::Original},
                      0,
                      false,
                      true));
  add(functions, spec(QStringLiteral("enclose"),
                      names({"\\cancel", "\\bcancel", "\\xcancel", "\\sout", "\\fbox", "\\boxed", "\\colorbox", "\\fcolorbox",
                             "\\phase", "\\angl"}),
                      1,
                      Handler::Enclose,
                      Builder::Enclose,
                      MathNodeType::Enclose,
                      {Arg::Original},
                      0,
                      false,
                      true,
                      true,
                      false,
                      false,
                      false,
                      QStringLiteral("mathVsSout")));
  add(functions, spec(QStringLiteral("includegraphics"),
                      names({"\\includegraphics"}),
                      1,
                      Handler::IncludeGraphics,
                      Builder::IncludeGraphics,
                      MathNodeType::IncludeGraphics,
                      {Arg::Raw, Arg::Url},
                      1,
                      false,
                      false,
                      true,
                      false,
                      false,
                      true));
  add(functions, spec(QStringLiteral("mathchoice"),
                      names({"\\mathchoice"}),
                      4,
                      Handler::MathChoice,
                      Builder::MathChoice,
                      MathNodeType::MathChoice,
                      {Arg::Math, Arg::Math, Arg::Math, Arg::Math},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true));
  add(functions, spec(QStringLiteral("href"),
                      names({"\\href"}),
                      2,
                      Handler::Href,
                      Builder::Href,
                      MathNodeType::Href,
                      {Arg::Url, Arg::Original},
                      0,
                      false,
                      true,
                      true,
                      false,
                      false,
                      true));
  add(functions, spec(QStringLiteral("url"),
                      names({"\\url"}),
                      1,
                      Handler::Url,
                      Builder::Href,
                      MathNodeType::Href,
                      {Arg::Url},
                      0,
                      false,
                      true,
                      true,
                      false,
                      false,
                      true));
  add(functions, spec(QStringLiteral("html"),
                      names({"\\htmlClass", "\\htmlId", "\\htmlStyle", "\\htmlData"}),
                      2,
                      Handler::Html,
                      Builder::Html,
                      MathNodeType::Html,
                      {Arg::Raw, Arg::Original},
                      0,
                      false,
                      true,
                      true,
                      false,
                      false,
                      true,
                      QStringLiteral("htmlExtension")));
  add(functions, spec(QStringLiteral("tag"),
                      names({"\\tag", "\\tag*"}),
                      1,
                      Handler::Tag,
                      Builder::Tag,
                      MathNodeType::Tag,
                      {Arg::Original}));
  add(functions, spec(QStringLiteral("verb"),
                      names({"\\verb", "\\verb*"}),
                      1,
                      Handler::Verb,
                      Builder::Verb,
                      MathNodeType::Verb,
                      {},
                      0,
                      false,
                      true));
  add(functions, spec(QStringLiteral("styling"),
                      names({"\\displaystyle", "\\textstyle", "\\scriptstyle", "\\scriptscriptstyle"}),
                      0,
                      Handler::Styling,
                      Builder::Styling,
                      MathNodeType::Styling,
                      {},
                      0,
                      false,
                      true,
                      true,
                      false,
                      true));
  add(functions, spec(QStringLiteral("sizing"),
                      names({"\\tiny", "\\sixptsize", "\\scriptsize", "\\footnotesize", "\\small", "\\normalsize",
                             "\\large", "\\Large", "\\LARGE", "\\huge", "\\Huge"}),
                      0,
                      Handler::Sizing,
                      Builder::Sizing,
                      MathNodeType::Sizing,
                      {},
                      0,
                      false,
                      true));
  add(functions, spec(QStringLiteral("mclass"),
                      names({"\\mathord", "\\mathbin", "\\mathrel", "\\mathopen", "\\mathclose", "\\mathpunct", "\\mathinner"}),
                      1,
                      Handler::MathClass,
                      Builder::Text,
                      MathNodeType::Class,
                      {Arg::Math},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true));
  add(functions, spec(QStringLiteral("mclass"),
                      names({"\\@binrel"}),
                      2,
                      Handler::MathClass,
                      Builder::Text,
                      MathNodeType::Class,
                      {Arg::Math, Arg::Math},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true));
  add(functions, spec(QStringLiteral("supsub"),
                      names({"\\stackrel", "\\overset", "\\underset"}),
                      2,
                      Handler::Stack,
                      Builder::SupSub,
                      MathNodeType::SupSub,
                      {Arg::Math, Arg::Math}));
  add(functions, spec(QStringLiteral("text"),
                      names({"\\text", "\\textrm", "\\textnormal", "\\textbf", "\\textit", "\\textup", "\\mathrm", "\\mathbf",
                             "\\mathit", "\\mathnormal", "\\mathsf", "\\mathtt", "\\mathbb", "\\mathcal", "\\mathfrak",
                             "\\mathscr", "\\mathsfit", "\\Bbb", "\\bold", "\\frak", "\\hbox", "\\html@mathml", "\\@char"}),
                      1,
                      Handler::Text,
                      Builder::Text,
                      MathNodeType::Text,
                      {Arg::Text},
                      0,
                      true,
                      true));
  add(functions, spec(QStringLiteral("font"),
                      names({"\\rm", "\\sf", "\\tt", "\\bf", "\\it", "\\cal"}),
                      0,
                      Handler::Text,
                      Builder::Text,
                      MathNodeType::Text,
                      {},
                      0,
                      true,
                      true,
                      true,
                      false,
                      true));
  add(functions, spec(QStringLiteral("mclass"),
                      names({"\\boldsymbol", "\\bm"}),
                      1,
                      Handler::MathClass,
                      Builder::Text,
                      MathNodeType::Class,
                      {Arg::Math}));
  add(functions, spec(QStringLiteral("color"),
                      names({"\\textcolor"}),
                      2,
                      Handler::Color,
                      Builder::Color,
                      MathNodeType::Color,
                      {Arg::Color, Arg::Original},
                      0,
                      false,
                      true));
  add(functions, spec(QStringLiteral("color"),
                      names({"\\color"}),
                      1,
                      Handler::Color,
                      Builder::Color,
                      MathNodeType::Color,
                      {Arg::Color},
                      0,
                      false,
                      true));
  add(functions, spec(QStringLiteral("delimsizing"),
                      names({"\\bigl", "\\bigr", "\\bigm", "\\big"}),
                      1,
                      Handler::DelimSizing,
                      Builder::DelimSizing,
                      MathNodeType::DelimSizing,
                      {Arg::Primitive},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true,
                      false,
                      {},
                      1));
  add(functions, spec(QStringLiteral("delimsizing"),
                      names({"\\Bigl", "\\Bigr", "\\Bigm", "\\Big"}),
                      1,
                      Handler::DelimSizing,
                      Builder::DelimSizing,
                      MathNodeType::DelimSizing,
                      {Arg::Primitive},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true,
                      false,
                      {},
                      2));
  add(functions, spec(QStringLiteral("delimsizing"),
                      names({"\\biggl", "\\biggr", "\\biggm", "\\bigg"}),
                      1,
                      Handler::DelimSizing,
                      Builder::DelimSizing,
                      MathNodeType::DelimSizing,
                      {Arg::Primitive},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true,
                      false,
                      {},
                      3));
  add(functions, spec(QStringLiteral("delimsizing"),
                      names({"\\Biggl", "\\Biggr", "\\Biggm", "\\Bigg"}),
                      1,
                      Handler::DelimSizing,
                      Builder::DelimSizing,
                      MathNodeType::DelimSizing,
                      {Arg::Primitive},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true,
                      false,
                      {},
                      4));
  add(functions, spec(QStringLiteral("op"),
                      names({"\\mathop"}),
                      1,
                      Handler::Operator,
                      Builder::Operator,
                      MathNodeType::Operator,
                      {Arg::Math},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true));
  add(functions, spec(QStringLiteral("op"),
                      names({"\\operatorname", "\\operatornamewithlimits"}),
                      1,
                      Handler::OperatorName,
                      Builder::Operator,
                      MathNodeType::Operator,
                      {Arg::Text}));
  add(functions, spec(QStringLiteral("environment"),
                      names({"\\begin"}),
                      1,
                      Handler::BeginEnvironment,
                      Builder::Array,
                      MathNodeType::Array,
                      {Arg::Text},
                      0,
                      false,
                      true,
                      true,
                      false,
                      true));
  add(functions, spec(QStringLiteral("leftright"),
                      names({"\\left"}),
                      1,
                      Handler::LeftRight,
                      Builder::LeftRight,
                      MathNodeType::LeftRight,
                      {Arg::Primitive},
                      0,
                      false,
                      false,
                      true,
                      false,
                      true));

  static const QHash<QString, MathNodeType> delimiterTypes{
      {QStringLiteral("\\bigl"), MathNodeType::Open},   {QStringLiteral("\\Bigl"), MathNodeType::Open},
      {QStringLiteral("\\biggl"), MathNodeType::Open},  {QStringLiteral("\\Biggl"), MathNodeType::Open},
      {QStringLiteral("\\bigr"), MathNodeType::Close},  {QStringLiteral("\\Bigr"), MathNodeType::Close},
      {QStringLiteral("\\biggr"), MathNodeType::Close}, {QStringLiteral("\\Biggr"), MathNodeType::Close},
      {QStringLiteral("\\bigm"), MathNodeType::Relation}, {QStringLiteral("\\Bigm"), MathNodeType::Relation},
      {QStringLiteral("\\biggm"), MathNodeType::Relation}, {QStringLiteral("\\Biggm"), MathNodeType::Relation}};
  for (auto it = functions.begin(); it != functions.end(); ++it) {
    if (it.value().handlerKind == Handler::DelimSizing) {
      MathFunctionSpec updated = it.value();
      updated.delimiterNodeType = delimiterTypes.value(it.key(), MathNodeType::Ord);
      functions.insert(it.key(), updated);
    }
  }

  return functions;
}

}  // namespace

const MathFunctionSpec* MathFunctionRegistry::lookup(const QString& name) {
  const QHash<QString, MathFunctionSpec>& map = functions();
  const auto it = map.constFind(name);
  return it == map.constEnd() ? nullptr : &it.value();
}

QVector<MathFunctionSpec> MathFunctionRegistry::specs() {
  const QHash<QString, MathFunctionSpec>& map = functions();
  QVector<MathFunctionSpec> unique;
  QSet<QString> seen;
  for (const MathFunctionSpec& function : map) {
    const QString key = function.typeName + QLatin1Char(':') + (function.names.isEmpty() ? QString{} : function.names.first());
    if (seen.contains(key)) {
      continue;
    }
    seen.insert(key);
    unique.push_back(function);
  }
  return unique;
}

QVector<QString> MathFunctionRegistry::names() {
  QVector<QString> keys = functions().keys().toVector();
  std::sort(keys.begin(), keys.end());
  return keys;
}

const QHash<QString, MathFunctionSpec>& MathFunctionRegistry::functions() {
  static const QHash<QString, MathFunctionSpec> registry = makeFunctions();
  return registry;
}

}  // namespace muffin::math

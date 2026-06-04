#include "math/MathSymbols.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace muffin::math {
namespace {

QHash<QString, MathSymbolInfo> buildSymbols() {
  QHash<QString, MathSymbolInfo> s;
  const auto add = [&s](const QString& token, const QString& replacement, MathNodeType type, const QString& fontClass = QStringLiteral("amsrm")) {
    s.insert(token, MathSymbolInfo{replacement, type, fontClass, true});
  };

  add(QStringLiteral("\\alpha"), QStringLiteral("\u03b1"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\beta"), QStringLiteral("\u03b2"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\gamma"), QStringLiteral("\u03b3"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\delta"), QStringLiteral("\u03b4"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\epsilon"), QStringLiteral("\u03f5"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\varepsilon"), QStringLiteral("\u03b5"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\theta"), QStringLiteral("\u03b8"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\lambda"), QStringLiteral("\u03bb"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\mu"), QStringLiteral("\u03bc"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\pi"), QStringLiteral("\u03c0"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\rho"), QStringLiteral("\u03c1"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\sigma"), QStringLiteral("\u03c3"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\tau"), QStringLiteral("\u03c4"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\phi"), QStringLiteral("\u03d5"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\varphi"), QStringLiteral("\u03c6"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\omega"), QStringLiteral("\u03c9"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\zeta"), QStringLiteral("\u03b6"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\eta"), QStringLiteral("\u03b7"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\iota"), QStringLiteral("\u03b9"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\kappa"), QStringLiteral("\u03ba"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\nu"), QStringLiteral("\u03bd"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\xi"), QStringLiteral("\u03be"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\omicron"), QStringLiteral("\u03bf"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\upsilon"), QStringLiteral("\u03c5"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\chi"), QStringLiteral("\u03c7"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\psi"), QStringLiteral("\u03c8"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\digamma"), QStringLiteral("\u03dd"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\vartheta"), QStringLiteral("\u03d1"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\varpi"), QStringLiteral("\u03d6"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\varrho"), QStringLiteral("\u03f1"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\varsigma"), QStringLiteral("\u03c2"), MathNodeType::Ord, QStringLiteral("mathnormal"));
  add(QStringLiteral("\\Gamma"), QStringLiteral("\u0393"), MathNodeType::Ord);
  add(QStringLiteral("\\Delta"), QStringLiteral("\u0394"), MathNodeType::Ord);
  add(QStringLiteral("\\Theta"), QStringLiteral("\u0398"), MathNodeType::Ord);
  add(QStringLiteral("\\Lambda"), QStringLiteral("\u039b"), MathNodeType::Ord);
  add(QStringLiteral("\\Pi"), QStringLiteral("\u03a0"), MathNodeType::Ord);
  add(QStringLiteral("\\Sigma"), QStringLiteral("\u03a3"), MathNodeType::Ord);
  add(QStringLiteral("\\Phi"), QStringLiteral("\u03a6"), MathNodeType::Ord);
  add(QStringLiteral("\\Omega"), QStringLiteral("\u03a9"), MathNodeType::Ord);
  add(QStringLiteral("\\Xi"), QStringLiteral("\u039e"), MathNodeType::Ord);
  add(QStringLiteral("\\Upsilon"), QStringLiteral("\u03a5"), MathNodeType::Ord);
  add(QStringLiteral("\\Psi"), QStringLiteral("\u03a8"), MathNodeType::Ord);

  add(QStringLiteral("\\times"), QStringLiteral("\u00d7"), MathNodeType::Binary);
  add(QStringLiteral("\\cdot"), QStringLiteral("\u22c5"), MathNodeType::Binary);
  add(QStringLiteral("\\ast"), QStringLiteral("*"), MathNodeType::Binary);
  add(QStringLiteral("\\star"), QStringLiteral("\u22c6"), MathNodeType::Binary);
  add(QStringLiteral("\\circ"), QStringLiteral("\u2218"), MathNodeType::Binary);
  add(QStringLiteral("\\bullet"), QStringLiteral("\u2219"), MathNodeType::Binary);
  add(QStringLiteral("\\cap"), QStringLiteral("\u2229"), MathNodeType::Binary);
  add(QStringLiteral("\\cup"), QStringLiteral("\u222a"), MathNodeType::Binary);
  add(QStringLiteral("\\wedge"), QStringLiteral("\u2227"), MathNodeType::Binary);
  add(QStringLiteral("\\vee"), QStringLiteral("\u2228"), MathNodeType::Binary);
  add(QStringLiteral("\\setminus"), QStringLiteral("\u2216"), MathNodeType::Binary);
  add(QStringLiteral("\\pm"), QStringLiteral("\u00b1"), MathNodeType::Binary);
  add(QStringLiteral("\\mp"), QStringLiteral("\u2213"), MathNodeType::Binary);
  add(QStringLiteral("\\div"), QStringLiteral("\u00f7"), MathNodeType::Binary);
  add(QStringLiteral("\\oplus"), QStringLiteral("\u2295"), MathNodeType::Binary);
  add(QStringLiteral("\\ominus"), QStringLiteral("\u2296"), MathNodeType::Binary);
  add(QStringLiteral("\\otimes"), QStringLiteral("\u2297"), MathNodeType::Binary);
  add(QStringLiteral("\\oslash"), QStringLiteral("\u2298"), MathNodeType::Binary);
  add(QStringLiteral("\\odot"), QStringLiteral("\u2299"), MathNodeType::Binary);
  add(QStringLiteral("\\dagger"), QStringLiteral("\u2020"), MathNodeType::Binary);
  add(QStringLiteral("\\ddagger"), QStringLiteral("\u2021"), MathNodeType::Binary);
  add(QStringLiteral("\\amalg"), QStringLiteral("\u2a3f"), MathNodeType::Binary);
  add(QStringLiteral("\\wr"), QStringLiteral("\u2240"), MathNodeType::Binary);
  add(QStringLiteral("\\sqcap"), QStringLiteral("\u2293"), MathNodeType::Binary);
  add(QStringLiteral("\\sqcup"), QStringLiteral("\u2294"), MathNodeType::Binary);
  add(QStringLiteral("\\triangleleft"), QStringLiteral("\u25c3"), MathNodeType::Binary);
  add(QStringLiteral("\\triangleright"), QStringLiteral("\u25b9"), MathNodeType::Binary);
  add(QStringLiteral("\\le"), QStringLiteral("\u2264"), MathNodeType::Relation);
  add(QStringLiteral("\\leq"), QStringLiteral("\u2264"), MathNodeType::Relation);
  add(QStringLiteral("\\ge"), QStringLiteral("\u2265"), MathNodeType::Relation);
  add(QStringLiteral("\\geq"), QStringLiteral("\u2265"), MathNodeType::Relation);
  add(QStringLiteral("\\ne"), QStringLiteral("\u2260"), MathNodeType::Relation, QStringLiteral("main"));
  add(QStringLiteral("\\neq"), QStringLiteral("\u2260"), MathNodeType::Relation, QStringLiteral("main"));
  add(QStringLiteral("\\equiv"), QStringLiteral("\u2261"), MathNodeType::Relation);
  add(QStringLiteral("\\in"), QStringLiteral("\u2208"), MathNodeType::Relation);
  add(QStringLiteral("\\notin"), QStringLiteral("\u2209"), MathNodeType::Relation);
  add(QStringLiteral("\\subset"), QStringLiteral("\u2282"), MathNodeType::Relation);
  add(QStringLiteral("\\subseteq"), QStringLiteral("\u2286"), MathNodeType::Relation);
  add(QStringLiteral("\\supset"), QStringLiteral("\u2283"), MathNodeType::Relation);
  add(QStringLiteral("\\supseteq"), QStringLiteral("\u2287"), MathNodeType::Relation);
  add(QStringLiteral("\\sqsubset"), QStringLiteral("\u228f"), MathNodeType::Relation);
  add(QStringLiteral("\\sqsupset"), QStringLiteral("\u2290"), MathNodeType::Relation);
  add(QStringLiteral("\\sqsubseteq"), QStringLiteral("\u2291"), MathNodeType::Relation);
  add(QStringLiteral("\\sqsupseteq"), QStringLiteral("\u2292"), MathNodeType::Relation);
  add(QStringLiteral("\\approx"), QStringLiteral("\u2248"), MathNodeType::Relation);
  add(QStringLiteral("\\sim"), QStringLiteral("\u223c"), MathNodeType::Relation);
  add(QStringLiteral("\\simeq"), QStringLiteral("\u2243"), MathNodeType::Relation);
  add(QStringLiteral("\\cong"), QStringLiteral("\u2245"), MathNodeType::Relation);
  add(QStringLiteral("\\@not"), QString(QChar(0xe020)), MathNodeType::Relation, QStringLiteral("main"));
  add(QStringLiteral("\\propto"), QStringLiteral("\u221d"), MathNodeType::Relation);
  add(QStringLiteral("\\models"), QStringLiteral("\u22a8"), MathNodeType::Relation);
  add(QStringLiteral("\\perp"), QStringLiteral("\u22a5"), MathNodeType::Relation);
  add(QStringLiteral("\\mid"), QStringLiteral("\u2223"), MathNodeType::Relation);
  add(QStringLiteral("\\parallel"), QStringLiteral("\u2225"), MathNodeType::Relation);
  add(QStringLiteral("\\bowtie"), QStringLiteral("\u22c8"), MathNodeType::Relation);
  add(QStringLiteral("\\prec"), QStringLiteral("\u227a"), MathNodeType::Relation);
  add(QStringLiteral("\\succ"), QStringLiteral("\u227b"), MathNodeType::Relation);
  add(QStringLiteral("\\preceq"), QStringLiteral("\u2aaf"), MathNodeType::Relation);
  add(QStringLiteral("\\succeq"), QStringLiteral("\u2ab0"), MathNodeType::Relation);
  add(QStringLiteral("\\to"), QStringLiteral("\u2192"), MathNodeType::Relation);
  add(QStringLiteral("\\rightarrow"), QStringLiteral("\u2192"), MathNodeType::Relation);
  add(QStringLiteral("\\leftarrow"), QStringLiteral("\u2190"), MathNodeType::Relation);
  add(QStringLiteral("\\leftrightarrow"), QStringLiteral("\u2194"), MathNodeType::Relation);
  add(QStringLiteral("\\Rightarrow"), QStringLiteral("\u21d2"), MathNodeType::Relation);
  add(QStringLiteral("\\Leftarrow"), QStringLiteral("\u21d0"), MathNodeType::Relation);
  add(QStringLiteral("\\Leftrightarrow"), QStringLiteral("\u21d4"), MathNodeType::Relation);
  add(QStringLiteral("\\mapsto"), QStringLiteral("\u21a6"), MathNodeType::Relation);
  add(QStringLiteral("\\longrightarrow"), QStringLiteral("\u27f6"), MathNodeType::Relation);
  add(QStringLiteral("\\longleftarrow"), QStringLiteral("\u27f5"), MathNodeType::Relation);
  add(QStringLiteral("\\longleftrightarrow"), QStringLiteral("\u27f7"), MathNodeType::Relation);
  add(QStringLiteral("\\Longrightarrow"), QStringLiteral("\u27f9"), MathNodeType::Relation);
  add(QStringLiteral("\\Longleftarrow"), QStringLiteral("\u27f8"), MathNodeType::Relation);
  add(QStringLiteral("\\Longleftrightarrow"), QStringLiteral("\u27fa"), MathNodeType::Relation);
  add(QStringLiteral("\\hookleftarrow"), QStringLiteral("\u21a9"), MathNodeType::Relation);
  add(QStringLiteral("\\hookrightarrow"), QStringLiteral("\u21aa"), MathNodeType::Relation);
  add(QStringLiteral("\\uparrow"), QStringLiteral("\u2191"), MathNodeType::Relation);
  add(QStringLiteral("\\downarrow"), QStringLiteral("\u2193"), MathNodeType::Relation);
  add(QStringLiteral("\\updownarrow"), QStringLiteral("\u2195"), MathNodeType::Relation);
  add(QStringLiteral("\\Uparrow"), QStringLiteral("\u21d1"), MathNodeType::Relation);
  add(QStringLiteral("\\Downarrow"), QStringLiteral("\u21d3"), MathNodeType::Relation);
  add(QStringLiteral("\\Updownarrow"), QStringLiteral("\u21d5"), MathNodeType::Relation);
  add(QStringLiteral("\\infty"), QStringLiteral("\u221e"), MathNodeType::Ord);
  add(QStringLiteral("\\partial"), QStringLiteral("\u2202"), MathNodeType::Ord);
  add(QStringLiteral("\\nabla"), QStringLiteral("\u2207"), MathNodeType::Ord);
  add(QStringLiteral("\\forall"), QStringLiteral("\u2200"), MathNodeType::Ord);
  add(QStringLiteral("\\exists"), QStringLiteral("\u2203"), MathNodeType::Ord);
  add(QStringLiteral("\\emptyset"), QStringLiteral("\u2205"), MathNodeType::Ord);
  add(QStringLiteral("\\varnothing"), QStringLiteral("\u2205"), MathNodeType::Ord);
  add(QStringLiteral("\\ell"), QStringLiteral("\u2113"), MathNodeType::Ord);
  add(QStringLiteral("\\hbar"), QStringLiteral("\u210f"), MathNodeType::Ord);
  add(QStringLiteral("\\aleph"), QStringLiteral("\u2135"), MathNodeType::Ord);
  add(QStringLiteral("\\beth"), QStringLiteral("\u2136"), MathNodeType::Ord);
  add(QStringLiteral("\\gimel"), QStringLiteral("\u2137"), MathNodeType::Ord);
  add(QStringLiteral("\\daleth"), QStringLiteral("\u2138"), MathNodeType::Ord);
  add(QStringLiteral("\\wp"), QStringLiteral("\u2118"), MathNodeType::Ord);
  add(QStringLiteral("\\Re"), QStringLiteral("\u211c"), MathNodeType::Ord);
  add(QStringLiteral("\\Im"), QStringLiteral("\u2111"), MathNodeType::Ord);
  add(QStringLiteral("\\angle"), QStringLiteral("\u2220"), MathNodeType::Ord);
  add(QStringLiteral("\\measuredangle"), QStringLiteral("\u2221"), MathNodeType::Ord);
  add(QStringLiteral("\\sphericalangle"), QStringLiteral("\u2222"), MathNodeType::Ord);
  add(QStringLiteral("\\top"), QStringLiteral("\u22a4"), MathNodeType::Ord);
  add(QStringLiteral("\\bot"), QStringLiteral("\u22a5"), MathNodeType::Ord);
  add(QStringLiteral("\\vdash"), QStringLiteral("\u22a2"), MathNodeType::Relation);
  add(QStringLiteral("\\dashv"), QStringLiteral("\u22a3"), MathNodeType::Relation);
  add(QStringLiteral("\\flat"), QStringLiteral("\u266d"), MathNodeType::Ord);
  add(QStringLiteral("\\natural"), QStringLiteral("\u266e"), MathNodeType::Ord);
  add(QStringLiteral("\\sharp"), QStringLiteral("\u266f"), MathNodeType::Ord);
  add(QStringLiteral("\\clubsuit"), QStringLiteral("\u2663"), MathNodeType::Ord);
  add(QStringLiteral("\\diamondsuit"), QStringLiteral("\u2662"), MathNodeType::Ord);
  add(QStringLiteral("\\heartsuit"), QStringLiteral("\u2661"), MathNodeType::Ord);
  add(QStringLiteral("\\spadesuit"), QStringLiteral("\u2660"), MathNodeType::Ord);
  add(QStringLiteral("\\ldots"), QStringLiteral("\u2026"), MathNodeType::Inner, QStringLiteral("main"));

  add(QStringLiteral("\\sum"), QStringLiteral("\u2211"), MathNodeType::Operator);
  add(QStringLiteral("\\prod"), QStringLiteral("\u220f"), MathNodeType::Operator);
  add(QStringLiteral("\\coprod"), QStringLiteral("\u2210"), MathNodeType::Operator);
  add(QStringLiteral("\\bigcup"), QStringLiteral("\u22c3"), MathNodeType::Operator);
  add(QStringLiteral("\\bigcap"), QStringLiteral("\u22c2"), MathNodeType::Operator);
  add(QStringLiteral("\\bigvee"), QStringLiteral("\u22c1"), MathNodeType::Operator);
  add(QStringLiteral("\\bigwedge"), QStringLiteral("\u22c0"), MathNodeType::Operator);
  add(QStringLiteral("\\bigoplus"), QStringLiteral("\u2a01"), MathNodeType::Operator);
  add(QStringLiteral("\\bigotimes"), QStringLiteral("\u2a02"), MathNodeType::Operator);
  add(QStringLiteral("\\bigodot"), QStringLiteral("\u2a00"), MathNodeType::Operator);
  add(QStringLiteral("\\int"), QStringLiteral("\u222b"), MathNodeType::Operator);
  add(QStringLiteral("\\iint"), QStringLiteral("\u222c"), MathNodeType::Operator);
  add(QStringLiteral("\\iiint"), QStringLiteral("\u222d"), MathNodeType::Operator);
  add(QStringLiteral("\\oint"), QStringLiteral("\u222e"), MathNodeType::Operator);
  add(QStringLiteral("\\smallint"), QStringLiteral("\u222b"), MathNodeType::Operator);
  add(QStringLiteral("\\lim"), QStringLiteral("lim"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\arcsin"), QStringLiteral("arcsin"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\arccos"), QStringLiteral("arccos"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\arctan"), QStringLiteral("arctan"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\arctg"), QStringLiteral("arctg"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\arcctg"), QStringLiteral("arcctg"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\arg"), QStringLiteral("arg"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\ch"), QStringLiteral("ch"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\sin"), QStringLiteral("sin"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\cos"), QStringLiteral("cos"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\tan"), QStringLiteral("tan"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\cot"), QStringLiteral("cot"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\cotg"), QStringLiteral("cotg"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\coth"), QStringLiteral("coth"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\cosec"), QStringLiteral("cosec"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\sec"), QStringLiteral("sec"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\csc"), QStringLiteral("csc"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\ctg"), QStringLiteral("ctg"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\cth"), QStringLiteral("cth"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\deg"), QStringLiteral("deg"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\exp"), QStringLiteral("exp"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\lg"), QStringLiteral("lg"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\sinh"), QStringLiteral("sinh"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\sh"), QStringLiteral("sh"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\cosh"), QStringLiteral("cosh"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\tanh"), QStringLiteral("tanh"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\tg"), QStringLiteral("tg"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\th"), QStringLiteral("th"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\max"), QStringLiteral("max"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\min"), QStringLiteral("min"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\sup"), QStringLiteral("sup"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\inf"), QStringLiteral("inf"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\det"), QStringLiteral("det"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\dim"), QStringLiteral("dim"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\gcd"), QStringLiteral("gcd"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\hom"), QStringLiteral("hom"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\ker"), QStringLiteral("ker"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\Pr"), QStringLiteral("Pr"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\log"), QStringLiteral("log"), MathNodeType::Operator, QStringLiteral("main"));
  add(QStringLiteral("\\ln"), QStringLiteral("ln"), MathNodeType::Operator, QStringLiteral("main"));

  add(QStringLiteral("\\langle"), QStringLiteral("\u27e8"), MathNodeType::Open);
  add(QStringLiteral("\\rangle"), QStringLiteral("\u27e9"), MathNodeType::Close);
  add(QStringLiteral("\\lbrace"), QStringLiteral("{"), MathNodeType::Open);
  add(QStringLiteral("\\rbrace"), QStringLiteral("}"), MathNodeType::Close);
  add(QStringLiteral("\\{"), QStringLiteral("{"), MathNodeType::Open);
  add(QStringLiteral("\\}"), QStringLiteral("}"), MathNodeType::Close);
  add(QStringLiteral("\\lvert"), QStringLiteral("|"), MathNodeType::Open);
  add(QStringLiteral("\\rvert"), QStringLiteral("|"), MathNodeType::Close);
  add(QStringLiteral("\\vert"), QStringLiteral("|"), MathNodeType::Ord);
  add(QStringLiteral("\\lVert"), QStringLiteral("\u2016"), MathNodeType::Open);
  add(QStringLiteral("\\rVert"), QStringLiteral("\u2016"), MathNodeType::Close);
  add(QStringLiteral("\\Vert"), QStringLiteral("\u2016"), MathNodeType::Ord);
  add(QStringLiteral("\\|"), QStringLiteral("\u2016"), MathNodeType::Ord);
  add(QStringLiteral("\\lfloor"), QStringLiteral("\u230a"), MathNodeType::Open);
  add(QStringLiteral("\\rfloor"), QStringLiteral("\u230b"), MathNodeType::Close);
  add(QStringLiteral("\\lceil"), QStringLiteral("\u2308"), MathNodeType::Open);
  add(QStringLiteral("\\rceil"), QStringLiteral("\u2309"), MathNodeType::Close);
  add(QStringLiteral("\\backslash"), QStringLiteral("\\"), MathNodeType::Ord);

  add(QStringLiteral("\\,"), QStringLiteral(" "), MathNodeType::Spacing);
  add(QStringLiteral("\\;"), QStringLiteral(" "), MathNodeType::Spacing);
  add(QStringLiteral("\\:"), QStringLiteral(" "), MathNodeType::Spacing);
  add(QStringLiteral("\\!"), QStringLiteral(""), MathNodeType::Spacing);
  add(QStringLiteral("\\thinspace"), QStringLiteral(" "), MathNodeType::Spacing);
  add(QStringLiteral("\\medspace"), QStringLiteral(" "), MathNodeType::Spacing);
  add(QStringLiteral("\\thickspace"), QStringLiteral(" "), MathNodeType::Spacing);
  add(QStringLiteral("\\negthinspace"), QStringLiteral(""), MathNodeType::Spacing);
  add(QStringLiteral("\\negmedspace"), QStringLiteral(""), MathNodeType::Spacing);
  add(QStringLiteral("\\negthickspace"), QStringLiteral(""), MathNodeType::Spacing);
  add(QStringLiteral("\\quad"), QStringLiteral(" "), MathNodeType::Spacing);
  add(QStringLiteral("\\qquad"), QStringLiteral("  "), MathNodeType::Spacing);
  add(QStringLiteral("\\allowbreak"), QStringLiteral(""), MathNodeType::Spacing);
  return s;
}

QString unescapeSymbolString(QString value) {
  value = value.trimmed();
  if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) ||
      (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))) {
    value = value.mid(1, value.size() - 2);
  }
  QString out;
  for (qsizetype i = 0; i < value.size(); ++i) {
    const QChar ch = value.at(i);
    if (ch != QLatin1Char('\\') || i + 1 >= value.size()) {
      out += ch;
      continue;
    }
    const QChar next = value.at(++i);
    if (next == QLatin1Char('u') && i + 4 < value.size()) {
      bool ok = false;
      const uint code = value.mid(i + 1, 4).toUInt(&ok, 16);
      if (ok) {
        out += QChar(code);
        i += 4;
      }
    } else if (next == QLatin1Char('\\')) {
      out += QLatin1Char('\\');
    } else if (next == QLatin1Char('"')) {
      out += QLatin1Char('"');
    } else if (next == QLatin1Char('\'')) {
      out += QLatin1Char('\'');
    } else {
      out += QLatin1Char('\\');
      out += next;
    }
  }
  return out;
}

QString resolveSymbolConstant(const QString& value) {
  const QString trimmed = value.trimmed();
  static const QHash<QString, QString> constants{
      {QStringLiteral("math"), QStringLiteral("math")},       {QStringLiteral("text"), QStringLiteral("text")},
      {QStringLiteral("main"), QStringLiteral("main")},       {QStringLiteral("ams"), QStringLiteral("ams")},
      {QStringLiteral("accent"), QStringLiteral("accent-token")},
      {QStringLiteral("bin"), QStringLiteral("bin")},         {QStringLiteral("close"), QStringLiteral("close")},
      {QStringLiteral("inner"), QStringLiteral("inner")},     {QStringLiteral("mathord"), QStringLiteral("mathord")},
      {QStringLiteral("op"), QStringLiteral("op-token")},     {QStringLiteral("open"), QStringLiteral("open")},
      {QStringLiteral("punct"), QStringLiteral("punct")},     {QStringLiteral("rel"), QStringLiteral("rel")},
      {QStringLiteral("spacing"), QStringLiteral("spacing")}, {QStringLiteral("textord"), QStringLiteral("textord")}};
  if (constants.contains(trimmed)) {
    return constants.value(trimmed);
  }
  return unescapeSymbolString(trimmed);
}

MathNodeType nodeTypeForKatexGroup(const QString& group) {
  if (group == QStringLiteral("textord")) return MathNodeType::Text;
  if (group == QStringLiteral("bin")) return MathNodeType::Binary;
  if (group == QStringLiteral("rel")) return MathNodeType::Relation;
  if (group == QStringLiteral("open")) return MathNodeType::Open;
  if (group == QStringLiteral("close")) return MathNodeType::Close;
  if (group == QStringLiteral("punct")) return MathNodeType::Punct;
  if (group == QStringLiteral("inner")) return MathNodeType::Inner;
  if (group == QStringLiteral("spacing")) return MathNodeType::Spacing;
  if (group == QStringLiteral("op-token")) return MathNodeType::Operator;
  return MathNodeType::Ord;
}

QString fontClassForKatexFont(const QString& font) {
  if (font == QStringLiteral("ams")) {
    return QStringLiteral("amsrm");
  }
  if (font == QStringLiteral("main")) {
    return QStringLiteral("main");
  }
  return QStringLiteral("mathnormal");
}

QStringList splitDefineSymbolArgs(const QString& args) {
  QStringList result;
  QString current;
  bool inString = false;
  QChar quote;
  int depth = 0;
  for (qsizetype i = 0; i < args.size(); ++i) {
    const QChar ch = args.at(i);
    if (inString) {
      current += ch;
      if (ch == QLatin1Char('\\') && i + 1 < args.size()) {
        current += args.at(++i);
      } else if (ch == quote) {
        inString = false;
      }
      continue;
    }
    if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
      inString = true;
      quote = ch;
      current += ch;
    } else if (ch == QLatin1Char('(')) {
      ++depth;
      current += ch;
    } else if (ch == QLatin1Char(')')) {
      --depth;
      current += ch;
    } else if (ch == QLatin1Char(',') && depth == 0) {
      result.push_back(current.trimmed());
      current.clear();
    } else {
      current += ch;
    }
  }
  if (!current.trimmed().isEmpty()) {
    result.push_back(current.trimmed());
  }
  return result;
}

void addKatexSymbols(QHash<QString, MathSymbolInfo>& symbols) {
  QFile file(QStringLiteral(":/katex/src/symbols.ts"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  const QString source = QString::fromUtf8(file.readAll());
  QRegularExpression callRe(QStringLiteral("defineSymbol\\(([^;]+)\\);"), QRegularExpression::DotMatchesEverythingOption);
  QRegularExpressionMatchIterator it = callRe.globalMatch(source);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    const QStringList args = splitDefineSymbolArgs(match.captured(1));
    if (args.size() < 5) {
      continue;
    }
    const QString mode = resolveSymbolConstant(args.at(0));
    if (mode != QStringLiteral("math")) {
      continue;
    }
    const QString font = resolveSymbolConstant(args.at(1));
    const QString group = resolveSymbolConstant(args.at(2));
    const QString replacement = unescapeSymbolString(args.at(3));
    const QString name = unescapeSymbolString(args.at(4));
    if (args.at(3).trimmed() == QStringLiteral("ch") || args.at(3).trimmed() == QStringLiteral("wideChar") ||
        args.at(4).trimmed() == QStringLiteral("ch") || args.at(4).trimmed() == QStringLiteral("wideChar")) {
      continue;
    }
    const bool acceptUnicode = args.size() >= 6 && args.at(5).trimmed() == QStringLiteral("true");
    const MathSymbolInfo info{replacement, nodeTypeForKatexGroup(group), fontClassForKatexFont(font), true};
    symbols.insert(name, info);
    if (acceptUnicode && !replacement.isEmpty()) {
      symbols.insert(replacement, info);
    }
  }
}

const QHash<QString, MathSymbolInfo>& symbols() {
  static const QHash<QString, MathSymbolInfo> table = [] {
    QHash<QString, MathSymbolInfo> result = buildSymbols();
    addKatexSymbols(result);
    return result;
  }();
  return table;
}

}  // namespace

MathSymbolInfo lookupSymbol(const QString& token) {
  if (symbols().contains(token)) {
    return symbols().value(token);
  }
  if (token.size() == 1) {
    const QChar c = token.at(0);
    if (QStringLiteral("+-*/=").contains(c)) {
      MathNodeType type = c == QLatin1Char('=') ? MathNodeType::Relation : MathNodeType::Binary;
      return {token, type, QStringLiteral("main"), true};
    }
    if (QStringLiteral("()[]{}<>|").contains(c)) {
      const bool open = c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{') || c == QLatin1Char('<');
      if (c == QLatin1Char('|')) {
        return {token, MathNodeType::Ord, QStringLiteral("main"), true};
      }
      return {token, open ? MathNodeType::Open : MathNodeType::Close, QStringLiteral("main"), true};
    }
    if (c.isDigit()) {
      return {token, MathNodeType::Ord, QStringLiteral("main"), true};
    }
    if (c.isLetter()) {
      return {token, MathNodeType::Ord, QStringLiteral("mathnormal"), true};
    }
    return {token, MathNodeType::Ord, QStringLiteral("main"), true};
  }
  return {token, MathNodeType::Error, QStringLiteral("main"), false};
}

}  // namespace muffin::math

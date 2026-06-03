#include "math/MathStyle.h"

#include <array>

namespace muffin::math {
namespace {

constexpr std::array<MathStyle, 8> styles{
    MathStyle(MathStyle::Display, 0, false),
    MathStyle(MathStyle::DisplayCramped, 0, true),
    MathStyle(MathStyle::Text, 1, false),
    MathStyle(MathStyle::TextCramped, 1, true),
    MathStyle(MathStyle::Script, 2, false),
    MathStyle(MathStyle::ScriptCramped, 2, true),
    MathStyle(MathStyle::ScriptScript, 3, false),
    MathStyle(MathStyle::ScriptScriptCramped, 3, true),
};

constexpr std::array<int, 8> supMap{4, 5, 4, 5, 6, 7, 6, 7};
constexpr std::array<int, 8> subMap{5, 5, 5, 5, 7, 7, 7, 7};
constexpr std::array<int, 8> fracNumMap{2, 3, 4, 5, 6, 7, 6, 7};
constexpr std::array<int, 8> fracDenMap{3, 3, 5, 5, 7, 7, 7, 7};
constexpr std::array<int, 8> crampMap{1, 1, 3, 3, 5, 5, 7, 7};
constexpr std::array<int, 8> textMap{0, 1, 2, 3, 2, 3, 2, 3};

MathStyle mapped(MathStyle::Id id, const std::array<int, 8>& map) {
  return styles.at(static_cast<size_t>(map.at(static_cast<size_t>(id))));
}

}  // namespace

MathStyle::Id MathStyle::id() const {
  return id_;
}

int MathStyle::size() const {
  return size_;
}

bool MathStyle::cramped() const {
  return cramped_;
}

MathStyle MathStyle::sup() const {
  return mapped(id_, supMap);
}

MathStyle MathStyle::sub() const {
  return mapped(id_, subMap);
}

MathStyle MathStyle::fracNum() const {
  return mapped(id_, fracNumMap);
}

MathStyle MathStyle::fracDen() const {
  return mapped(id_, fracDenMap);
}

MathStyle MathStyle::cramp() const {
  return mapped(id_, crampMap);
}

MathStyle MathStyle::text() const {
  return mapped(id_, textMap);
}

bool MathStyle::isTight() const {
  return size_ >= 2;
}

MathStyle MathStyle::display() {
  return styles.at(Display);
}

MathStyle MathStyle::textStyle() {
  return styles.at(Text);
}

MathStyle MathStyle::script() {
  return styles.at(Script);
}

MathStyle MathStyle::scriptScript() {
  return styles.at(ScriptScript);
}

}  // namespace muffin::math

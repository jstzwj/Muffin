#pragma once

namespace muffin::math {

class MathStyle {
public:
  enum Id {
    Display = 0,
    DisplayCramped = 1,
    Text = 2,
    TextCramped = 3,
    Script = 4,
    ScriptCramped = 5,
    ScriptScript = 6,
    ScriptScriptCramped = 7
  };

  constexpr MathStyle(Id id, int size, bool cramped) : id_(id), size_(size), cramped_(cramped) {}

  Id id() const;
  int size() const;
  bool cramped() const;

  MathStyle sup() const;
  MathStyle sub() const;
  MathStyle fracNum() const;
  MathStyle fracDen() const;
  MathStyle cramp() const;
  MathStyle text() const;
  bool isTight() const;

  static MathStyle display();
  static MathStyle textStyle();
  static MathStyle script();
  static MathStyle scriptScript();

private:
  Id id_;
  int size_ = 0;
  bool cramped_ = false;
};

}  // namespace muffin::math

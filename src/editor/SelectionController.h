#pragma once

#include "editor/CursorPosition.h"
#include "Export.h"

#include <QObject>

namespace muffin {

class MUFFIN_UI_EXPORT SelectionController final : public QObject {
  Q_OBJECT

public:
  explicit SelectionController(QObject* parent = nullptr);

  bool hasCursor() const;
  SelectionRange selection() const;
  CursorPosition cursorPosition() const;
  HitTestResult currentHit() const;

  void setHitResult(HitTestResult hit);
  void setCursorPosition(CursorPosition position);
  void setSelection(SelectionRange selection);
  void setSelection(SelectionRange selection, HitTestResult focusHit);
  void clear();

signals:
  void selectionChanged(SelectionRange selection, HitTestResult hit);

private:
  SelectionRange selection_;
  HitTestResult currentHit_;
  bool hasCursor_ = false;
};

}  // namespace muffin

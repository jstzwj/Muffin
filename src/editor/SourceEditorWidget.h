#pragma once

#include <QWidget>

class QPlainTextEdit;
class QResizeEvent;

namespace muffin {

class RenderTheme;


class SourceEditorWidget final : public QWidget {
  Q_OBJECT

public:
  explicit SourceEditorWidget(QWidget* parent = nullptr);

  QString text() const;
  void setText(const QString& text);
  void setWordWrapEnabled(bool enabled);
  void setZoomPercent(int percent);
  void setTheme(const RenderTheme& theme);

  QPlainTextEdit* editor();
  const QPlainTextEdit* editor() const;

protected:
  void resizeEvent(QResizeEvent* event) override;

signals:
  void textEdited(QString text);
  void cursorPositionChanged(int line, int column);

private:
  void setupStyle();
  void updateEditorWidth();
  void emitCursorPosition();

  QPlainTextEdit* editor_ = nullptr;
};

}  // namespace muffin

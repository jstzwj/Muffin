#pragma once

#include <QWidget>

class QPlainTextEdit;
class QEvent;
class QResizeEvent;

namespace muffin {

class RenderTheme;

class MarkdownSourceEdit;

class SourceEditorWidget final : public QWidget {
  Q_OBJECT

public:
  explicit SourceEditorWidget(QWidget* parent = nullptr);

  QString text() const;
  void setText(const QString& text);
  void setWordWrapEnabled(bool enabled);
  void setZoomPercent(int percent);
  void setFontSizePx(int px);
  void setTheme(const RenderTheme& theme);

  QPlainTextEdit* editor();
  const QPlainTextEdit* editor() const;

protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

signals:
  void textEdited(QString text);
  void cursorPositionChanged(int line, int column);

private:
  void setupStyle();
  void retranslateUi();
  void updateEditorWidth();
  void emitCursorPosition();
  void applyFontSize();

  MarkdownSourceEdit* editor_ = nullptr;
  int zoomPercent_ = 100;
  int fontSizePx_ = 16;
};

}  // namespace muffin

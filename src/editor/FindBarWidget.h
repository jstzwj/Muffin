#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;

namespace muffin {

class FindBarWidget final : public QWidget {
  Q_OBJECT

public:
  explicit FindBarWidget(QWidget* parent = nullptr);

  void setSearchText(const QString& text);
  QString searchText() const;
  void setReplaceVisible(bool visible);
  void activateFind();
  void activateReplace();
  void setResultInfo(int current, int total);

signals:
  void findRequested(QString text, bool forward);
  void findNextRequested();
  void findPreviousRequested();
  void replaceRequested(QString findText, QString replaceText);
  void replaceAllRequested(QString findText, QString replaceText);
  void closed();

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  void setupUi();
  QLineEdit* findEdit_ = nullptr;
  QLineEdit* replaceEdit_ = nullptr;
  QPushButton* nextButton_ = nullptr;
  QPushButton* prevButton_ = nullptr;
  QPushButton* replaceButton_ = nullptr;
  QPushButton* replaceAllButton_ = nullptr;
  QLabel* resultLabel_ = nullptr;
  QWidget* replaceRow_ = nullptr;
};

}  // namespace muffin

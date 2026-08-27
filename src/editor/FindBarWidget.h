#pragma once

#include <QWidget>

#include "Export.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;

namespace muffin {

class MUFFIN_UI_EXPORT FindBarWidget final : public QWidget {
  Q_OBJECT

public:
  explicit FindBarWidget(QWidget* parent = nullptr);

  void setSearchText(const QString& text);
  QString searchText() const;
  bool regularExpressionEnabled() const;
  bool caseSensitiveEnabled() const;
  void setReplaceVisible(bool visible);
  void activateFind();
  void activateReplace();
  void setResultInfo(int current, int total);
  void setErrorText(const QString& error);

signals:
  void findRequested(QString text, bool forward, bool regularExpression, bool caseSensitive);
  void findNextRequested();
  void findPreviousRequested();
  void replaceRequested(QString findText, QString replaceText, bool regularExpression, bool caseSensitive);
  void replaceAllRequested(QString findText, QString replaceText, bool regularExpression, bool caseSensitive);
  void closed();

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  void setupUi();
  void retranslateUi();

  QLineEdit* findEdit_ = nullptr;
  QLineEdit* replaceEdit_ = nullptr;
  QPushButton* prevButton_ = nullptr;
  QPushButton* nextButton_ = nullptr;
  QPushButton* closeButton_ = nullptr;
  QPushButton* replaceButton_ = nullptr;
  QPushButton* replaceAllButton_ = nullptr;
  QLabel* resultLabel_ = nullptr;
  QCheckBox* regexCheck_ = nullptr;
  QCheckBox* caseCheck_ = nullptr;
  QWidget* replaceRow_ = nullptr;
};

}  // namespace muffin

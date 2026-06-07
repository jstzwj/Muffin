#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace muffin {

class PreferencesPage : public QWidget {
  Q_OBJECT

public:
  explicit PreferencesPage(QWidget* parent = nullptr);

  virtual void retranslateUi() = 0;

protected:
  QLabel* makeSectionLabel(QWidget* parent) const;
  QLabel* makeMutedLabel(QWidget* parent) const;
  QPushButton* makeButton(QWidget* parent) const;
  QLabel* makeInfoIcon(QWidget* parent) const;
};

}  // namespace muffin

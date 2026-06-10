#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace muffin {

class PreferencesPage : public QWidget {
  Q_OBJECT

public:
  explicit PreferencesPage(QWidget* parent = nullptr);

  virtual void retranslateUi() = 0;

protected:
  static constexpr int kPageLeftMargin = 30;
  static constexpr int kPageTopMargin = 28;
  static constexpr int kPageRightMargin = 38;
  static constexpr int kPageBottomMargin = 28;
  static constexpr int kContentWidth = 680;
  static constexpr int kCardSpacing = 12;

  QLabel* makeSectionLabel(QWidget* parent) const;
  QLabel* makeMutedLabel(QWidget* parent) const;
  QPushButton* makeButton(QWidget* parent) const;
  QLabel* makeInfoIcon(QWidget* parent) const;
  QWidget* makeCard(QWidget* parent) const;
  QVBoxLayout* makeCardLayout(QWidget* card) const;
  void wireBoolSetting(QCheckBox* checkBox, const QString& key) const;
  void wireComboIndexSetting(QComboBox* comboBox, const QString& key) const;
  void wireLineEditSetting(QLineEdit* lineEdit, const QString& key) const;
  void loadCheck(QCheckBox* checkBox, const QString& key, bool defaultValue) const;
  void loadComboIndex(QComboBox* comboBox, const QString& key, int defaultValue) const;
  void loadLineEdit(QLineEdit* lineEdit, const QString& key, const QString& defaultValue = QString()) const;
  void rebuildCombo(QComboBox* comboBox, const QVector<QString>& items) const;
  void polishComboBox(QComboBox* comboBox, int visibleItems = 8) const;
};

}  // namespace muffin

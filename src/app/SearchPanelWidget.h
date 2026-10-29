#pragma once
#include "app/WorkspaceSearch.h"
#include <QWidget>
class QLineEdit;
class QCheckBox;
class QLabel;
class QTreeWidget;
class QPushButton;
class QToolButton;
class QTimer;
namespace muffin {
struct ThemeDefinition;
class MUFFIN_UI_EXPORT SearchPanelWidget final : public QWidget {
  Q_OBJECT
public:
  explicit SearchPanelWidget(QWidget* parent = nullptr);
  ~SearchPanelWidget() override;
  void setRoot(const QString& root);
  // Follow the file tree only while there is no active search query.
  void setDefaultRoot(const QString& root);
  QString root() const { return root_; }
  void activate(bool replace);
  void startSearch(const OpenSearchBuffers& buffers);
  void retranslateUi();
  void setThemeDefinition(const ThemeDefinition& definition);
  QWidget* focusWidget() const;
  const WorkspaceSearchResult& result() const { return result_; }
  QString replacement() const;
  void invalidate();
signals:
  void searchRequested();
  void resultActivated(QString path, QByteArray fingerprint, qsizetype offset, qsizetype length);
  void replaceRequested();

private:
  void scheduleSearch();
  void addFile(const WorkspaceSearchFile& file);
  void updateSummary();
  void showDetails();
  void saveState() const;
  void cancelSearch();
  void changeEvent(QEvent* event) override;
  QString root_;
  QLineEdit *query_, *replacement_;
  QCheckBox *case_, *regex_;
  QLabel* summary_;
  QTreeWidget* results_;
  QToolButton* replaceToggle_;
  QWidget* replaceRow_;
  QPushButton *search_, *cancelButton_, *preview_;
  QTimer* timer_;
  SearchCancellation cancel_;
  WorkspaceSearchResult result_;
  bool running_ = false;
};
} // namespace muffin

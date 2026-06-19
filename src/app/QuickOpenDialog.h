#pragma once

// Modal file picker backing File → Quick Open (Ctrl+P). Shows a filter box over
// a list of candidate absolute paths (recent files plus, when a file is open,
// the current file's sibling Markdown files — see MainWindow::quickOpenCandidates),
// narrows the list as you type, and returns the selected path. Filtering is
// case-insensitive substring on both the file name and the full path; name hits
// are ranked first.
//
// NOTE: the .cpp contains tr() calls and follows the lupdate rule in CLAUDE.md
// (fully-qualified muffin::QuickOpenDialog::method() defs + anonymous-namespace
// helpers, no namespace muffin {} wrapper) so translations land under the
// muffin::QuickOpenDialog context.

#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;
class QListWidget;

namespace muffin {

class QuickOpenDialog : public QDialog {
  Q_OBJECT

public:
  explicit QuickOpenDialog(QWidget* parent = nullptr);

  void setCandidates(QStringList paths);
  QString selectedPath() const;

protected:
  // Routes Enter/Esc/Up/Down typed in the filter box to accept/reject/navigate
  // the list, so the picker is fully keyboard-driven without losing typing focus.
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  void applyFilter(const QString& text);
  void addPathItem(const QString& path);
  void retranslateUi();

  QLineEdit* filterEdit_ = nullptr;
  QListWidget* list_ = nullptr;
  QStringList candidates_;
};

}  // namespace muffin

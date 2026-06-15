#pragma once

// A lightweight Markdown help viewer shared by the Help menu (Quick Start,
// Markdown Reference, Acknowledgements). Docs live as GFM .md files under the
// `:/help/` resource tree, one set per UI locale with an English fallback, and
// are rendered with Qt's native markdown engine — so styling follows the active
// light/dark palette automatically. A single instance is reused for the whole
// session (see HelpViewerDialog::open), which keeps back/forward history across
// the cross-document `help:<topic>` links.

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QVector>

class QTextBrowser;
class QToolButton;

namespace muffin {

enum class HelpTopic {
  QuickStart,
  MarkdownReference,
  Acknowledgements,
};

class HelpViewerDialog : public QDialog {
  Q_OBJECT

public:
  // Brings the shared viewer to front and navigates to `topic`, creating the
  // window lazily on first use. `parent` (the MainWindow) is used only to reparent.
  static void open(QWidget* parent, HelpTopic topic);

protected:
  void changeEvent(QEvent* event) override;

private:
  explicit HelpViewerDialog(QWidget* parent = nullptr);

  void loadTopic(HelpTopic topic);
  void goBack();
  void goForward();
  void goHome();
  void renderCurrent();
  void updateNavButtons();
  void retranslateUi();

  static QString topicKey(HelpTopic topic);
  static QString topicTitle(HelpTopic topic);
  static QString readTopicDoc(HelpTopic topic, const QString& localeCode);

  QTextBrowser* browser_ = nullptr;
  QToolButton* backButton_ = nullptr;
  QToolButton* forwardButton_ = nullptr;
  QToolButton* homeButton_ = nullptr;

  QVector<HelpTopic> history_;
  int historyIndex_ = -1;
  HelpTopic currentTopic_ = HelpTopic::QuickStart;
};

}  // namespace muffin

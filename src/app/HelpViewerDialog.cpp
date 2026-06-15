// NOTE: This file contains tr() calls and therefore MUST NOT contain any
// `namespace muffin { }` block — lupdate loses the namespace prefix otherwise
// and generates context `HelpViewerDialog` instead of `muffin::HelpViewerDialog`,
// so translations never match at runtime (see CLAUDE.md). All HelpViewerDialog
// methods use fully-qualified `muffin::` names; the tr()-free helper below lives
// in an anonymous namespace at file scope (which the rule permits).

#include "app/HelpViewerDialog.h"

#include "app/LanguageManager.h"

#include <QDesktopServices>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QPointer>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// Maps an internal cross-document link key (`help:<key>`) back to a topic.
muffin::HelpTopic topicFromKey(const QString& key) {
  if (key == QLatin1String("markdown-reference")) {
    return muffin::HelpTopic::MarkdownReference;
  }
  if (key == QLatin1String("acknowledgements")) {
    return muffin::HelpTopic::Acknowledgements;
  }
  return muffin::HelpTopic::QuickStart;
}
}  // namespace

void muffin::HelpViewerDialog::open(QWidget* parent, HelpTopic topic) {
  static QPointer<HelpViewerDialog> instance;
  if (!instance) {
    QWidget* host = parent ? parent->window() : nullptr;
    instance = new HelpViewerDialog(host);
    // Closing the viewer must not terminate the app.
    instance->setAttribute(Qt::WA_QuitOnClose, false);
  }
  instance->loadTopic(topic);
  instance->show();
  instance->raise();
  instance->activateWindow();
}

muffin::HelpViewerDialog::HelpViewerDialog(QWidget* parent) : QDialog(parent) {
  setMinimumSize(560, 420);
  resize(840, 640);

  browser_ = new QTextBrowser(this);
  browser_->setReadOnly(true);
  browser_->setOpenLinks(false);  // we route clicks ourselves via anchorClicked
  browser_->document()->setDocumentMargin(20);
  // Internal `help:<topic>` links navigate within the viewer; everything else
  // (http etc.) opens in the OS browser.
  connect(browser_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
    if (url.scheme() == QLatin1String("help")) {
      QString key = url.host().isEmpty() ? url.path() : url.host();
      if (key.startsWith(QLatin1Char('/'))) {
        key.remove(0, 1);
      }
      loadTopic(topicFromKey(key));
      return;
    }
    QDesktopServices::openUrl(url);
  });

  backButton_ = new QToolButton(this);
  forwardButton_ = new QToolButton(this);
  homeButton_ = new QToolButton(this);
  backButton_->setAutoRaise(true);
  forwardButton_->setAutoRaise(true);
  homeButton_->setAutoRaise(true);
  backButton_->setArrowType(Qt::LeftArrow);
  forwardButton_->setArrowType(Qt::RightArrow);
  homeButton_->setArrowType(Qt::UpArrow);
  connect(backButton_, &QToolButton::clicked, this, &HelpViewerDialog::goBack);
  connect(forwardButton_, &QToolButton::clicked, this, &HelpViewerDialog::goForward);
  connect(homeButton_, &QToolButton::clicked, this, &HelpViewerDialog::goHome);

  auto* nav = new QHBoxLayout;
  nav->setContentsMargins(8, 6, 8, 6);
  nav->setSpacing(4);
  nav->addWidget(backButton_);
  nav->addWidget(forwardButton_);
  nav->addWidget(homeButton_);
  nav->addStretch();

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addLayout(nav);
  root->addWidget(browser_);

  retranslateUi();
  loadTopic(HelpTopic::QuickStart);
}

void muffin::HelpViewerDialog::loadTopic(HelpTopic topic) {
  currentTopic_ = topic;
  if (history_.isEmpty() || history_.constLast() != topic) {
    history_.resize(historyIndex_ + 1);
    history_.append(topic);
    historyIndex_ = history_.size() - 1;
  }
  renderCurrent();
  updateNavButtons();
}

void muffin::HelpViewerDialog::goBack() {
  if (historyIndex_ <= 0) {
    return;
  }
  --historyIndex_;
  currentTopic_ = history_.at(historyIndex_);
  renderCurrent();
  updateNavButtons();
}

void muffin::HelpViewerDialog::goForward() {
  if (historyIndex_ + 1 >= history_.size()) {
    return;
  }
  ++historyIndex_;
  currentTopic_ = history_.at(historyIndex_);
  renderCurrent();
  updateNavButtons();
}

void muffin::HelpViewerDialog::goHome() {
  browser_->moveCursor(QTextCursor::Start);
  if (auto* sb = browser_->verticalScrollBar()) {
    sb->setValue(0);
  }
}

void muffin::HelpViewerDialog::renderCurrent() {
  const QString locale = LanguageManager::instance().currentLanguageCode();
  const QString markdown = readTopicDoc(currentTopic_, locale);
  browser_->document()->setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
  browser_->moveCursor(QTextCursor::Start);
  if (auto* sb = browser_->verticalScrollBar()) {
    sb->setValue(0);
  }
  setWindowTitle(tr("%1 — Muffin Help").arg(topicTitle(currentTopic_)));
}

void muffin::HelpViewerDialog::updateNavButtons() {
  backButton_->setEnabled(historyIndex_ > 0);
  forwardButton_->setEnabled(historyIndex_ + 1 < history_.size());
}

void muffin::HelpViewerDialog::retranslateUi() {
  backButton_->setToolTip(tr("Go back"));
  forwardButton_->setToolTip(tr("Go forward"));
  homeButton_->setToolTip(tr("Scroll to top"));
  setWindowTitle(tr("%1 — Muffin Help").arg(topicTitle(currentTopic_)));
}

void muffin::HelpViewerDialog::changeEvent(QEvent* event) {
  QDialog::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    // Locale changed: re-render so the matching localized doc is shown.
    retranslateUi();
    renderCurrent();
  }
}

QString muffin::HelpViewerDialog::topicKey(HelpTopic topic) {
  switch (topic) {
    case HelpTopic::QuickStart:
      return QStringLiteral("quick-start");
    case HelpTopic::MarkdownReference:
      return QStringLiteral("markdown-reference");
    case HelpTopic::Acknowledgements:
      return QStringLiteral("acknowledgements");
  }
  return QStringLiteral("quick-start");
}

QString muffin::HelpViewerDialog::topicTitle(HelpTopic topic) {
  switch (topic) {
    case HelpTopic::QuickStart:
      return tr("Quick Start");
    case HelpTopic::MarkdownReference:
      return tr("Markdown Reference");
    case HelpTopic::Acknowledgements:
      return tr("Acknowledgements");
  }
  return {};
}

QString muffin::HelpViewerDialog::readTopicDoc(HelpTopic topic, const QString& localeCode) {
  const QString key = topicKey(topic);
  if (!localeCode.isEmpty() && localeCode != QLatin1String("en")) {
    QFile localized(QStringLiteral(":/help/%1/%2.md").arg(localeCode, key));
    if (localized.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return QString::fromUtf8(localized.readAll());
    }
  }
  QFile fallback(QStringLiteral(":/help/%1.md").arg(key));
  if (fallback.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString::fromUtf8(fallback.readAll());
  }
  return QStringLiteral("# %1\n\n%2").arg(topicTitle(topic), tr("This page is not available."));
}

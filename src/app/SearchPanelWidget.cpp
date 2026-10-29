#include "app/SearchPanelWidget.h"
#include <QDialog>
#include <QDialogButtonBox>
#include "app/FileOpenProtection.h"
#include "theme/ThemeDefinition.h"
#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <QPromise>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QTextLayout>
#include <QStyle>
#include <QSettings>
#include <QCryptographicHash>
#include <QSignalBlocker>

namespace {
class MatchDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    if (!index.parent().isValid()) {
      QStyledItemDelegate::paint(painter, option, index);
      return;
    }
    QString text = index.data().toString();
    const bool selected = option.state & QStyle::State_Selected;
    int highlight = index.data(Qt::UserRole + 2).toInt();
    const int prefix = index.data(Qt::UserRole + 4).toInt();
    const int width = qMax(1, option.rect.width() - 8);
    int start = prefix;
    // Keep the line number and match visible even in a narrow sidebar. The
    // source snippet can contain 70 characters before the match.
    while (start < highlight && option.fontMetrics.horizontalAdvance(text.mid(start, highlight - start)) > width / 3) {
      ++start;
      if (start < text.size() && text.at(start).isLowSurrogate())
        ++start;
    }
    if (start > prefix) {
      text = text.left(prefix) + QChar(0x2026) + text.mid(start);
      highlight -= start - prefix - 1;
    }
    text = option.fontMetrics.elidedText(text, Qt::ElideRight, width);
    QTextLayout layout(text, option.font);
    QTextCharFormat format;
    format.setBackground(option.palette.color(QPalette::Link));
    format.setForeground(option.palette.color(QPalette::BrightText));
    layout.setFormats(
        {{highlight, qMax(0, qMin(index.data(Qt::UserRole + 3).toInt(), int(text.size()) - highlight)), format}});
    layout.beginLayout();
    auto line = layout.createLine();
    if (line.isValid())
      line.setLineWidth(width);
    layout.endLayout();
    painter->save();
    painter->setClipRect(option.rect);
    if (selected)
      painter->fillRect(option.rect, option.palette.color(QPalette::Highlight));
    painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
    layout.draw(painter, QPointF(option.rect.left() + 4,
                                 option.rect.top() + (option.rect.height() - layout.boundingRect().height()) / 2));
    painter->restore();
  }
};
}

muffin::SearchPanelWidget::SearchPanelWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 12, 10, 8);
  query_ = new QLineEdit(this);
  query_->setObjectName("WorkspaceSearchQuery");
  layout->addWidget(query_);
  auto* options = new QHBoxLayout;
  case_ = new QCheckBox(this);
  regex_ = new QCheckBox(this);
  options->addWidget(case_);
  options->addWidget(regex_);
  options->addStretch();
  layout->addLayout(options);
  replaceToggle_ = new QToolButton(this);
  replaceToggle_->setCheckable(true);
  layout->addWidget(replaceToggle_);
  replaceRow_ = new QWidget(this);
  auto* replaceLayout = new QVBoxLayout(replaceRow_);
  replaceLayout->setContentsMargins(0, 0, 0, 0);
  replacement_ = new QLineEdit(this);
  replacement_->setObjectName("WorkspaceReplacement");
  preview_ = new QPushButton(this);
  preview_->setObjectName("WorkspaceReplacePreview");
  replaceLayout->addWidget(replacement_);
  replaceLayout->addWidget(preview_);
  layout->addWidget(replaceRow_);
  replaceRow_->hide();
  auto* actions = new QHBoxLayout;
  search_ = new QPushButton(this);
  search_->setObjectName("WorkspaceSearchRun");
  cancelButton_ = new QPushButton(this);
  actions->addWidget(search_);
  actions->addWidget(cancelButton_);
  layout->addLayout(actions);
  summary_ = new QLabel(this);
  summary_->setWordWrap(true);
  summary_->setObjectName("WorkspaceSearchSummary");
  layout->addWidget(summary_);
  summary_->setTextFormat(Qt::RichText);
  summary_->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
  connect(summary_, &QLabel::linkActivated, this, [this] { showDetails(); });
  results_ = new QTreeWidget(this);
  results_->setObjectName("WorkspaceSearchResults");
  results_->setHeaderHidden(true);
  results_->setItemDelegate(new MatchDelegate(results_));
  results_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  results_->setColumnCount(1);
  results_->setIndentation(12);
  layout->addWidget(results_, 1);
  timer_ = new QTimer(this);
  timer_->setSingleShot(true);
  timer_->setInterval(250);
  connect(timer_, &QTimer::timeout, this, &SearchPanelWidget::searchRequested);
  connect(query_, &QLineEdit::textChanged, this, &SearchPanelWidget::scheduleSearch);
  connect(case_, &QCheckBox::toggled, this, &SearchPanelWidget::scheduleSearch);
  connect(regex_, &QCheckBox::toggled, this, &SearchPanelWidget::scheduleSearch);
  connect(search_, &QPushButton::clicked, this, [this] {
    timer_->stop();
    emit searchRequested();
  });
  connect(cancelButton_, &QPushButton::clicked, this, &SearchPanelWidget::cancelSearch);
  connect(replaceToggle_, &QToolButton::toggled, replaceRow_, &QWidget::setVisible);
  connect(preview_, &QPushButton::clicked, this, &SearchPanelWidget::replaceRequested);
  connect(results_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
    if (!item->parent())
      return;
    const int fileIndex = item->data(0, Qt::UserRole).toInt();
    const int hitIndex = item->data(0, Qt::UserRole + 1).toInt();
    if (fileIndex < 0 || fileIndex >= result_.files.size())
      return;
    const auto& file = result_.files[fileIndex];
    if (hitIndex < 0 || hitIndex >= file.hits.size())
      return;
    const auto& hit = file.hits[hitIndex].match;
    emit resultActivated(file.path, file.fingerprint, hit.start, hit.length);
  });
  connect(results_, &QTreeWidget::itemActivated, this,
          [this](QTreeWidgetItem* item) { emit results_->itemClicked(item, 0); });
  retranslateUi();
}
muffin::SearchPanelWidget::~SearchPanelWidget() {
  saveState();
  if (cancel_)
    cancel_->store(true);
}
void muffin::SearchPanelWidget::saveState() const {
  if (root_.isEmpty())
    return;
  const QString key = QString::fromLatin1(
      QCryptographicHash::hash(WorkspaceFileIndex::pathKey(root_).toUtf8(), QCryptographicHash::Sha256).toHex());
  QSettings settings;
  settings.beginGroup("search/folders/" + key);
  settings.remove("include");
  settings.remove("exclude");
  settings.setValue("case", case_->isChecked());
  settings.setValue("regex", regex_->isChecked());
  // Search/replacement text and old result snapshots are intentionally transient.
}
void muffin::SearchPanelWidget::cancelSearch() {
  if (cancel_)
    cancel_->store(true);
  running_ = false;
  result_.complete = false;
  cancelButton_->setEnabled(false);
  preview_->setEnabled(false);
  updateSummary();
}
void muffin::SearchPanelWidget::invalidate() {
  timer_->stop();
  cancelSearch();
}
void muffin::SearchPanelWidget::scheduleSearch() {
  cancelSearch();
  results_->clear();
  result_ = {};
  updateSummary();
  if (!root_.isEmpty() && !query_->text().isEmpty())
    timer_->start();
}
void muffin::SearchPanelWidget::setRoot(const QString& root) {
  if (root_ == root)
    return;
  saveState();
  invalidate();
  root_ = root;
  results_->clear();
  result_ = {};
  retranslateUi();
  const QSignalBlocker b1(case_), b2(regex_);
  const QString key = QString::fromLatin1(
      QCryptographicHash::hash(WorkspaceFileIndex::pathKey(root_).toUtf8(), QCryptographicHash::Sha256).toHex());
  QSettings settings;
  settings.beginGroup("search/folders/" + key);
  case_->setChecked(settings.value("case", false).toBool());
  regex_->setChecked(settings.value("regex", false).toBool());
}
void muffin::SearchPanelWidget::setDefaultRoot(const QString& root) {
  if (query_->text().isEmpty() && !running_)
    setRoot(root);
}
void muffin::SearchPanelWidget::activate(bool replace) {
  replaceToggle_->setChecked(replace);
  query_->setFocus();
  query_->selectAll();
}
QWidget* muffin::SearchPanelWidget::focusWidget() const {
  return query_;
}
QString muffin::SearchPanelWidget::replacement() const {
  return replacement_->text();
}

void muffin::SearchPanelWidget::startSearch(const OpenSearchBuffers& buffers) {
  timer_->stop();
  if (cancel_)
    cancel_->store(true);
  cancel_ = std::make_shared<std::atomic_bool>(false);
  const auto cancellation = cancel_;
  WorkspaceQuery query;
  query.root = root_;
  query.pattern = query_->text();
  query.options = {regex_->isChecked(), case_->isChecked()};
  query.extensions = WorkspaceFileIndex::textExtensions();
  query.maxFileBytes = qMin(query.maxFileBytes, file_open_protection::sizeLimitBytes());
  results_->clear();
  result_ = {};
  result_.query = query;
  if (root_.isEmpty() || query.pattern.isEmpty()) {
    running_ = false;
    updateSummary();
    return;
  }
  running_ = true;
  preview_->setEnabled(false);
  cancelButton_->setEnabled(true);
  updateSummary();
  // Promise results stream complete files while the worker keeps searching.
  auto* watcher = new QFutureWatcher<WorkspaceSearchResult>(this);
  connect(watcher, &QFutureWatcher<WorkspaceSearchResult>::resultReadyAt, this,
          [this, watcher, cancellation](int index) {
            if (cancellation != cancel_ || cancellation->load())
              return;
            auto batch = watcher->resultAt(index);
            if (!batch.query.root.isEmpty())
              return;
            if (!batch.files.isEmpty()) {
              for (const auto& file : batch.files) {
                addFile(file);
                result_.files.append(file);
                result_.matches += file.hits.size();
              }
            }
            updateSummary();
          });
  connect(watcher, &QFutureWatcher<WorkspaceSearchResult>::finished, this, [this, watcher, cancellation] {
    watcher->deleteLater();
    if (cancellation != cancel_ || cancellation->load())
      return;
    // The last entry contains the authoritative completion/error state.
    const auto future = watcher->future();
    if (future.resultCount())
      result_ = future.resultAt(future.resultCount() - 1);
    running_ = false;
    cancelButton_->setEnabled(false);
    preview_->setEnabled(result_.complete && result_.error.isEmpty() && result_.matches > 0);
    updateSummary();
  });
  watcher->setFuture(QtConcurrent::run([query, buffers, cancellation](QPromise<WorkspaceSearchResult>& promise) {
    auto result = WorkspaceSearch::run(query, buffers, cancellation, [&](const WorkspaceSearchFile& file) {
      WorkspaceSearchResult batch;
      batch.complete = false;
      batch.files.append(file);
      promise.addResult(batch);
    });
    promise.addResult(result);
  }));
}
void muffin::SearchPanelWidget::addFile(const WorkspaceSearchFile& file) {
  const int fileIndex = result_.files.size();
  const QString parent = QDir(root_).relativeFilePath(QFileInfo(file.path).absolutePath());
  QString title = QFileInfo(file.path).fileName() + QStringLiteral(" (%1)").arg(file.hits.size()) +
                  (file.modified ? tr(" · Unsaved") : QString());
  if (parent != QLatin1String("."))
    title += '\n' + QDir::toNativeSeparators(parent);
  auto* group = new QTreeWidgetItem(results_, {title});
  group->setToolTip(0, file.path);
  for (qsizetype i = 0; i < file.hits.size(); ++i) {
    const auto& hit = file.hits[i];
    auto* item = new QTreeWidgetItem(group, {QStringLiteral("%1  %2").arg(hit.line).arg(hit.context)});
    item->setToolTip(0, hit.context);
    item->setData(0, Qt::UserRole, fileIndex);
    item->setData(0, Qt::UserRole + 1, i);
    item->setData(0, Qt::UserRole + 2, QString::number(hit.line).size() + 2 + hit.highlight);
    item->setData(0, Qt::UserRole + 3, qMin<qsizetype>(hit.match.length, hit.context.size() - hit.highlight));
    item->setData(0, Qt::UserRole + 4, QString::number(hit.line).size() + 2);
  }
  group->setExpanded(true);
}
void muffin::SearchPanelWidget::updateSummary() {
  QString text;
  if (root_.isEmpty())
    text = tr("Open a folder to search its contents.");
  else if (!result_.error.isEmpty())
    text = tr("Search failed: %1").arg(result_.error);
  else if (query_->text().isEmpty())
    text = tr("Search text files in this folder and its subfolders.");
  else
    text = tr("%1 matches in %2 files").arg(result_.matches).arg(result_.files.size());
  if (running_)
    text += tr(" · Searching…");
  else if (!result_.complete)
    text += tr(" · Incomplete results; search again before replacing");
  if (result_.searchedFiles)
    text += QStringLiteral(" · ") + tr("%1 files searched").arg(result_.searchedFiles);
  if (!result_.details.isEmpty())
    text += tr(" · %1 files or folders skipped").arg(result_.details.size());
  if (result_.resultLimitReached)
    text += QStringLiteral(" · ") + tr("Result limit reached");
  text = text.toHtmlEscaped();
  if (!result_.details.isEmpty() || result_.resultLimitReached)
    text += QStringLiteral(" · <a href=\"details\">%1</a>").arg(tr("Search details").toHtmlEscaped());
  summary_->setText(text);
}
void muffin::SearchPanelWidget::showDetails() {
  QDialog dialog(this);
  dialog.setObjectName("WorkspaceSearchDetails");
  dialog.setWindowTitle(tr("Search details"));
  dialog.resize(760, 440);
  auto* layout = new QVBoxLayout(&dialog);
  auto* scope = new QLabel(tr("%1 files searched").arg(result_.searchedFiles), &dialog);
  layout->addWidget(scope);
  auto* files = new QTreeWidget(&dialog);
  files->setHeaderLabels({tr("Path"), tr("Reason")});
  files->setColumnWidth(0, 400);
  files->setRootIsDecorated(false);
  layout->addWidget(files, 1);
  for (const auto& detail : result_.details) {
    QString reason;
    switch (detail.reason) {
    case WorkspaceSkipReason::TooLarge:
      reason = tr("File size exceeds %1 MiB").arg(result_.query.maxFileBytes / (1024 * 1024));
      break;
    case WorkspaceSkipReason::RegexTooLarge:
      reason = tr("Regular expression search is limited to %1 MiB per file")
                   .arg(result_.query.maxRegexBytes / (1024 * 1024));
      break;
    case WorkspaceSkipReason::Busy:
      reason = tr("File is loading or has conflicting unsaved versions");
      break;
    case WorkspaceSkipReason::Unreadable:
      reason = tr("Cannot read file or folder");
      break;
    case WorkspaceSkipReason::InvalidText:
      reason = tr("Unsupported encoding or binary content");
      break;
    case WorkspaceSkipReason::Changed:
      reason = tr("File changed during search");
      break;
    case WorkspaceSkipReason::ResourceLimit:
      reason = tr("Search exceeded its resource limit");
      break;
    case WorkspaceSkipReason::IgnoredDirectory:
      reason = tr("Ignored directory");
      break;
    }
    auto* item = new QTreeWidgetItem(files, {QDir(root_).relativeFilePath(detail.path), reason});
    item->setToolTip(0, detail.path);
    item->setToolTip(1, reason);
  }
  if (result_.resultLimitReached)
    layout->addWidget(new QLabel(tr("Result limit reached"), &dialog));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
}
void muffin::SearchPanelWidget::retranslateUi() {
  query_->setToolTip(QDir::toNativeSeparators(root_));
  query_->setPlaceholderText(tr("Search contents…"));
  query_->setAccessibleName(tr("Search contents"));
  case_->setText(QStringLiteral("Aa"));
  regex_->setText(QStringLiteral(".*"));
  case_->setToolTip(tr("Match case"));
  case_->setAccessibleName(case_->toolTip());
  regex_->setToolTip(tr("Regular expression"));
  regex_->setAccessibleName(regex_->toolTip());
  replacement_->setPlaceholderText(tr("Replace with…"));
  replacement_->setAccessibleName(tr("Replace with"));
  replaceToggle_->setText(tr("Replace"));
  preview_->setText(tr("Replace") + QChar(0x2026));
  preview_->setToolTip(tr("Preview folder replacement…"));
  search_->setText(tr("Search"));
  cancelButton_->setText(tr("Stop"));
  results_->setAccessibleName(tr("Content search results"));
  preview_->setEnabled(!running_ && result_.complete && result_.matches > 0);
  cancelButton_->setEnabled(running_);
  updateSummary();
}
void muffin::SearchPanelWidget::setThemeDefinition(const ThemeDefinition& definition) {
  const auto& colors = definition.colors;
  auto palette = results_->palette();
  palette.setColor(QPalette::Text, colors.chromeText);
  palette.setColor(QPalette::Highlight, colors.chromeSelection);
  palette.setColor(QPalette::HighlightedText, colors.chromeSelectionText);
  palette.setColor(QPalette::Link, colors.accent);
  palette.setColor(QPalette::BrightText, colors.accent.lightnessF() < 0.5 ? Qt::white : Qt::black);
  results_->setPalette(palette);
}
void muffin::SearchPanelWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange)
    retranslateUi();
  QWidget::changeEvent(event);
}

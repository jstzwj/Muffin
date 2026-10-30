// NOTE: This file contains tr() calls and therefore MUST NOT contain any
// `namespace muffin { }` block — lupdate loses the namespace prefix otherwise
// and generates context `QuickOpenDialog` instead of `muffin::QuickOpenDialog`,
// so translations never match at runtime (see CLAUDE.md). All QuickOpenDialog
// methods use fully-qualified `muffin::` names; tr()-free helpers live in an
// anonymous namespace at file scope (which the rule permits).

#include "app/QuickOpenDialog.h"
#include "app/SearchSettings.h"
#include "theme/ThemeDefinition.h"
#include "theme/ChromeStyleSheet.h"

#include <QCoreApplication>
#include <QApplication>
#include <QSet>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QFutureWatcher>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTimer>
#include <QtConcurrentRun>
#include <QVBoxLayout>
#include <algorithm>

namespace {

class FileDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    return {0, option.fontMetrics.height() * (index.data(Qt::UserRole + 1).toString().isEmpty() ? 1 : 2) + 16};
  }
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    painter->setClipRect(option.rect);
    const bool selected = option.state & QStyle::State_Selected;
    // Native Windows styles may paint a pale selected row while the palette
    // still offers white HighlightedText. Paint the paired theme colors together.
    if (selected) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(option.palette.color(QPalette::Highlight));
      painter->drawRoundedRect(option.rect.adjusted(2, 1, -2, -1), 4, 4);
    }
    painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
    const QRect area = option.rect.adjusted(10, 7, -10, -7);
    const int height = option.fontMetrics.height();
    const QString parent = index.data(Qt::UserRole + 1).toString();
    painter->drawText(QRect(area.x(), area.y(), area.width(), height), Qt::AlignVCenter,
                      option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, area.width()));
    if (!parent.isEmpty()) {
      painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::PlaceholderText));
      painter->drawText(QRect(area.x(), area.y() + height + 2, area.width(), height), Qt::AlignVCenter,
                        option.fontMetrics.elidedText(parent, Qt::ElideMiddle, area.width()));
    }
    painter->restore();
  }
};

} // namespace

muffin::QuickOpenDialog::QuickOpenDialog(QWidget* parent) : QDialog(parent) {
  setObjectName("QuickOpenDialog");
  setMinimumSize(560, 360);
  resize(640, 420);

  auto* layout = new QVBoxLayout(this);
  scopeLabel_ = new QLabel(this);
  scopeLabel_->setTextFormat(Qt::PlainText);
  scopeLabel_->setWordWrap(true);
  layout->addWidget(scopeLabel_);

  filterEdit_ = new QLineEdit(this);
  filterEdit_->setObjectName("QuickOpenFilter");
  filterEdit_->setAccessibleName(tr("Quick Open"));
  filterEdit_->installEventFilter(this);
  layout->addWidget(filterEdit_);

  list_ = new QListWidget(this);
  list_->setObjectName("QuickOpenResults");
  list_->setItemDelegate(new FileDelegate(list_));
  list_->setAccessibleName(tr("Matching files"));
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setSortingEnabled(false);
  layout->addWidget(list_, 1);
  statusLabel_ = new QLabel(this);
  statusLabel_->setWordWrap(true);
  statusLabel_->setObjectName("QuickOpenStatus");
  layout->addWidget(statusLabel_);

  filterTimer_ = new QTimer(this);
  filterTimer_->setSingleShot(true);
  filterTimer_->setInterval(70);
  connect(filterTimer_, &QTimer::timeout, this, [this] { applyFilter(filterEdit_->text()); });
  QObject::connect(filterEdit_, &QLineEdit::textChanged, filterTimer_, qOverload<>(&QTimer::start));
  QObject::connect(list_, &QListWidget::itemDoubleClicked, this, [this]() { accept(); });

  retranslateUi();
  setThemeDefinition(ThemeDefinition::builtIn(QStringLiteral("github")).value());
}

muffin::QuickOpenDialog::~QuickOpenDialog() {
  if (cancel_)
    cancel_->store(true);
}

void muffin::QuickOpenDialog::setWorkspace(const QString& root, const QStringList& extensions) {
  if (cancel_)
    cancel_->store(true);
  root_ = root;
  truncated_ = false;
  unreadable_ = 0;
  if (root.isEmpty()) {
    scanning_ = false;
    retranslateUi();
    return;
  }
  // History must not bypass the workspace's extension, hidden or ignore rules.
  // With a workspace, candidates come exclusively from the filtered scan.
  candidates_.clear();
  applyFilter(filterEdit_->text());
  cancel_ = std::make_shared<std::atomic_bool>(false);
  const auto cancellation = cancel_;
  scanning_ = true;
  auto* watcher = new QFutureWatcher<WorkspaceFiles>(this);
  connect(watcher, &QFutureWatcher<WorkspaceFiles>::finished, this, [this, watcher, cancellation] {
    const auto result = watcher->result();
    watcher->deleteLater();
    if (cancellation != cancel_)
      return;
    QSet<QString> seen;
    for (const auto& path : candidates_)
      seen.insert(WorkspaceFileIndex::pathKey(path));
    for (const auto& path : result.paths) {
      if (!seen.contains(WorkspaceFileIndex::pathKey(path)))
        candidates_.append(path);
    }
    scanning_ = false;
    truncated_ = result.truncated;
    unreadable_ = result.unreadableDirectories.size() + result.unreadableIgnoreFiles.size();
    applyFilter(filterEdit_->text());
  });
  const auto options = SearchSettings::load().scan;
  watcher->setFuture(QtConcurrent::run([root, extensions, cancellation, options] {
    return WorkspaceFileIndex::scan(root, extensions, cancellation, options);
  }));
  retranslateUi();
}

void muffin::QuickOpenDialog::setCandidates(QStringList paths) {
  candidates_.clear();
  QSet<QString> seen;
  for (const auto& path : paths) {
    const QString key = WorkspaceFileIndex::pathKey(path);
    if (!QFileInfo(path).isFile() || seen.contains(key))
      continue;
    candidates_.append(QFileInfo(path).absoluteFilePath());
    seen.insert(key);
  }
  applyFilter(filterEdit_->text());
  if (list_->count() > 0) {
    list_->setCurrentRow(0);
  }
  filterEdit_->setFocus();
  filterEdit_->selectAll();
}

QString muffin::QuickOpenDialog::selectedPath() const {
  if (const QListWidgetItem* item = list_->currentItem()) {
    return item->data(Qt::UserRole).toString();
  }
  return {};
}

bool muffin::QuickOpenDialog::eventFilter(QObject* watched, QEvent* event) {
  if (watched == filterEdit_ && event->type() == QEvent::KeyPress) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const int key = keyEvent->key();
    if (filterTimer_->isActive()) {
      filterTimer_->stop();
      applyFilter(filterEdit_->text());
    }
    if (key == Qt::Key_Down || key == Qt::Key_Up || key == Qt::Key_PageDown || key == Qt::Key_PageUp) {
      if (list_->count() > 0) {
        // Hand the navigation key to the list directly so the selection moves
        // even though the filter box keeps keyboard focus.
        QKeyEvent navigation(keyEvent->type(), key, keyEvent->modifiers(), keyEvent->text(), keyEvent->isAutoRepeat(),
                             keyEvent->count());
        QCoreApplication::sendEvent(list_, &navigation);
      }
      return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      if (list_->currentItem() != nullptr) {
        accept();
      }
      return true;
    }
    if (key == Qt::Key_Escape) {
      reject();
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void muffin::QuickOpenDialog::changeEvent(QEvent* event) {
  QDialog::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
}

void muffin::QuickOpenDialog::applyFilter(const QString& text) {
  const QString selected = text == lastFilter_ ? selectedPath() : QString();
  lastFilter_ = text;
  list_->clear();
  const QString needle = text.trimmed();
  const QString absoluteQuery = QFileInfo(needle).isAbsolute() ? WorkspaceFileIndex::pathKey(needle) : QString();
  struct Ranked {
    QString path;
    int score;
  };
  QVector<Ranked> ranked;
  const QDir rootDirectory(root_);
  for (const auto& path : candidates_) {
    const QString relative = root_.isEmpty() ? path : rootDirectory.relativeFilePath(path);
    const int score = !absoluteQuery.isEmpty() && absoluteQuery == WorkspaceFileIndex::pathKey(path)
                          ? 100000
                          : PathMatcher::score(needle, relative);
    if (score >= 0)
      ranked.append({path, score});
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.score > b.score; });
  const int shown = qMin<qsizetype>(ranked.size(), 200);
  int currentRow = 0;
  for (int i = 0; i < shown; ++i) {
    addPathItem(ranked[i].path);
    if (ranked[i].path == selected)
      currentRow = i;
  }
  if (shown)
    list_->setCurrentRow(currentRow);
  QString status = scanning_          ? tr("Scanning folder…")
                   : ranked.isEmpty() ? tr("No matching files")
                                      : tr("%1 matching files").arg(ranked.size());
  if (ranked.size() > shown)
    status += tr(" · Showing the first %1; type to narrow results").arg(shown);
  if (truncated_)
    status += tr(" · File limit reached; results are incomplete");
  if (unreadable_)
    status += tr(" · %1 unreadable folders skipped").arg(unreadable_);
  statusLabel_->setText(status);
}

void muffin::QuickOpenDialog::addPathItem(const QString& path) {
  auto* item = new QListWidgetItem(QFileInfo(path).fileName(), list_);
  item->setData(Qt::UserRole, path);
  QString parent =
      root_.isEmpty() ? QFileInfo(path).absolutePath() : QDir(root_).relativeFilePath(QFileInfo(path).absolutePath());
  if (parent == QLatin1String("."))
    parent.clear();
  item->setData(Qt::UserRole + 1, QDir::toNativeSeparators(parent));
  item->setData(Qt::AccessibleTextRole, item->text() + ' ' + item->data(Qt::UserRole + 1).toString());
  item->setToolTip(QDir::toNativeSeparators(path));
}

void muffin::QuickOpenDialog::setThemeDefinition(const ThemeDefinition& definition) {
  const auto& colors = definition.colors;
  auto palette = this->palette();
  palette.setColor(QPalette::Window, colors.canvas);
  palette.setColor(QPalette::WindowText, colors.chromeText);
  palette.setColor(QPalette::Base, colors.surface);
  palette.setColor(QPalette::Text, colors.chromeText);
  palette.setColor(QPalette::PlaceholderText, colors.chromeMuted);
  palette.setColor(QPalette::Highlight, colors.chromeSelection);
  palette.setColor(QPalette::HighlightedText, colors.chromeSelectionText);
  setPalette(palette);
  setStyleSheet(quickOpenStyleSheet(definition));
  // A stylesheet can resolve the view's palette independently of its parent.
  list_->setPalette(palette);
}

void muffin::QuickOpenDialog::retranslateUi() {
  setWindowTitle(tr("Quick Open"));
  filterEdit_->setPlaceholderText(tr("Search file names or paths…"));
  scopeLabel_->setText(root_.isEmpty() ? tr("Open and recent files") : QDir::toNativeSeparators(root_));
  applyFilter(filterEdit_->text());
}

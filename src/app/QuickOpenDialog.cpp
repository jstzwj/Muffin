// NOTE: This file contains tr() calls and therefore MUST NOT contain any
// `namespace muffin { }` block — lupdate loses the namespace prefix otherwise
// and generates context `QuickOpenDialog` instead of `muffin::QuickOpenDialog`,
// so translations never match at runtime (see CLAUDE.md). All QuickOpenDialog
// methods use fully-qualified `muffin::` names; tr()-free helpers live in an
// anonymous namespace at file scope (which the rule permits).

#include "app/QuickOpenDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

namespace {

bool nameContains(const QString& path, const QString& needle) {
  return QFileInfo(path).fileName().contains(needle, Qt::CaseInsensitive);
}

bool pathContains(const QString& path, const QString& needle) {
  return path.contains(needle, Qt::CaseInsensitive);
}

}  // namespace

muffin::QuickOpenDialog::QuickOpenDialog(QWidget* parent) : QDialog(parent) {
  setMinimumSize(560, 360);
  resize(640, 420);

  auto* layout = new QVBoxLayout(this);

  filterEdit_ = new QLineEdit(this);
  filterEdit_->installEventFilter(this);
  layout->addWidget(filterEdit_);

  list_ = new QListWidget(this);
  list_->setUniformItemSizes(true);
  list_->setSortingEnabled(false);
  layout->addWidget(list_, 1);

  QObject::connect(filterEdit_, &QLineEdit::textChanged, this, [this](const QString& text) { applyFilter(text); });
  QObject::connect(list_, &QListWidget::itemDoubleClicked, this, [this]() { accept(); });

  retranslateUi();
}

void muffin::QuickOpenDialog::setCandidates(QStringList paths) {
  candidates_ = std::move(paths);
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
    if (key == Qt::Key_Down || key == Qt::Key_Up || key == Qt::Key_PageDown || key == Qt::Key_PageUp ||
        key == Qt::Key_Home || key == Qt::Key_End) {
      if (list_->count() > 0) {
        // Hand the navigation key to the list directly so the selection moves
        // even though the filter box keeps keyboard focus.
        QCoreApplication::sendEvent(list_, keyEvent);
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
  list_->clear();
  const QString needle = text.trimmed();
  if (needle.isEmpty()) {
    for (const QString& path : candidates_) {
      addPathItem(path);
    }
  } else {
    // Name matches first, then path-only matches.
    for (const QString& path : candidates_) {
      if (nameContains(path, needle)) {
        addPathItem(path);
      }
    }
    for (const QString& path : candidates_) {
      if (!nameContains(path, needle) && pathContains(path, needle)) {
        addPathItem(path);
      }
    }
  }
  if (list_->count() > 0) {
    list_->setCurrentRow(0);
  }
}

void muffin::QuickOpenDialog::addPathItem(const QString& path) {
  auto* item = new QListWidgetItem(QFileInfo(path).fileName(), list_);
  item->setData(Qt::UserRole, path);
  item->setToolTip(QDir::toNativeSeparators(path));
}

void muffin::QuickOpenDialog::retranslateUi() {
  setWindowTitle(tr("Quick Open"));
  filterEdit_->setPlaceholderText(tr("Type to filter files…"));
}

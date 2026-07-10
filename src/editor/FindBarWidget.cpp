#include "editor/FindBarWidget.h"

#include <QEvent>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>

muffin::FindBarWidget::FindBarWidget(QWidget* parent) : QWidget(parent) {
  setupUi();
}

void muffin::FindBarWidget::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(6, 4, 6, 4);
  mainLayout->setSpacing(2);

  // Find row
  auto* findRow = new QHBoxLayout();
  findRow->setSpacing(4);

  findEdit_ = new QLineEdit(this);
  findEdit_->setMinimumWidth(180);
  connect(findEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
    Q_UNUSED(text)
    setResultInfo(0, 0);
  });
  connect(findEdit_, &QLineEdit::returnPressed, this, [this] {
    emit findRequested(findEdit_->text(), true, regularExpressionEnabled(), caseSensitiveEnabled());
  });

  prevButton_ = new QPushButton(this);
  connect(prevButton_, &QPushButton::clicked, this, [this] {
    emit findRequested(findEdit_->text(), false, regularExpressionEnabled(), caseSensitiveEnabled());
  });

  nextButton_ = new QPushButton(this);
  connect(nextButton_, &QPushButton::clicked, this, [this] {
    emit findRequested(findEdit_->text(), true, regularExpressionEnabled(), caseSensitiveEnabled());
  });

  regexCheck_ = new QCheckBox(this);
  caseCheck_ = new QCheckBox(this);
  connect(regexCheck_, &QCheckBox::toggled, this, [this] { setResultInfo(0, 0); });
  connect(caseCheck_, &QCheckBox::toggled, this, [this] { setResultInfo(0, 0); });

  resultLabel_ = new QLabel(this);
  resultLabel_->setMinimumWidth(60);

  closeButton_ = new QPushButton(this);
  connect(closeButton_, &QPushButton::clicked, this, &FindBarWidget::closed);

  findRow->addWidget(findEdit_);
  findRow->addWidget(prevButton_);
  findRow->addWidget(nextButton_);
  findRow->addWidget(regexCheck_);
  findRow->addWidget(caseCheck_);
  findRow->addWidget(resultLabel_);
  findRow->addStretch();
  findRow->addWidget(closeButton_);

  mainLayout->addLayout(findRow);

  // Replace row (hidden by default)
  replaceRow_ = new QWidget(this);
  auto* replaceLayout = new QHBoxLayout(replaceRow_);
  replaceLayout->setContentsMargins(0, 0, 0, 0);
  replaceLayout->setSpacing(4);

  replaceEdit_ = new QLineEdit(replaceRow_);
  replaceEdit_->setMinimumWidth(180);

  replaceButton_ = new QPushButton(replaceRow_);
  connect(replaceButton_, &QPushButton::clicked, this, [this] {
    emit replaceRequested(findEdit_->text(), replaceEdit_->text(),
                          regularExpressionEnabled(), caseSensitiveEnabled());
  });

  replaceAllButton_ = new QPushButton(replaceRow_);
  connect(replaceAllButton_, &QPushButton::clicked, this, [this] {
    emit replaceAllRequested(findEdit_->text(), replaceEdit_->text(),
                             regularExpressionEnabled(), caseSensitiveEnabled());
  });

  replaceLayout->addWidget(replaceEdit_);
  replaceLayout->addWidget(replaceButton_);
  replaceLayout->addWidget(replaceAllButton_);
  replaceLayout->addStretch();

  replaceRow_->setVisible(false);
  mainLayout->addWidget(replaceRow_);

  retranslateUi();
}

void muffin::FindBarWidget::retranslateUi() {
  findEdit_->setPlaceholderText(tr("Find"));
  prevButton_->setText(tr("Previous"));
  nextButton_->setText(tr("Next"));
  closeButton_->setText(tr("Close"));
  replaceEdit_->setPlaceholderText(tr("Replace"));
  replaceButton_->setText(tr("Replace"));
  replaceAllButton_->setText(tr("Replace All"));
  regexCheck_->setText(tr("Regex"));
  caseCheck_->setText(tr("Match Case"));
}

void muffin::FindBarWidget::setSearchText(const QString& text) {
  findEdit_->setText(text);
  findEdit_->selectAll();
}

QString muffin::FindBarWidget::searchText() const {
  return findEdit_->text();
}

bool muffin::FindBarWidget::regularExpressionEnabled() const {
  return regexCheck_ && regexCheck_->isChecked();
}

bool muffin::FindBarWidget::caseSensitiveEnabled() const {
  return caseCheck_ && caseCheck_->isChecked();
}

void muffin::FindBarWidget::setReplaceVisible(bool visible) {
  replaceRow_->setVisible(visible);
}

void muffin::FindBarWidget::activateFind() {
  findEdit_->setFocus();
  findEdit_->selectAll();
}

void muffin::FindBarWidget::activateReplace() {
  replaceRow_->setVisible(true);
  findEdit_->setFocus();
  findEdit_->selectAll();
}

void muffin::FindBarWidget::setResultInfo(int current, int total) {
  resultLabel_->setToolTip(QString());
  if (current < 0) {
    resultLabel_->setText(tr("Not found"));
  } else if (total > 0) {
    resultLabel_->setText(tr("%1/%2").arg(current + 1).arg(total));
  } else {
    resultLabel_->clear();
  }
}

void muffin::FindBarWidget::setErrorText(const QString& error) {
  resultLabel_->setText(error);
  resultLabel_->setToolTip(error);
}

void muffin::FindBarWidget::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    emit closed();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    const bool forward = !(event->modifiers() & Qt::ShiftModifier);
    emit findRequested(findEdit_->text(), forward,
                       regularExpressionEnabled(), caseSensitiveEnabled());
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void muffin::FindBarWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  activateFind();
}

void muffin::FindBarWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QWidget::changeEvent(event);
}

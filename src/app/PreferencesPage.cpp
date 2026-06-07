#include "app/PreferencesPage.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>

namespace muffin {

PreferencesPage::PreferencesPage(QWidget* parent) : QWidget(parent) {
  setStyleSheet(QStringLiteral("background:#ffffff;"));
}

QLabel* PreferencesPage::makeSectionLabel(QWidget* parent) const {
  auto* label = new QLabel(parent);
  label->setStyleSheet(QStringLiteral("font-weight:600; color:#111111;"));
  return label;
}

QLabel* PreferencesPage::makeMutedLabel(QWidget* parent) const {
  auto* label = new QLabel(parent);
  label->setStyleSheet(QStringLiteral("color:#666666;"));
  return label;
}

QPushButton* PreferencesPage::makeButton(QWidget* parent) const {
  return new QPushButton(parent);
}

QLabel* PreferencesPage::makeInfoIcon(QWidget* parent) const {
  auto* label = new QLabel(parent);
  const qreal dpr = qApp->devicePixelRatio();
  const int sz = 14;
  QPixmap px(static_cast<int>(sz * dpr), static_cast<int>(sz * dpr));
  px.setDevicePixelRatio(dpr);
  px.fill(Qt::transparent);
  QPainter p(&px);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(QColor(0x99, 0x99, 0x99));
  p.setPen(Qt::NoPen);
  p.drawEllipse(0, 0, sz, sz);
  p.setPen(Qt::white);
  QFont f;
  f.setPixelSize(10);
  f.setBold(true);
  p.setFont(f);
  p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, QStringLiteral("?"));
  p.end();
  label->setPixmap(px);
  return label;
}

}  // namespace muffin

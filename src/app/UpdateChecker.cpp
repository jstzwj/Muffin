#include "app/UpdateChecker.h"

#include <QApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QVersionNumber>

muffin::UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {
}

muffin::UpdateChecker& muffin::UpdateChecker::instance() {
  static UpdateChecker checker;
  return checker;
}

bool muffin::UpdateChecker::isUserInitiated() const {
  return userInitiated_;
}

void muffin::UpdateChecker::checkForUpdates() {
  if (checking_) {
    return;
  }
  checking_ = true;

  const QString userAgent = QStringLiteral("Muffin/%1").arg(QApplication::applicationVersion());

  QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/jstzwj/Muffin/releases/latest")));
  request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

  QNetworkReply* reply = network_.get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater();
    checking_ = false;

    QSettings().setValue(QStringLiteral("update/lastChecked"), QDateTime::currentDateTime());

    if (reply->error() != QNetworkReply::NoError) {
      emit checkFailed(reply->errorString());
      return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
      emit checkFailed(parseError.errorString());
      return;
    }

    const QJsonObject obj = doc.object();
    const QString tagName = obj.value(QStringLiteral("tag_name")).toString();
    const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString();

    // Strip optional "v" prefix (e.g. "v0.2.1" -> "0.2.1")
    const QString remoteVersion = tagName.startsWith(QChar('v')) ? tagName.mid(1) : tagName;
    const QString currentVersion = QApplication::applicationVersion();

    const QVersionNumber remote = QVersionNumber::fromString(remoteVersion);
    const QVersionNumber current = QVersionNumber::fromString(currentVersion);

    if (remote > current) {
      emit updateAvailable(remoteVersion, htmlUrl);
    } else {
      emit upToDate();
    }
  });
}

void muffin::UpdateChecker::maybeAutoCheck() {
  QSettings settings;
  const bool autoCheck = settings.value(QStringLiteral("update/autoCheck"), true).toBool();
  if (!autoCheck) {
    return;
  }

  const QDateTime lastChecked = settings.value(QStringLiteral("update/lastChecked")).toDateTime();
  if (lastChecked.isValid() && lastChecked.secsTo(QDateTime::currentDateTime()) < 86400) {
    return;  // Less than 24 hours since last check
  }

  userInitiated_ = false;
  checkForUpdates();
}

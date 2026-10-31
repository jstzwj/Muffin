#include "app/UpdateChecker.h"

#include <QApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QVersionNumber>
#include <algorithm>
#include <memory>

namespace {
constexpr qsizetype kCatalogLimit = 128 * 1024;
}

muffin::UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent),
      sources_{QUrl(QStringLiteral("https://cnb.cool/jstzwj/Muffin/-/releases/download/updates/muffin-update.json")),
               QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/releases/latest/download/muffin-update.json"))} {
}

muffin::UpdateChecker& muffin::UpdateChecker::instance() {
  static UpdateChecker checker;
  return checker;
}

QString muffin::UpdateChecker::cacheKey(int source) const {
  // Keep validators bound to the exact resource, including in tests.
  return QStringLiteral("update/catalog/") +
         QString::fromLatin1(sources_[source].toEncoded().toBase64(QByteArray::Base64UrlEncoding));
}

bool muffin::UpdateChecker::acceptCatalog(const QByteArray& data, QObject* requester) {
  if (data.isEmpty() || data.size() > kCatalogLimit)
    return false;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(data, &error);
  const auto object = document.object();
  if (error.error != QJsonParseError::NoError || object.value(QStringLiteral("schema")).toInt() != 1)
    return false;
  auto release = UpdatePackage::release(object, UpdatePackage::architectureName());
  if (!release)
    return false;
  const auto current = QVersionNumber::fromString(QApplication::applicationVersion());
  if (current.isNull())
    return false;
  latestPackage_ = std::move(release);
  QSettings settings;
  settings.setValue(QStringLiteral("update/lastChecked"), QDateTime::currentDateTimeUtc());
  settings.remove(QStringLiteral("update/failureCount"));
  settings.remove(QStringLiteral("update/retryAt"));
  if (QVersionNumber::fromString(latestPackage_->version) > current)
    emit updateAvailable(latestPackage_->version, latestPackage_->page.toString(), requester);
  else
    emit upToDate(requester);
  return true;
}

bool muffin::UpdateChecker::checkForUpdates(QObject* requester) {
  if (checking_)
    return false;
  checking_ = true;
  QSettings().setValue(QStringLiteral("update/lastAttempt"), QDateTime::currentDateTimeUtc());
  requestCatalog(QPointer<QObject>(requester), requester != nullptr, 0);
  return true;
}

void muffin::UpdateChecker::requestCatalog(QPointer<QObject> owner, bool manual, int source) {
  if (manual && owner.isNull()) {
    checking_ = false;
    return;
  }
  QSettings settings;
  if (source >= sources_.size()) {
    checking_ = false;
    const int failures = std::clamp(settings.value(QStringLiteral("update/failureCount"), 0).toInt(), 0, 5) + 1;
    settings.setValue(QStringLiteral("update/failureCount"), failures);
    settings.setValue(QStringLiteral("update/retryAt"),
                      QDateTime::currentDateTimeUtc().addSecs(std::min(86400, 3600 * (1 << (failures - 1)))));
    emit checkFailed(
        tr("Could not reach the update service. Please try again later or download Muffin from the official release page."),
        owner.data());
    return;
  }
  const QString key = cacheKey(source);
  const auto now = QDateTime::currentDateTimeUtc();
  if (settings.value(key + QStringLiteral("/retryAt")).toDateTime() > now) {
    requestCatalog(owner, manual, source + 1);
    return;
  }
  QNetworkRequest request(sources_[source]);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("Muffin/%1").arg(QApplication::applicationVersion()));
  request.setRawHeader("Accept", "application/json");
  const auto cached = settings.value(key + QStringLiteral("/body")).toByteArray();
  const auto etag = settings.value(key + QStringLiteral("/etag")).toByteArray();
  if (!cached.isEmpty() && cached.size() <= kCatalogLimit && !etag.isEmpty())
    request.setRawHeader("If-None-Match", etag);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(15000);
  auto* reply = network_.get(request);
  reply->setReadBufferSize(kCatalogLimit + 1);
  QTimer::singleShot(20000, reply, [reply] {
    if (reply->isRunning())
      reply->abort();
  });
  const auto buffer = std::make_shared<QByteArray>();
  const auto consume = [reply, buffer] {
    if (reply->bytesAvailable() > 0)
      buffer->append(reply->readAll());
    if (buffer->size() > kCatalogLimit)
      reply->abort();
  };
  connect(reply, &QNetworkReply::readyRead, this, consume);
  connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, consume, cached, key, owner, manual, source] {
    consume();
    reply->deleteLater();
    if (manual && owner.isNull()) {
      checking_ = false;
      return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 403 || status == 429) {
      const auto now = QDateTime::currentDateTimeUtc();
      bool secondsValid = false;
      const auto header = reply->rawHeader("Retry-After");
      const auto seconds = header.toLongLong(&secondsValid);
      auto retry = secondsValid ? now.addSecs(std::clamp<qint64>(seconds, 60, 86400))
                                : QDateTime::fromString(QString::fromLatin1(header), Qt::RFC2822Date);
      if (!retry.isValid() || retry < now)
        retry = now.addSecs(3600);
      QSettings().setValue(key + QStringLiteral("/retryAt"), std::min(retry, now.addDays(1)));
    }
    const bool usable = reply->error() == QNetworkReply::NoError && (status == 200 || status == 304);
    const auto data = status == 304 ? cached : *buffer;
    // Never report "up to date" using an offline cache. A 304 is a fresh server validation.
    if (usable) {
      checking_ = false;
      if (acceptCatalog(data, owner.data())) {
        if (status == 200) {
          QSettings settings;
          settings.setValue(key + QStringLiteral("/body"), data);
          settings.setValue(key + QStringLiteral("/etag"), reply->rawHeader("ETag"));
        }
        return;
      }
      checking_ = true;
    }
    requestCatalog(owner, manual, source + 1);
  });
}

void muffin::UpdateChecker::maybeAutoCheck() {
  QSettings settings;
  if (!settings.value(QStringLiteral("update/autoCheck"), true).toBool())
    return;
  const auto now = QDateTime::currentDateTimeUtc();
  const auto last = settings.value(QStringLiteral("update/lastChecked")).toDateTime();
  const auto attempted = settings.value(QStringLiteral("update/lastAttempt")).toDateTime();
  if ((last.isValid() && last.secsTo(now) >= 0 && last.secsTo(now) < 86400) ||
      (attempted.isValid() && attempted.secsTo(now) >= 0 && attempted.secsTo(now) < 3600) ||
      settings.value(QStringLiteral("update/retryAt")).toDateTime() > now)
    return;
  checkForUpdates(nullptr);
}

// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bootstrapfetch.h"

#include "bootstrap.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>

static const int IDLE_TIMEOUT_MS = 45000;   // without any data
static const int MAX_REDIRECTS = 5;

bool QtBootstrapFetch(const std::string& url, const std::string& path, uint64_t maxBytes,
                      std::atomic<uint64_t>* progress, std::string* body, std::string& error)
{
    const QUrl target(QString::fromStdString(url));
    const QString scheme = target.scheme().toLower();
    if (!target.isValid() || (scheme != "http" && scheme != "https")) {
        error = "only http:// and https:// addresses are supported: " + url;
        return false;
    }
    if (scheme == "https" && !QSslSocket::supportsSsl()) {
        error = "this build cannot do https:// (no TLS library)";
        return false;
    }

    QFile file;
    quint64 offset = 0;
    if (!path.empty()) {
        // the path comes in the 8-bit encoding of file names of this system
        file.setFileName(QFile::decodeName(QByteArray(path.c_str())));
        if (file.exists())
            offset = file.size();
        if (offset >= maxBytes) {
            // nothing sensible to resume: start again
            file.remove();
            offset = 0;
        }
        if (!file.open(offset > 0 ? QIODevice::Append : QIODevice::WriteOnly)) {
            error = "cannot write " + path;
            return false;
        }
        if (progress)
            *progress += offset;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(target);
    // redirects are followed, but not from https to http
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setMaximumRedirectsAllowed(MAX_REDIRECTS);
    request.setRawHeader("User-Agent", "aokchain-bootstrap");
    if (offset > 0)
        request.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QTimer tick;
    QElapsedTimer idle;
    idle.start();
    quint64 received = 0;
    QString failure;
    QByteArray collected;

    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        idle.restart();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 && status != 206) {
            reply->readAll();
            return;
        }
        if (status == 200 && offset > 0) {
            // the server does not do ranges and sends the whole file: start the file again
            if (!file.resize(0) || !file.seek(0)) {
                failure = "cannot write " + QString::fromStdString(path);
                reply->abort();
                return;
            }
            if (progress)
                *progress -= offset;
            offset = 0;
        }
        const QByteArray data = reply->readAll();
        received += data.size();
        if (offset + received > maxBytes) {
            failure = "the server sends more than the manifest says";
            reply->abort();
            return;
        }
        if (file.isOpen()) {
            if (file.write(data) != data.size()) {
                failure = "cannot write " + QString::fromStdString(path) + " (disk full?)";
                reply->abort();
                return;
            }
        } else {
            collected.append(data);
        }
        if (progress)
            *progress += data.size();
    });
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        if (BootstrapCancelled()) {
            failure = "cancelled";
            reply->abort();
        } else if (idle.elapsed() > IDLE_TIMEOUT_MS) {
            failure = "no data from the server for too long";
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    tick.start(300);
    loop.exec();
    tick.stop();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkText = reply->errorString();
    reply->deleteLater();
    if (file.isOpen())
        file.close();

    if (!failure.isEmpty()) {
        error = failure.toStdString();
        return false;
    }
    if (status == 416 && offset > 0) {
        // the range starts after the end: what is on disk is not a part of this file
        file.remove();
        if (progress)
            *progress -= offset;
        error = "the partial file did not fit; starting again";
        return false;
    }
    if (networkError != QNetworkReply::NoError) {
        // a certificate that cannot be verified ends here: the transfer is not continued
        error = networkText.toStdString();
        return false;
    }
    if (status != 200 && status != 206) {
        error = QString("HTTP status %1 for %2").arg(status).arg(QString::fromStdString(url)).toStdString();
        return false;
    }
    if (body)
        *body = std::string(collected.constData(), collected.size());
    return true;
}

#include "ProxyManager.h"

#include "encode/Proxy.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

const char* stateName(ProxyManager::State state)
{
    switch (state) {
    case ProxyManager::State::Queued:
        return "queued";
    case ProxyManager::State::Making:
        return "making";
    case ProxyManager::State::Ready:
        return "ready";
    case ProxyManager::State::Failed:
        return "failed";
    case ProxyManager::State::None:
        break;
    }
    return "none";
}

} // namespace

ProxyManager::ProxyManager(QObject* parent) : QObject(parent)
{
    const QByteArray override = qgetenv("TVE_PROXY_DIR");
    m_directory = !override.isEmpty()
                      ? QString::fromLocal8Bit(override)
                      : QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/Proxies");
}

ProxyManager::~ProxyManager()
{
    m_cancel = true;
    if (m_worker.joinable())
        m_worker.join();
}

void ProxyManager::setDirectory(const QString& directory)
{
    m_directory = directory;
}

QString ProxyManager::proxyPath(const QString& source) const
{
    const QFileInfo info(source);
    const QByteArray key = source.toUtf8() + '|' + QByteArray::number(info.size()) + '|'
                         + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return m_directory + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex())
         + QStringLiteral("_720p.mp4");
}

QString ProxyManager::readyProxy(const QString& source) const
{
    if (const auto job = m_jobs.find(source); job != m_jobs.end() && job->second.state == State::Making)
        return {};
    const QString path = proxyPath(source);
    return QFileInfo::exists(path) ? path : QString();
}

ProxyManager::State ProxyManager::state(const QString& source) const
{
    if (const auto job = m_jobs.find(source); job != m_jobs.end() && job->second.state != State::Ready)
        return job->second.state;
    return readyProxy(source).isEmpty() ? State::None : State::Ready;
}

double ProxyManager::progress(const QString& source) const
{
    const auto job = m_jobs.find(source);
    return job == m_jobs.end() ? 0.0 : job->second.progress;
}

QVariantMap ProxyManager::states(const QStringList& sources) const
{
    QVariantMap map;
    for (const QString& source : sources) {
        const State s = state(source);
        if (s == State::None)
            continue;
        const auto job = m_jobs.find(source);
        map[source] = QVariantMap{
            {QStringLiteral("state"), QString::fromLatin1(stateName(s))},
            {QStringLiteral("progress"), s == State::Ready ? 1.0 : progress(source)},
            {QStringLiteral("error"), job == m_jobs.end() ? QString() : job->second.error},
        };
    }
    return map;
}

void ProxyManager::request(const QString& source)
{
    const State s = state(source);
    if (s == State::Ready || s == State::Queued || s == State::Making)
        return;
    m_jobs[source] = Job{State::Queued, 0, {}};
    m_queue << source;
    emit changed(source, false);
    startNext();
}

void ProxyManager::remove(const QString& source)
{
    m_queue.removeAll(source);
    if (source == m_current) {
        m_cancel = true; // the worker deletes its partial file; startNext() runs when it ends
        m_jobs[source] = Job{State::None, 0, {}};
    } else {
        m_jobs.erase(source);
        QFile::remove(proxyPath(source));
    }
    emit changed(source, false);
}

void ProxyManager::startNext()
{
    if (m_worker.joinable() || m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    m_jobs[m_current].state = State::Making;
    m_cancel = false;
    const QString source = m_current;
    const QString destination = proxyPath(source);
    emit changed(source, false);
    m_worker = std::thread([this, source, destination] {
        // Progress reaches the UI thread at most every 1 %.
        double reported = 0;
        std::string error;
        const bool ok = tve::makeProxy(source.toStdString(), destination.toStdString(), tve::ProxyOptions{},
                                       [&](double p) {
                                           if (p - reported >= 0.01) {
                                               reported = p;
                                               QMetaObject::invokeMethod(this, [this, source, p] {
                                                   if (m_current == source && m_jobs[source].state == State::Making) {
                                                       m_jobs[source].progress = p;
                                                       emit changed(source, false);
                                                   }
                                               }, Qt::QueuedConnection);
                                           }
                                           return !m_cancel.load();
                                       },
                                       &error);
        QMetaObject::invokeMethod(this, [this, source, ok, error] {
            if (m_worker.joinable())
                m_worker.join();
            m_current.clear();
            Job& job = m_jobs[source];
            if (job.state == State::Making) { // not removed meanwhile
                job.state = ok ? State::Ready : error.empty() ? State::None : State::Failed;
                job.progress = ok ? 1.0 : 0.0;
                job.error = QString::fromStdString(error);
            }
            emit changed(source, ok && job.state == State::Ready);
            startNext();
        }, Qt::QueuedConnection);
    });
}

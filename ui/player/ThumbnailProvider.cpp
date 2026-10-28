#include "ThumbnailProvider.h"

#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"
#include "StillImage.h"
#include "Stickers.h"

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QRunnable>
#include <QUrl>

#include <list>
#include <memory>

namespace {

// Thumbnails already made, keyed by request id. Cost is in kilobytes (~32 MB total).
QCache<QString, QImage>& cache()
{
    static QCache<QString, QImage> instance(32 * 1024);
    return instance;
}

QMutex& cacheMutex()
{
    static QMutex mutex;
    return mutex;
}

// Idle decoders, most recently used first. Opening a file (and its hardware session) costs
// more than the seek, so a strip of tiles from one clip reuses a few decoders.
struct IdleDecoder {
    QString path;
    std::unique_ptr<tve::VideoDecoder> decoder;
};
constexpr std::size_t kMaxIdleDecoders = 6;

std::list<IdleDecoder>& idleDecoders()
{
    static std::list<IdleDecoder> instance;
    return instance;
}

QMutex& decoderMutex()
{
    static QMutex mutex;
    return mutex;
}

std::unique_ptr<tve::VideoDecoder> takeDecoder(const QString& path)
{
    {
        QMutexLocker lock(&decoderMutex());
        auto& idle = idleDecoders();
        for (auto it = idle.begin(); it != idle.end(); ++it) {
            if (it->path == path) {
                auto decoder = std::move(it->decoder);
                idle.erase(it);
                return decoder;
            }
        }
    }
    return tve::VideoDecoder::open(path.toStdString());
}

void returnDecoder(const QString& path, std::unique_ptr<tve::VideoDecoder> decoder)
{
    QMutexLocker lock(&decoderMutex());
    auto& idle = idleDecoders();
    idle.push_front({path, std::move(decoder)});
    if (idle.size() > kMaxIdleDecoders)
        idle.pop_back();
}

class ThumbnailResponse : public QQuickImageResponse, public QRunnable {
public:
    explicit ThumbnailResponse(QString id) : m_id(std::move(id)) { setAutoDelete(false); }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }

    void run() override
    {
        {
            QMutexLocker lock(&cacheMutex());
            if (const QImage* cached = cache().object(m_id)) {
                m_image = *cached;
                emit finished();
                return;
            }
        }
        // [!]<url-encoded path>@<seconds>@<height>
        const qsizetype heightAt = m_id.lastIndexOf(QLatin1Char('@'));
        const qsizetype timeAt = m_id.lastIndexOf(QLatin1Char('@'), heightAt - 1);
        if (heightAt < 0 || timeAt < 0) {
            m_error = QStringLiteral("bad thumbnail id");
            emit finished();
            return;
        }
        const qsizetype pathAt = m_id.startsWith(QLatin1Char('!')) ? 1 : 0;
        const QString path = QUrl::fromPercentEncoding(m_id.mid(pathAt, timeAt - pathAt).toUtf8());
        const double seconds = m_id.mid(timeAt + 1, heightAt - timeAt - 1).toDouble();
        const int height = m_id.mid(heightAt + 1).toInt();

        if (tve::isLottieFile(path.toStdString())) {
            // A sticker: drawn at that moment of its loop.
            if (auto animation = stickers::open(path))
                m_image = stickers::frame(*animation, seconds, std::max(1, height));
            else
                m_error = QStringLiteral("cannot read %1").arg(path);
            emit finished();
            return;
        }
        if (still::isStillImage(path)) {
            // A still looks the same at every time: one thumbnail per height serves them all.
            const QString key = QStringLiteral("still:%1@%2").arg(path).arg(height);
            {
                QMutexLocker lock(&cacheMutex());
                if (const QImage* cached = cache().object(key)) {
                    m_image = *cached;
                    emit finished();
                    return;
                }
            }
            const QImage full = still::read(path, 0);
            if (full.isNull()) {
                m_error = QStringLiteral("cannot read %1").arg(path);
            } else {
                m_image = full.scaledToHeight(std::max(1, height), Qt::SmoothTransformation);
                QMutexLocker lock(&cacheMutex());
                cache().insert(key, new QImage(m_image), int(m_image.sizeInBytes() / 1024) + 1);
            }
            emit finished();
            return;
        }
        auto decoder = takeDecoder(path);
        std::optional<tve::RgbaImage> thumb;
        if (decoder) {
            thumb = tve::extractThumbnail(*decoder, tve::fromSeconds(seconds), height);
            returnDecoder(path, std::move(decoder));
        }
        if (thumb) {
            m_image = QImage(thumb->pixels.data(), thumb->width, thumb->height, QImage::Format_RGBA8888).copy();
            QMutexLocker lock(&cacheMutex());
            cache().insert(m_id, new QImage(m_image), int(m_image.sizeInBytes() / 1024) + 1);
        } else {
            m_error = QStringLiteral("no frame at %1 s in %2").arg(seconds).arg(path);
        }
        emit finished();
    }

private:
    QString m_id;
    QImage m_image;
    QString m_error;
};

} // namespace

ThumbnailProvider::ThumbnailProvider()
{
    // Two decodes at a time: enough to fill a strip quickly without starving playback,
    // which shares the hardware decoder.
    m_pool.setMaxThreadCount(2);
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize&)
{
    auto* response = new ThumbnailResponse(id);
    // "!" asks for the front of the queue: media bin tiles, few and looked at first.
    m_pool.start(response, id.startsWith(QLatin1Char('!')) ? 1 : 0);
    return response;
}

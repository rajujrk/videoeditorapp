#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

// Serves video thumbnails to QML as image://tvethumb/<url-encoded path>@<seconds>@<height>.
// Frames are decoded on a thread pool and cached in memory; a leading "!" on the id puts the
// request ahead of the others.
class ThumbnailProvider : public QQuickAsyncImageProvider {
public:
    static constexpr const char* kId = "tvethumb";

    ThumbnailProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    QThreadPool m_pool;
};

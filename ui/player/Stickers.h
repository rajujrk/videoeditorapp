#pragma once

#include "decode/LottieAnimation.h"

#include <QImage>
#include <QQuickImageProvider>
#include <QString>
#include <QVariantList>

#include <memory>

// Stickers: Lottie animations, built in (effects/stickers, made by tools/make_stickers.py,
// at ":/effects/stickers/…") or imported.
namespace stickers {

// The built-in stickers: [{id, title, category, path}], path being their resource.
const QVariantList& catalog();
// The resource path of a built-in sticker, empty if there is none by that id.
QString pathOf(const QString& id);
// A built-in sticker's title ("Star · Spin") from its path; empty for other files.
QString titleOf(const QString& path);

// Reads a Lottie file (a resource path too); nullptr if it is not one.
std::unique_ptr<tve::LottieAnimation> open(const QString& path);

// One frame as an image (straight-alpha RGBA) of `height` pixels, keeping the shape.
QImage frame(tve::LottieAnimation& animation, double seconds, int height);

} // namespace stickers

// Library previews: image://tvesticker/<id>@<seconds> is that built-in sticker at that
// moment, 160 pixels tall.
class StickerPreviewProvider : public QQuickImageProvider {
public:
    static constexpr const char* kId = "tvesticker";
    static constexpr int kSize = 160;

    StickerPreviewProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

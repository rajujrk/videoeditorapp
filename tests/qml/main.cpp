// Runs the tst_*.qml files in this folder against the app's static QML modules.
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

#include "ThumbnailProvider.h"
#include "TransitionPreviewProvider.h"
#include "EffectPreviewProvider.h"
#include "Stickers.h"
#include "LookLibrary.h"
#include "TitleRenderer.h"

Q_IMPORT_QML_PLUGIN(TechieVideoEditor_ThemePlugin)
Q_IMPORT_QML_PLUGIN(TechieVideoEditor_PlayerPlugin)
Q_IMPORT_QML_PLUGIN(TechieVideoEditor_ShellPlugin)

// Still images for the tests, written at start-up: a 400 × 200 picture, red on the left and
// blue on the right, as PNG, JPEG, WebP, TIFF and HEIC; the same JPEG with an EXIF tag saying
// "turn a quarter clockwise" (so it shows 200 × 400, red on top); and a PNG with a
// transparent background around a green disc.
void writeStills(const QString& dir)
{
    QImage halves(400, 200, QImage::Format_RGB32);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 400; ++x)
            halves.setPixel(x, y, x < 200 ? qRgb(220, 30, 30) : qRgb(30, 60, 220));
    for (const char* format : {"png", "jpg", "webp", "tiff", "heic"})
        halves.save(dir + QStringLiteral("/halves.") + QLatin1String(format), format, 95);

    // An Exif APP1 segment with Orientation = 6, after the JPEG's start-of-image marker.
    QFile jpeg(dir + QStringLiteral("/halves.jpg"));
    if (!jpeg.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = jpeg.readAll();
    const unsigned char exif[] = {0xFF, 0xE1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0,
                                  'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,  // TIFF header, IFD at 8
                                  0x01, 0x00,                                    // one entry
                                  0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, // Orientation = 6
                                  0x00, 0x00, 0x00, 0x00};                       // no next IFD
    QFile rotated(dir + QStringLiteral("/rotated.jpg"));
    if (!rotated.open(QIODevice::WriteOnly))
        return;
    rotated.write(data.left(2));
    rotated.write(reinterpret_cast<const char*>(exif), sizeof exif);
    rotated.write(data.mid(2));

    QImage disc(200, 200, QImage::Format_ARGB32);
    disc.fill(Qt::transparent);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x)
            if ((x - 100) * (x - 100) + (y - 100) * (y - 100) < 60 * 60)
                disc.setPixel(x, y, qRgba(40, 200, 80, 255));
    disc.save(dir + QStringLiteral("/disc.png"));

    // A green screen (16:9, so it fills the canvas) with a red square in the middle.
    QImage keyed(320, 180, QImage::Format_RGB32);
    keyed.fill(qRgb(0, 176, 64));
    for (int y = 60; y < 120; ++y)
        for (int x = 120; x < 200; ++x)
            keyed.setPixel(x, y, qRgb(220, 30, 30));
    keyed.save(dir + QStringLiteral("/keyed.png"));
}

// Gives tests the sample-media folder and a scratch folder as context properties.
class Setup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("samplesDir"), QStringLiteral(TVE_SAMPLES_DIR));
        engine->rootContext()->setContextProperty(QStringLiteral("tempDir"), QDir::tempPath());
        engine->rootContext()->setContextProperty(QStringLiteral("recordingsDir"), qEnvironmentVariable("TVE_RECORDING_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("fixturesDir"), QStringLiteral(TVE_QML_TESTS_DIR "/fixtures"));
        engine->rootContext()->setContextProperty(QStringLiteral("stillsDir"), qEnvironmentVariable("TVE_TEST_STILLS_DIR"));
        engine->addImageProvider(QString::fromLatin1(ThumbnailProvider::kId), new ThumbnailProvider);
        engine->addImageProvider(QString::fromLatin1(TransitionPreviewProvider::kId), new TransitionPreviewProvider);
        engine->addImageProvider(QString::fromLatin1(EffectPreviewProvider::kId), new EffectPreviewProvider);
        engine->addImageProvider(QString::fromLatin1(StickerPreviewProvider::kId), new StickerPreviewProvider);
        engine->addImageProvider(QString::fromLatin1(TitlePreviewProvider::kId), new TitlePreviewProvider);
        engine->addImageProvider(QString::fromLatin1(LookPreviewProvider::kId), new LookPreviewProvider);
    }
};

int main(int argc, char** argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // Real-time audio callbacks without a sound card: meters work, nothing is heard.
    qputenv("TVE_AUDIO", "null");
    // Autosaves go to a scratch folder that is removed afterwards, never the user's own.
    QTemporaryDir autosave;
    qputenv("TVE_AUTOSAVE_DIR", autosave.path().toLocal8Bit());
    QTemporaryDir proxies; // likewise proxies
    qputenv("TVE_PROXY_DIR", proxies.path().toLocal8Bit());
    // Voice-overs record real-time silence (no microphone, no permission prompt) into a
    // scratch folder.
    qputenv("TVE_AUDIO_INPUT", "null");
    QTemporaryDir recordings;
    qputenv("TVE_RECORDING_DIR", recordings.path().toLocal8Bit());
    QTemporaryDir stills;
    writeStills(stills.path());
    qputenv("TVE_TEST_STILLS_DIR", stills.path().toLocal8Bit());
    QTemporaryDir colorPresets; // colour presets saved by tests
    qputenv("TVE_COLOR_PRESET_DIR", colorPresets.path().toLocal8Bit());
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "tve_qml_tests", TVE_QML_TESTS_DIR, &setup);
}

#include "main.moc"

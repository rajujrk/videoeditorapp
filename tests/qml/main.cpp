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
#include "decode/AudioDecoder.h"
#include "encode/MediaWriter.h"

#include <cmath>
#include <functional>
#include <numbers>
#include <random>

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

// File chores for tests (moving media away to relink it, reading the keymap), confined to
// one scratch folder (`scratchDir`); paths outside it are refused.
class TestFiles : public QObject {
    Q_OBJECT
public:
    explicit TestFiles(QString root) : m_root(QDir(root).canonicalPath()) {}

    Q_INVOKABLE bool copy(const QString& from, const QString& to) const
    {
        return inside(to) && QDir().mkpath(QFileInfo(to).path()) && QFile::copy(from, to);
    }
    Q_INVOKABLE bool move(const QString& from, const QString& to) const
    {
        return inside(from) && inside(to) && QDir().mkpath(QFileInfo(to).path()) && QDir().rename(from, to);
    }
    Q_INVOKABLE bool exists(const QString& path) const { return QFileInfo::exists(path); }
    Q_INVOKABLE QString read(const QString& path) const
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

private:
    bool inside(const QString& path) const
    {
        return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).startsWith(m_root + QLatin1Char('/'));
    }
    QString m_root;
};

// Media for the analysis tests, written at start-up:
//   talk.wav / talk.mov  6.5 s: sound 0–1, 2–3, 3.3–4.3 and 5.8–6.5 s (a 300 Hz tone), else silence;
//                        the MOV adds grey pictures (so it goes on a video track)
//   beat.wav             8 s of noise clicks at 120 BPM from 0.25 s
//   hum.wav              7 s of a quiet 220 Hz tone (music to duck)
//   scenes.mov           three one-second shots at 30 fps, each moving
void writeAnalysisMedia(const QString& dir)
{
    using namespace tve;
    const auto talkAt = [](double t) {
        const bool on = t < 1.0 || (t >= 2.0 && t < 3.0) || (t >= 3.3 && t < 4.3) || t >= 5.8;
        return on ? 0.25f * float(std::sin(2.0 * std::numbers::pi * 300.0 * t)) : 0.0f;
    };
    const auto writeSound = [&](const QString& name, double length, const std::function<float(double)>& at) {
        MediaSettings settings;
        settings.container = Container::Wav;
        settings.audioCodec = AudioCodec::Pcm16;
        auto writer = MediaWriter::open((dir + QLatin1Char('/') + name).toStdString(), settings);
        std::vector<float> samples(std::size_t(length * audio::kSampleRate) * 2);
        for (std::size_t i = 0; i < samples.size() / 2; ++i)
            samples[i * 2] = samples[i * 2 + 1] = at(double(i) / audio::kSampleRate);
        writer->writeAudio(samples.data(), int(samples.size() / 2));
        writer->finish();
    };
    writeSound(QStringLiteral("talk.wav"), 6.5, talkAt);
    std::mt19937 random(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    writeSound(QStringLiteral("beat.wav"), 8.0, [&](double t) {
        const double into = std::fmod(t - 0.25, 0.5);
        return t >= 0.25 && into < 0.02 ? 0.6f * noise(random) * float(1.0 - into / 0.02) : 0.0f;
    });
    writeSound(QStringLiteral("hum.wav"), 7.0, [](double t) { return 0.1f * float(std::sin(2.0 * std::numbers::pi * 220.0 * t)); });

    const auto writeMovie = [&](const QString& name, int frames, bool sound, const std::function<void(int, std::vector<std::uint8_t>&)>& draw) {
        MediaSettings settings;
        settings.container = Container::Mov;
        settings.codec = VideoCodec::ProRes;
        settings.proRes = ProResProfile::Proxy;
        settings.width = 160;
        settings.height = 90;
        settings.audio = sound;
        settings.audioCodec = AudioCodec::Pcm16;
        auto writer = MediaWriter::open((dir + QLatin1Char('/') + name).toStdString(), settings);
        std::vector<std::uint8_t> rgba(160 * 90 * 4);
        std::vector<float> samples(std::size_t(audio::kSampleRate / 30) * 2);
        for (int f = 0; f < frames; ++f) {
            draw(f, rgba);
            writer->writeVideoRgba(rgba.data(), 160 * 4);
            if (sound) {
                for (std::size_t i = 0; i < samples.size() / 2; ++i)
                    samples[i * 2] = samples[i * 2 + 1] = talkAt(f / 30.0 + double(i) / audio::kSampleRate);
                writer->writeAudio(samples.data(), int(samples.size() / 2));
            }
        }
        writer->finish();
    };
    writeMovie(QStringLiteral("talk.mov"), 195, true, [](int, std::vector<std::uint8_t>& rgba) {
        for (std::size_t i = 0; i < rgba.size(); i += 4)
            rgba[i] = rgba[i + 1] = rgba[i + 2] = 128, rgba[i + 3] = 255;
    });
    writeMovie(QStringLiteral("scenes.mov"), 90, false, [](int f, std::vector<std::uint8_t>& rgba) {
        for (int y = 0; y < 90; ++y)
            for (int x = 0; x < 160; ++x) {
                std::uint8_t* p = &rgba[std::size_t((y * 160 + x) * 4)];
                const int shot = f / 30, t = f % 30;
                if (shot == 0) {
                    const bool bar = std::abs(x - t * 5) < 8;
                    p[0] = bar ? 250 : 200; p[1] = bar ? 220 : 30; p[2] = 30;
                } else if (shot == 1) {
                    p[0] = 20; p[1] = std::uint8_t((x + t * 2) % 160); p[2] = 200;
                } else {
                    const bool disc = std::hypot(x - 80, y - 45) < 10 + t;
                    p[0] = disc ? 240 : 30; p[1] = disc ? 240 : 170; p[2] = disc ? 240 : 60;
                }
                p[3] = 255;
            }
    });
}

// Gives tests the sample-media folder and a scratch folder as context properties.
class Setup : public QObject {
    Q_OBJECT
public:
    QTemporaryDir scratch;
    TestFiles files{scratch.path()};

public slots:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("scratchDir"), QDir(scratch.path()).canonicalPath());
        engine->rootContext()->setContextProperty(QStringLiteral("testFiles"), &files);
        engine->rootContext()->setContextProperty(QStringLiteral("samplesDir"), QStringLiteral(TVE_SAMPLES_DIR));
        engine->rootContext()->setContextProperty(QStringLiteral("tempDir"), QDir::tempPath());
        engine->rootContext()->setContextProperty(QStringLiteral("recordingsDir"), qEnvironmentVariable("TVE_RECORDING_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("fixturesDir"), QStringLiteral(TVE_QML_TESTS_DIR "/fixtures"));
        engine->rootContext()->setContextProperty(QStringLiteral("stillsDir"), qEnvironmentVariable("TVE_TEST_STILLS_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("analysisMediaDir"), qEnvironmentVariable("TVE_TEST_ANALYSIS_DIR"));
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
    QTemporaryDir analysis; // measured media
    qputenv("TVE_ANALYSIS_DIR", analysis.path().toLocal8Bit());
    QTemporaryDir analysisMedia; // media to measure
    writeAnalysisMedia(analysisMedia.path());
    qputenv("TVE_TEST_ANALYSIS_DIR", analysisMedia.path().toLocal8Bit());
    QTemporaryDir keymap; // shortcuts changed by tests
    qputenv("TVE_KEYMAP_FILE", (keymap.path() + QStringLiteral("/keymap.json")).toLocal8Bit());
    QTemporaryDir colorPresets; // colour presets saved by tests
    qputenv("TVE_COLOR_PRESET_DIR", colorPresets.path().toLocal8Bit());
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "tve_qml_tests", TVE_QML_TESTS_DIR, &setup);
}

#include "main.moc"

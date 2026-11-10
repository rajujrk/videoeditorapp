// Runs the tst_*.qml files in this folder against the app's static QML modules.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QImage>
#include <QTemporaryDir>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

#include "Language.h"
#include "ThumbnailProvider.h"
#include "MediaImports.h"
#include "TemplateLibrary.h"
#include "TransitionPreviewProvider.h"
#include "EffectPreviewProvider.h"
#include "OverlayTransitions.h"
#include "Stickers.h"
#include "LookLibrary.h"
#include "TitleRenderer.h"
#include "decode/AudioDecoder.h"
#include "audio/SoundPacks.h"
#include "encode/MediaWriter.h"
#include "packs/Packs.h"

#include <QDirIterator>
#include <QTcpServer>
#include <QTcpSocket>

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
    // Sets a test hook's environment variable (TVE_CONSENT_RECORDING…) for what follows.
    Q_INVOKABLE void setEnv(const QString& name, const QString& value) const
    {
        if (name.startsWith(QLatin1String("TVE_")))
            qputenv(name.toLocal8Bit().constData(), value.toLocal8Bit());
    }
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
//   long.mov             40 s in four 10 s shots, a disc moving across; loud from 22 to 30 s
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

    const auto writeMovie = [&](const QString& name, int frames, const std::function<float(double)>& soundAt,
                                const std::function<void(int, std::vector<std::uint8_t>&)>& draw) {
        const bool sound = bool(soundAt);
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
                    samples[i * 2] = samples[i * 2 + 1] = soundAt(f / 30.0 + double(i) / audio::kSampleRate);
                writer->writeAudio(samples.data(), int(samples.size() / 2));
            }
        }
        writer->finish();
    };
    writeMovie(QStringLiteral("talk.mov"), 195, talkAt, [](int, std::vector<std::uint8_t>& rgba) {
        for (std::size_t i = 0; i < rgba.size(); i += 4)
            rgba[i] = rgba[i + 1] = rgba[i + 2] = 128, rgba[i + 3] = 255;
    });
    writeMovie(QStringLiteral("scenes.mov"), 90, {}, [](int f, std::vector<std::uint8_t>& rgba) {
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
    // long.mov: 40 s in four shots (a new colour every 10 s) with a disc moving across; a
    // quiet hum, loud from 22 to 30 s.
    std::mt19937 hiss(7);
    std::uniform_real_distribution<float> spread(-1.0f, 1.0f);
    writeMovie(QStringLiteral("long.mov"), 1200, [&](double t) {
        const float hum = 0.02f * float(std::sin(2.0 * std::numbers::pi * 110.0 * t));
        return t >= 22.0 && t < 30.0 ? 0.5f * spread(hiss) : hum;
    }, [](int f, std::vector<std::uint8_t>& rgba) {
        const int shot = f / 300;
        const double cx = 20 + (f % 300) * 0.4;
        for (int y = 0; y < 90; ++y)
            for (int x = 0; x < 160; ++x) {
                std::uint8_t* p = &rgba[std::size_t((y * 160 + x) * 4)];
                const bool disc = std::hypot(x - cx, y - 45) < 12;
                const std::uint8_t base[4][3] = {{40, 90, 160}, {150, 60, 40}, {50, 140, 70}, {120, 110, 40}};
                p[0] = disc ? 245 : base[shot][0];
                p[1] = disc ? 245 : base[shot][1];
                p[2] = disc ? 245 : base[shot][2];
                p[3] = 255;
            }
    });
}

// Gives tests the sample-media folder and a scratch folder as context properties.
// For the Audio tab: music/ (two tones, as My music) and packs/good (two sound effects) and
// packs/tampered (one of its two files changed after the manifest was written).
void writeSoundFixtures(const QString& dir)
{
    using namespace tve;
    const auto tone = [](double hz, double seconds) {
        std::vector<float> samples(std::size_t(seconds * 48000) * 2);
        for (std::size_t f = 0; f < samples.size() / 2; ++f)
            samples[f * 2] = samples[f * 2 + 1] = 0.2f * float(std::sin(2.0 * std::numbers::pi * hz * double(f) / 48000.0));
        return samples;
    };
    writeSoundFile((dir + QStringLiteral("/music/Evening tune.wav")).toStdString(), tone(330, 3));
    writeSoundFile((dir + QStringLiteral("/music/albums/Morning tune.wav")).toStdString(), tone(440, 2));
    std::vector<SoundRecipe> recipes{*findSound("tve.sfx.pop"), *findSound("tve.sfx.ding")};
    writeSoundPack((dir + QStringLiteral("/packs/good")).toStdString(), "test.good", "Test pack", recipes);
    writeSoundPack((dir + QStringLiteral("/packs/tampered")).toStdString(), "test.tampered", "Tampered pack", recipes);
    QFile changed(dir + QStringLiteral("/packs/tampered/sfx/ding.wav"));
    if (changed.open(QIODevice::Append))
        changed.write("x");
}

// Stand-ins for the Photos library, the Music library and a device (their folders are albums,
// playlists and devices), from the stills and analysis media already written.
void writeImportFixtures(const QString& dir, const QString& stills, const QString& media)
{
    const auto put = [&](const QString& from, const QString& to) {
        QDir().mkpath(QFileInfo(dir + to).absolutePath());
        QFile::copy(from, dir + to);
    };
    put(stills + QStringLiteral("/halves.png"), QStringLiteral("/photos/Holiday/IMG_0001.png"));
    put(media + QStringLiteral("/scenes.mov"), QStringLiteral("/photos/Holiday/IMG_0002.mov"));
    put(stills + QStringLiteral("/disc.png"), QStringLiteral("/photos/Pets/IMG_0003.png"));
    put(media + QStringLiteral("/hum.wav"), QStringLiteral("/music/Road trip/Hum along.wav"));
    put(media + QStringLiteral("/beat.wav"), QStringLiteral("/music/Road trip/On the beat.wav"));
    put(media + QStringLiteral("/scenes.mov"), QStringLiteral("/devices/Test camera/CLIP0001.mov"));
    put(stills + QStringLiteral("/halves.jpg"), QStringLiteral("/devices/Test camera/DSC0002.jpg"));
}

// Subtitles for the AI tests: talk.srt (three captions) and long.srt (130, past one request).
void writeCaptionFixtures(const QString& dir)
{
    const auto time = [](int ms) {
        return QStringLiteral("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QLatin1Char('0')).arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
            .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
    };
    const auto write = [&](const QString& name, const QStringList& lines) {
        QFile file(dir + QLatin1Char('/') + name);
        if (!file.open(QIODevice::WriteOnly))
            return;
        QString srt;
        for (int i = 0; i < lines.size(); ++i)
            srt += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(i + 1).arg(time(500 + i * 2000)).arg(time(2000 + i * 2000)).arg(lines[i]);
        file.write(srt.toUtf8());
    };
    write(QStringLiteral("talk.srt"), {QStringLiteral("We walked along the beach at dawn."),
                                       QStringLiteral("The tide had left shells everywhere."),
                                       QStringLiteral("By noon we were home again.")});
    QStringList many;
    for (int i = 1; i <= 130; ++i)
        many << QStringLiteral("Line number %1").arg(i);
    write(QStringLiteral("long.srt"), many);
}

// A pack store's site for the tests: a static web server on this machine serving a folder,
// packs published into it with the real publishing code, and the catalogue signed with a key
// made for this run (in memory only; its public key is the one the store trusts).
// TVE_STORE_URL points the store at it; TVE_PACKS_DIR and TVE_STORE_SETTINGS_FILE are scratch.
class PackSite : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString url READ url CONSTANT)
    Q_PROPERTY(QString packsDir READ packsDir CONSTANT)
    Q_PROPERTY(int requests READ requests)
    // The folder the tests run in (a failed install must leave it alone).
    Q_PROPERTY(QString currentDir READ currentDir CONSTANT)
public:
    QTemporaryDir root;
    QTemporaryDir staging;
    tve::SigningKey key;
    tve::SigningKey otherKey;
    QTcpServer server;
    int served = 0;
    qint64 sequence = 0;

    PackSite()
    {
        key = tve::makeSigningKey().value_or(tve::SigningKey{});
        otherKey = tve::makeSigningKey().value_or(tve::SigningKey{});
    }
    // The keys file the store trusts (the site's key only).
    void writeTrustedKeys(const QString& path) const
    {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(QStringLiteral("{\"keys\": [{\"id\": \"test\", \"publicKey\": \"%1\"}]}")
                           .arg(QString::fromLatin1(QByteArray::fromStdString(key.publicKey).toBase64())).toUtf8());
    }
    void start()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    const QByteArray request = socket->peek(8192);
                    if (!request.contains("\r\n\r\n"))
                        return;
                    socket->readAll();
                    ++served;
                    const QList<QByteArray> line = request.left(request.indexOf("\r\n")).split(' ');
                    const QString path = line.size() > 1 ? QUrl::fromPercentEncoding(line[1]) : QString();
                    QFile file(root.path() + path);
                    QByteArray response;
                    if (!path.contains(QStringLiteral("..")) && file.open(QIODevice::ReadOnly)) {
                        const QByteArray body = file.readAll();
                        response = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size())
                                   + "\r\nConnection: close\r\n\r\n" + body;
                    } else {
                        response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    }
                    socket->write(response);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/catalogue.json").arg(server.serverPort()); }
    QString packsDir() const { return qEnvironmentVariable("TVE_PACKS_DIR"); }
    int requests() const { return served; }
    QString currentDir() const { return QDir::currentPath(); }

    // Copies a file or folder (also from resources) to `target`.
    static bool copy(const QString& source, const QString& target)
    {
        if (QFileInfo(source).isDir()) {
            QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                const QString to = target + file.mid(source.size());
                QDir().mkpath(QFileInfo(to).absolutePath());
                if (!QFile::copy(file, to))
                    return false;
            }
            return true;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        return QFile::copy(source, target);
    }

public slots:
    // Publishes a pack: files maps a path in the pack to a file or folder to copy there.
    // "" if it went, or why not.
    QString publish(const QVariantMap& files, const QString& id, int version, const QString& title,
                    const QString& description = {}, const QString& preview = {})
    {
        if (id.isEmpty() || id.contains(QLatin1Char('/')) || !staging.isValid())
            return QStringLiteral("not a pack id");
        const QString source = staging.path() + QStringLiteral("/%1-%2").arg(id).arg(version);
        QDir(source).removeRecursively();
        for (auto it = files.begin(); it != files.end(); ++it)
            if (!copy(it.value().toString(), source + QLatin1Char('/') + it.key()))
                return QStringLiteral("can't copy ") + it.value().toString();
        tve::PackManifest meta{.id = id.toStdString(), .version = version, .title = title.toStdString(),
                               .description = description.toStdString(), .author = "Test author", .license = "Test licence"};
        const auto error = tve::publishPack(source.toStdString(), root.path().toStdString(), meta, preview.toStdString());
        return error ? QString::fromStdString(*error) : QString();
    }
    // A file with this text to publish, its path.
    QString text(const QString& name, const QString& content)
    {
        const QString path = staging.path() + QStringLiteral("/files/") + name;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(content.toUtf8());
        return path;
    }
    // Writes and signs the catalogue (with the trusted key, or another one); sequence < 0: the next.
    void sign(int nextSequence = -1, bool trustedKey = true)
    {
        sequence = nextSequence >= 0 ? nextSequence : sequence + 1;
        const std::string json = tve::catalogueJson(tve::siteCatalogue(root.path().toStdString(), sequence, "2026-11-01"));
        QFile catalogue(root.path() + QStringLiteral("/catalogue.json"));
        catalogue.open(QIODevice::WriteOnly);
        catalogue.write(QByteArray::fromStdString(json));
        catalogue.close();
        QFile signature(root.path() + QStringLiteral("/catalogue.json.sig"));
        signature.open(QIODevice::WriteOnly);
        signature.write(QByteArray::fromStdString(tve::signData(json, trustedKey ? key.privateKey : otherKey.privateKey)));
    }
    // Changes a published file (the catalogue still says what it should be).
    void tamper(const QString& path)
    {
        QFile file(root.path() + QLatin1Char('/') + path);
        // (Files copied from resources are read-only.)
        file.setPermissions(file.permissions() | QFileDevice::WriteOwner);
        if (file.open(QIODevice::Append))
            file.write("changed");
    }
    // An empty site, nothing installed, nothing seen.
    void reset()
    {
        if (!root.isValid())
            return;
        for (const QString& name : QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
            QFileInfo entry(root.path() + QLatin1Char('/') + name);
            entry.isDir() ? (void)QDir(entry.filePath()).removeRecursively() : (void)QFile::remove(entry.filePath());
        }
        // (Only the scratch packs folder: never an empty path, the current folder.)
        if (packsDir().endsWith(QLatin1String("/Packs")) && QDir::isAbsolutePath(packsDir()))
            QDir(packsDir()).removeRecursively();
        QFile::remove(qEnvironmentVariable("TVE_STORE_SETTINGS_FILE"));
        sequence = 0;
    }
};

class Setup : public QObject {
    Q_OBJECT
public:
    QTemporaryDir scratch;
    TestFiles files{scratch.path()};
    PackSite* packSite = nullptr;

public slots:
    void applicationAvailable()
    {
        packSite = new PackSite;
        packSite->start();
        qputenv("TVE_STORE_URL", packSite->url().toLocal8Bit());
        packSite->writeTrustedKeys(qEnvironmentVariable("TVE_STORE_TRUSTED_KEYS"));
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        Language::create(engine, nullptr); // (retranslated when a test changes the language)
        Language::instance()->apply();
        engine->rootContext()->setContextProperty(QStringLiteral("scratchDir"), QDir(scratch.path()).canonicalPath());
        engine->rootContext()->setContextProperty(QStringLiteral("testFiles"), &files);
        engine->rootContext()->setContextProperty(QStringLiteral("packSite"), packSite);
        engine->rootContext()->setContextProperty(QStringLiteral("samplesDir"), QStringLiteral(TVE_SAMPLES_DIR));
        engine->rootContext()->setContextProperty(QStringLiteral("tempDir"), QDir::tempPath());
        engine->rootContext()->setContextProperty(QStringLiteral("recordingsDir"), qEnvironmentVariable("TVE_RECORDING_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("fixturesDir"), QStringLiteral(TVE_QML_TESTS_DIR "/fixtures"));
        engine->rootContext()->setContextProperty(QStringLiteral("stillsDir"), qEnvironmentVariable("TVE_TEST_STILLS_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("analysisMediaDir"), qEnvironmentVariable("TVE_TEST_ANALYSIS_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("captionFixturesDir"), qEnvironmentVariable("TVE_TEST_CAPTIONS_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("templatesDir"), qEnvironmentVariable("TVE_TEMPLATE_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("templateCacheDir"), qEnvironmentVariable("TVE_TEMPLATE_CACHE_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("voicesDir"), qEnvironmentVariable("TVE_VOICE_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("consentDir"), qEnvironmentVariable("TVE_CONSENT_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("languageFile"), qEnvironmentVariable("TVE_LANGUAGE_FILE"));
        engine->rootContext()->setContextProperty(QStringLiteral("keyStoreDir"), qEnvironmentVariable("TVE_KEY_STORE"));
        engine->rootContext()->setContextProperty(QStringLiteral("importFixturesDir"), qEnvironmentVariable("TVE_TEST_IMPORTS_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("importsDir"), qEnvironmentVariable("TVE_IMPORT_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("soundFixturesDir"), qEnvironmentVariable("TVE_TEST_SOUNDS_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("soundPacksDir"), qEnvironmentVariable("TVE_SOUND_PACK_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("soundCacheDir"), qEnvironmentVariable("TVE_SOUND_CACHE_DIR"));
        engine->rootContext()->setContextProperty(QStringLiteral("speechModelAvailable"),
                                                  QFileInfo::exists(QStringLiteral(TVE_SAMPLES_DIR "/models/ggml-base.en-q5_1.bin")));
        engine->addImageProvider(QString::fromLatin1(ThumbnailProvider::kId), new ThumbnailProvider);
        engine->addImageProvider(QString::fromLatin1(ImportThumbnailProvider::kId), new ImportThumbnailProvider);
        engine->addImageProvider(QString::fromLatin1(TemplatePreviewProvider::kId), new TemplatePreviewProvider);
        engine->addImageProvider(QString::fromLatin1(TransitionPreviewProvider::kId), new TransitionPreviewProvider);
        engine->addImageProvider(QString::fromLatin1(EffectPreviewProvider::kId), new EffectPreviewProvider);
        engine->addImageProvider(QString::fromLatin1(StickerPreviewProvider::kId), new StickerPreviewProvider);
        engine->addImageProvider(QString::fromLatin1(TitlePreviewProvider::kId), new TitlePreviewProvider);
        engine->addImageProvider(QString::fromLatin1(LookPreviewProvider::kId), new LookPreviewProvider);
        engine->addImageProvider(QString::fromLatin1(OverlayPreviewProvider::kId), new OverlayPreviewProvider);
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
    // Screen and camera recordings come from test sources (no devices, no permission prompts).
    qputenv("TVE_SCREEN_SOURCE", "test");
    qputenv("TVE_CAMERA_SOURCE", "test");
    QTemporaryDir recordings;
    qputenv("TVE_RECORDING_DIR", recordings.path().toLocal8Bit());
    QTemporaryDir stills;
    writeStills(stills.path());
    qputenv("TVE_TEST_STILLS_DIR", stills.path().toLocal8Bit());
    QTemporaryDir analysis; // measured media
    qputenv("TVE_ANALYSIS_DIR", analysis.path().toLocal8Bit());
    QTemporaryDir enhanced; // cleaned speech
    qputenv("TVE_ENHANCED_DIR", enhanced.path().toLocal8Bit());
    QTemporaryDir processed; // processed pictures
    qputenv("TVE_PROCESSED_DIR", processed.path().toLocal8Bit());
    // Speech models: an empty folder to download into, from a local "server" folder holding
    // the real models (if tools/fetch_models.sh fetched them) and a corrupt one.
    QTemporaryDir models;
    QTemporaryDir modelSource;
    qputenv("TVE_MODEL_DIR", models.path().toLocal8Bit());
    qputenv("TVE_MODEL_BASE_URL", QUrl::fromLocalFile(modelSource.path() + QLatin1Char('/')).toString().toLocal8Bit());
    const QString realModel = QStringLiteral(TVE_SAMPLES_DIR "/models/ggml-base.en-q5_1.bin");
    if (QFileInfo::exists(realModel))
        QFile::link(realModel, modelSource.path() + QStringLiteral("/ggml-base.en-q5_1.bin"));
    const QString enhanceModel = QStringLiteral(TVE_SAMPLES_DIR "/models/DeepFilterNet3_onnx.tar.gz");
    if (QFileInfo::exists(enhanceModel))
        QFile::link(enhanceModel, modelSource.path() + QStringLiteral("/DeepFilterNet3_onnx.tar.gz"));
    {
        QFile corrupt(modelSource.path() + QStringLiteral("/ggml-small.en-q5_1.bin"));
        if (corrupt.open(QIODevice::WriteOnly))
            corrupt.write(QByteArray(1000, 'x'));
    }
    QTemporaryDir analysisMedia; // media to measure
    writeAnalysisMedia(analysisMedia.path());
    qputenv("TVE_TEST_ANALYSIS_DIR", analysisMedia.path().toLocal8Bit());
    QTemporaryDir keymap; // shortcuts changed by tests
    qputenv("TVE_KEYMAP_FILE", (keymap.path() + QStringLiteral("/keymap.json")).toLocal8Bit());
    QTemporaryDir colorPresets; // colour presets saved by tests
    qputenv("TVE_COLOR_PRESET_DIR", colorPresets.path().toLocal8Bit());
    // The sound library: its render cache, installed packs and chosen music folders.
    QTemporaryDir soundCache;
    qputenv("TVE_SOUND_CACHE_DIR", soundCache.path().toLocal8Bit());
    QTemporaryDir soundPacks;
    qputenv("TVE_SOUND_PACK_DIR", soundPacks.path().toLocal8Bit());
    QTemporaryDir musicFolders;
    qputenv("TVE_MUSIC_FOLDERS_FILE", (musicFolders.path() + QStringLiteral("/music-folders.json")).toLocal8Bit());
    QTemporaryDir soundFixtures;
    writeSoundFixtures(soundFixtures.path());
    qputenv("TVE_TEST_SOUNDS_DIR", QDir(soundFixtures.path()).canonicalPath().toLocal8Bit());
    // Imports from Photos, Music and devices: test folders in their place, fetched into a
    // scratch folder.
    QTemporaryDir importFixtures;
    const QString importRoot = QDir(importFixtures.path()).canonicalPath();
    writeImportFixtures(importRoot, stills.path(), analysisMedia.path());
    qputenv("TVE_TEST_IMPORTS_DIR", importRoot.toLocal8Bit());
    qputenv("TVE_PHOTOS_SOURCE", (importRoot + QStringLiteral("/photos")).toLocal8Bit());
    qputenv("TVE_MUSIC_SOURCE", (importRoot + QStringLiteral("/music")).toLocal8Bit());
    qputenv("TVE_DEVICE_SOURCE", (importRoot + QStringLiteral("/devices")).toLocal8Bit());
    QTemporaryDir importsInto;
    qputenv("TVE_IMPORT_DIR", QDir(importsInto.path()).canonicalPath().toLocal8Bit());
    // AI: the mock Claude and ElevenLabs on this machine; keys, settings and voices in
    // scratch folders (never the real Keychain).
    qputenv("TVE_CLOUD_MOCK", "1");
    QTemporaryDir keyStore;
    qputenv("TVE_KEY_STORE", keyStore.path().toLocal8Bit());
    QTemporaryDir cloudSettings;
    qputenv("TVE_CLOUD_SETTINGS_FILE", (cloudSettings.path() + QStringLiteral("/cloud-settings.json")).toLocal8Bit());
    QTemporaryDir voices;
    qputenv("TVE_VOICE_DIR", QDir(voices.path()).canonicalPath().toLocal8Bit());
    // Voice cloning: consent records in a scratch folder; the code words fixed.
    // The language setting in a scratch file (the app's own isn't touched).
    QTemporaryDir languageSettings;
    qputenv("TVE_LANGUAGE_FILE", (languageSettings.path() + QStringLiteral("/language.json")).toLocal8Bit());
    {
        // English, whatever this Mac speaks (tst_language switches it).
        QFile file(languageSettings.path() + QStringLiteral("/language.json"));
        if (file.open(QIODevice::WriteOnly))
            file.write(R"({"language": "en"})");
    }
    QTemporaryDir consents;
    qputenv("TVE_CONSENT_DIR", QDir(consents.path()).canonicalPath().toLocal8Bit());
    qputenv("TVE_CONSENT_SEED", "7");
    // Templates: the person's own, the cache of placeholders and music, the favourites.
    QTemporaryDir templates;
    qputenv("TVE_TEMPLATE_DIR", QDir(templates.path()).canonicalPath().toLocal8Bit());
    QTemporaryDir templateCache;
    qputenv("TVE_TEMPLATE_CACHE_DIR", QDir(templateCache.path()).canonicalPath().toLocal8Bit());
    QTemporaryDir templateFavorites;
    qputenv("TVE_TEMPLATE_FAVORITES_FILE", (templateFavorites.path() + QStringLiteral("/template-favorites.json")).toLocal8Bit());
    QTemporaryDir overlayCache;
    qputenv("TVE_OVERLAY_CACHE_DIR", QDir(overlayCache.path()).canonicalPath().toLocal8Bit());
    QTemporaryDir generated;
    qputenv("TVE_GENERATED_DIR", QDir(generated.path()).canonicalPath().toLocal8Bit());
    // The pack store: its site (started with the app), installed packs, settings, trusted keys.
    QTemporaryDir store;
    qputenv("TVE_PACKS_DIR", (QDir(store.path()).canonicalPath() + QStringLiteral("/Packs")).toLocal8Bit());
    qputenv("TVE_STORE_SETTINGS_FILE", (store.path() + QStringLiteral("/store.json")).toLocal8Bit());
    qputenv("TVE_STORE_TRUSTED_KEYS", (store.path() + QStringLiteral("/trusted-keys.json")).toLocal8Bit());
    // Audio plug-ins: the test VST3 effect (TVE Test Gain) found by the real scanner, with a
    // scratch scan cache; Apple's own Audio Units are on every Mac.
    qputenv("TVE_VST3_PATH", TVE_TEST_VST3_DIR);
    qputenv("TVE_PLUGIN_SCANNER", TVE_PLUGIN_SCANNER_PATH);
    qputenv("TVE_OFX_PATH", TVE_TEST_OFX_DIR);
    QTemporaryDir pluginCache;
    qputenv("TVE_PLUGIN_CACHE_FILE", (pluginCache.path() + QStringLiteral("/plugin-cache.json")).toLocal8Bit());
    QTemporaryDir captionFixtures;
    writeCaptionFixtures(captionFixtures.path());
    qputenv("TVE_TEST_CAPTIONS_DIR", captionFixtures.path().toLocal8Bit());
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "tve_qml_tests", TVE_QML_TESTS_DIR, &setup);
}

#include "main.moc"

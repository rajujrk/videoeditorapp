#include "ProjectController.h"
#include "ThumbnailProvider.h"
#include "TransitionPreviewProvider.h"
#include "EffectPreviewProvider.h"
#include "Stickers.h"
#include "LookLibrary.h"
#include "TitleRenderer.h"
#include "VideoView.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>

Q_IMPORT_QML_PLUGIN(TechieVideoEditor_ThemePlugin)
Q_IMPORT_QML_PLUGIN(TechieVideoEditor_PlayerPlugin)
Q_IMPORT_QML_PLUGIN(TechieVideoEditor_ShellPlugin)

namespace {

// Prints one line per stats sample and a summary on exit, for unattended benchmarking.
void attachStatsPrinter(VideoView* view, QCoreApplication& app)
{
    struct Summary {
        int samples = 0;
        double presentSum = 0;
        double presentMin = 1e9;
    };
    auto summary = std::make_shared<Summary>();

    QObject::connect(view, &VideoView::sourceChanged, view, [view] {
        if (!view->errorString().isEmpty())
            std::printf("error: %s\n", qPrintable(view->errorString()));
        else
            std::printf("stream: %s\n", qPrintable(view->streamDescription()));
        std::fflush(stdout);
    });
    QObject::connect(view, &VideoView::statsChanged, view, [view, summary] {
        if (view->presentFps() <= 0 && view->decodeFps() <= 0)
            return;
        std::printf("decode_fps=%.1f present_fps=%.1f skipped=%lld decoder=%s\n", view->decodeFps(),
                    view->presentFps(), static_cast<long long>(view->skippedFrames()),
                    view->hardwareAccelerated() ? "videotoolbox" : "software");
        std::fflush(stdout);
        ++summary->samples;
        if (summary->samples > 2) { // ignore warm-up seconds
            summary->presentSum += view->presentFps();
            summary->presentMin = std::min(summary->presentMin, view->presentFps());
        }
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, view, [view, summary] {
        const int counted = summary->samples - 2;
        if (counted > 0) {
            std::printf("summary: avg_present_fps=%.2f min_present_fps=%.1f skipped_total=%lld samples=%d\n",
                        summary->presentSum / counted, summary->presentMin,
                        static_cast<long long>(view->skippedFrames()), counted);
            std::fflush(stdout);
        }
    });
}

} // namespace

int main(int argc, char* argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    // Basic is the one Controls style that takes all its colors from the palette, which
    // Main.qml fills from the theme tokens.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("TechieVideoEditor"));
    QGuiApplication::setOrganizationName(QStringLiteral("TechieVideoEditor"));
    QGuiApplication::setApplicationVersion(QStringLiteral(TVE_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("TechieVideoEditor"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption statsOption(QStringLiteral("stats"), QStringLiteral("Print playback statistics every second."));
    const QCommandLineOption quitOption(QStringLiteral("quit-after"), QStringLiteral("Quit after <seconds>."),
                                        QStringLiteral("seconds"));
    const QCommandLineOption effectsOption(
        QStringLiteral("effects"), QStringLiteral("Enable effects at their defaults: comma-separated ids, or \"all\"."),
        QStringLiteral("ids"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Save a screenshot of the window to <file>."),
                                              QStringLiteral("file"));
    const QCommandLineOption screenshotAtOption(QStringLiteral("screenshot-at"),
                                                QStringLiteral("When to take the screenshot (default 3)."),
                                                QStringLiteral("seconds"), QStringLiteral("3"));
    const QCommandLineOption screenOption(QStringLiteral("screen"),
                                          QStringLiteral("Start on \"home\" or \"editor\" (default: editor when a "
                                                         "file is given, else home)."),
                                          QStringLiteral("name"));
    const QCommandLineOption themeOption(QStringLiteral("theme"), QStringLiteral("\"dark\" (default) or \"light\"."),
                                         QStringLiteral("name"), QStringLiteral("dark"));
    const QCommandLineOption playOption(QStringLiteral("play"), QStringLiteral("Start playing after loading."));
    const QCommandLineOption seekOption(QStringLiteral("seek"), QStringLiteral("Move the playhead to <seconds>."),
                                        QStringLiteral("seconds"));
    const QCommandLineOption exportOption(QStringLiteral("export"),
                                          QStringLiteral("Export the project to <file>, then quit."),
                                          QStringLiteral("file"));
    const QCommandLineOption exportSizeOption(QStringLiteral("export-size"),
                                              QStringLiteral("Export size, e.g. 1280x720 (default: the canvas)."),
                                              QStringLiteral("WxH"));
    const QCommandLineOption exportCodecOption(QStringLiteral("export-codec"), QStringLiteral("h264 (default) or hevc."),
                                               QStringLiteral("codec"), QStringLiteral("h264"));
    const QCommandLineOption exportFormatOption(
        QStringLiteral("export-format"),
        QStringLiteral("mp4-h264 (default), mp4-hevc, mov-h264, mov-prores-proxy|lt|422|hq|4444, gif, m4a, wav-16, "
                       "wav-24 or flac."),
        QStringLiteral("format"));
    const QCommandLineOption exportPresetOption(QStringLiteral("export-preset"),
                                                QStringLiteral("An export preset, e.g. web-1080, vertical, master, gif."),
                                                QStringLiteral("id"));
    const QCommandLineOption makeProxiesOption(QStringLiteral("make-proxies"),
                                               QStringLiteral("Make proxies of the given media, then quit."));
    const QCommandLineOption noProxiesOption(QStringLiteral("no-proxies"),
                                             QStringLiteral("Play the original media, not proxies."));
    parser.addOptions({statsOption, quitOption, effectsOption, screenshotOption, screenshotAtOption, screenOption,
                       themeOption, playOption, seekOption, exportOption, exportSizeOption, exportCodecOption,
                       exportFormatOption, exportPresetOption, makeProxiesOption, noProxiesOption});
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("A .tve project to open, or media files to add to a new project."),
                                 QStringLiteral("[files...]"));
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QString::fromLatin1(ThumbnailProvider::kId), new ThumbnailProvider);
    engine.addImageProvider(QString::fromLatin1(TransitionPreviewProvider::kId), new TransitionPreviewProvider);
    engine.addImageProvider(QString::fromLatin1(EffectPreviewProvider::kId), new EffectPreviewProvider);
    engine.addImageProvider(QString::fromLatin1(StickerPreviewProvider::kId), new StickerPreviewProvider);
    engine.addImageProvider(QString::fromLatin1(TitlePreviewProvider::kId), new TitlePreviewProvider);
    engine.addImageProvider(QString::fromLatin1(LookPreviewProvider::kId), new LookPreviewProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    if (auto* theme = engine.singletonInstance<QObject*>("TechieVideoEditor.Theme", "Theme"))
        theme->setProperty("dark", parser.value(themeOption) != QLatin1String("light"));
    engine.loadFromModule("TechieVideoEditor.App", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    const bool hasFile = !parser.positionalArguments().isEmpty();
    const QString screen = parser.isSet(screenOption) ? parser.value(screenOption)
                                                      : QString::fromLatin1(hasFile ? "editor" : "home");
    engine.rootObjects().constFirst()->setProperty("screen", screen);

    auto* view = engine.rootObjects().constFirst()->findChild<VideoView*>(QStringLiteral("player"));
    if (view && parser.isSet(statsOption))
        attachStatsPrinter(view, app);
    if (view && parser.isSet(effectsOption)) {
        const QString ids = parser.value(effectsOption);
        for (const QVariant& effect : view->availableEffects()) {
            const QString id = effect.toMap().value(QStringLiteral("id")).toString();
            if (ids == QLatin1String("all") || ids.split(QLatin1Char(',')).contains(id))
                view->setEffectEnabled(id, true);
        }
    }
    // Files on the command line: a .tve project is opened; media files are imported and
    // appended to the main track in order.
    auto* project = engine.rootObjects().constFirst()->findChild<ProjectController*>(QStringLiteral("project"));
    if (project) {
        // Scripted runs (measurements, screenshots, exports) leave no unsaved work behind.
        // …nor start making proxies on their own (that would skew measurements).
        if (parser.isSet(statsOption) || parser.isSet(quitOption) || parser.isSet(screenshotOption)
            || parser.isSet(exportOption) || parser.isSet(makeProxiesOption)) {
            project->setAutosaveEnabled(false);
            project->setAutoProxies(false);
        }
        if (parser.isSet(noProxiesOption))
            project->setUseProxies(false);
        for (const QString& argument : parser.positionalArguments()) {
            const QUrl url = QUrl::fromLocalFile(QFileInfo(argument).absoluteFilePath());
            if (argument.endsWith(QLatin1String(".tve")))
                project->open(url);
            else
                project->importAndAppend(url);
        }
        if (parser.isSet(seekOption))
            project->seek(parser.value(seekOption).toDouble());
        if (parser.isSet(makeProxiesOption)) {
            const auto started = std::chrono::steady_clock::now();
            for (const QVariant& media : project->media())
                project->createProxy(media.toMap().value(QStringLiteral("id")).toDouble());
            // Quit once nothing is queued or being made.
            QObject::connect(project, &ProjectController::proxiesChanged, &app, [project, started] {
                const QVariantMap states = project->proxyStates();
                for (const QVariant& state : states) {
                    const QString name = state.toMap().value(QStringLiteral("state")).toString();
                    if (name == QLatin1String("queued") || name == QLatin1String("making"))
                        return;
                }
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                std::printf("proxies: done (%.1f s) %s\n", seconds,
                            qPrintable(QStringList(states.keys()).join(QLatin1Char(' '))));
                std::fflush(stdout);
                QCoreApplication::exit(0);
            });
        }
        if (parser.isSet(exportOption)) {
            QVariantMap options{{QStringLiteral("codec"), parser.value(exportCodecOption)}};
            if (parser.isSet(exportFormatOption))
                options[QStringLiteral("format")] = parser.value(exportFormatOption);
            if (parser.isSet(exportPresetOption))
                options[QStringLiteral("preset")] = parser.value(exportPresetOption);
            const QStringList size = parser.value(exportSizeOption).split(QLatin1Char('x'));
            if (size.size() == 2) {
                options[QStringLiteral("width")] = size[0].toInt();
                options[QStringLiteral("height")] = size[1].toInt();
            }
            const auto started = std::chrono::steady_clock::now();
            QObject::connect(project, &ProjectController::exportFinished, &app, [started](bool ok, const QString& message) {
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                std::printf("export: %s %s (%.1f s)\n", ok ? "done" : "FAILED", qPrintable(message), seconds);
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 1);
            });
            const QUrl file = QUrl::fromLocalFile(QFileInfo(parser.value(exportOption)).absoluteFilePath());
            if (!project->exportVideo(file, options)) {
                std::printf("export: FAILED %s\n", qPrintable(project->lastError()));
                return 1;
            }
        }
        if (parser.isSet(playOption) || (hasFile && parser.isSet(statsOption)))
            project->play();
        if (parser.isSet(statsOption)) {
            const QVariantMap audio = project->audioStats();
            std::printf("audio: device=\"%s\" running=%d latency=%.1f ms\n", qPrintable(audio[QStringLiteral("device")].toString()),
                        audio[QStringLiteral("running")].toBool(), audio[QStringLiteral("latencyMs")].toDouble());
            std::fflush(stdout);
            QObject::connect(&app, &QCoreApplication::aboutToQuit, project, [project] {
                std::printf("audio: underruns=%lld\n", project->audioStats()[QStringLiteral("underruns")].toLongLong());
                std::fflush(stdout);
            });
        }
    }

    if (window && parser.isSet(screenshotOption)) {
        const QString path = QFileInfo(parser.value(screenshotOption)).absoluteFilePath();
        const double seconds = parser.value(screenshotAtOption).toDouble();
        QTimer::singleShot(int(seconds * 1000), window, [window, path] {
            const bool saved = window->grabWindow().save(path);
            std::printf("screenshot: %s %s\n", saved ? "saved" : "FAILED", qPrintable(path));
            std::fflush(stdout);
        });
    }

    if (parser.isSet(quitOption)) {
        bool ok = false;
        const double seconds = parser.value(quitOption).toDouble(&ok);
        if (ok && seconds > 0)
            QTimer::singleShot(int(seconds * 1000), &app, &QCoreApplication::quit);
    }

    return app.exec();
}

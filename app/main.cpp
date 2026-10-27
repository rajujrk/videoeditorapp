#include "ProjectController.h"
#include "ThumbnailProvider.h"
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
    parser.addOptions({statsOption, quitOption, effectsOption, screenshotOption, screenshotAtOption, screenOption,
                       themeOption, playOption, seekOption});
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("A .tve project to open, or media files to add to a new project."),
                                 QStringLiteral("[files...]"));
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QString::fromLatin1(ThumbnailProvider::kId), new ThumbnailProvider);
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
        for (const QString& argument : parser.positionalArguments()) {
            const QUrl url = QUrl::fromLocalFile(QFileInfo(argument).absoluteFilePath());
            if (argument.endsWith(QLatin1String(".tve")))
                project->open(url);
            else
                project->importAndAppend(url);
        }
        if (parser.isSet(seekOption))
            project->seek(parser.value(seekOption).toDouble());
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

#include "VideoView.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <cstdio>
#include <memory>

Q_IMPORT_QML_PLUGIN(TechieVideoEditor_PlayerPlugin)

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

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("TechieVideoEditor"));
    QGuiApplication::setOrganizationName(QStringLiteral("TechieVideoEditor"));
    QGuiApplication::setApplicationVersion(QStringLiteral(TVE_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("TechieVideoEditor — Phase 0 playback spike"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption statsOption(QStringLiteral("stats"), QStringLiteral("Print playback statistics every second."));
    const QCommandLineOption quitOption(QStringLiteral("quit-after"), QStringLiteral("Quit after <seconds>."),
                                        QStringLiteral("seconds"));
    parser.addOptions({statsOption, quitOption});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Video file to play."), QStringLiteral("[file]"));
    parser.process(app);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("TechieVideoEditor.App", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    auto* view = engine.rootObjects().constFirst()->findChild<VideoView*>(QStringLiteral("player"));
    if (view && parser.isSet(statsOption))
        attachStatsPrinter(view, app);
    if (view && !parser.positionalArguments().isEmpty())
        view->setSource(QFileInfo(parser.positionalArguments().constFirst()).absoluteFilePath());

    if (parser.isSet(quitOption)) {
        bool ok = false;
        const double seconds = parser.value(quitOption).toDouble(&ok);
        if (ok && seconds > 0)
            QTimer::singleShot(int(seconds * 1000), &app, &QCoreApplication::quit);
    }

    return app.exec();
}

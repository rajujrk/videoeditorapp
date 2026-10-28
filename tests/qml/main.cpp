// Runs the tst_*.qml files in this folder against the app's static QML modules.
#include <QDir>
#include <QTemporaryDir>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

#include "ThumbnailProvider.h"
#include "TransitionPreviewProvider.h"
#include "LookLibrary.h"
#include "TitleRenderer.h"

Q_IMPORT_QML_PLUGIN(TechieVideoEditor_ThemePlugin)
Q_IMPORT_QML_PLUGIN(TechieVideoEditor_PlayerPlugin)
Q_IMPORT_QML_PLUGIN(TechieVideoEditor_ShellPlugin)

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
        engine->addImageProvider(QString::fromLatin1(ThumbnailProvider::kId), new ThumbnailProvider);
        engine->addImageProvider(QString::fromLatin1(TransitionPreviewProvider::kId), new TransitionPreviewProvider);
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
    QTemporaryDir colorPresets; // colour presets saved by tests
    qputenv("TVE_COLOR_PRESET_DIR", colorPresets.path().toLocal8Bit());
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "tve_qml_tests", TVE_QML_TESTS_DIR, &setup);
}

#include "main.moc"

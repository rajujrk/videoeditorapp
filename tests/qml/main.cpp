// Runs the tst_*.qml files in this folder against the app's static QML modules.
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

Q_IMPORT_QML_PLUGIN(TechieVideoEditor_ThemePlugin)
Q_IMPORT_QML_PLUGIN(TechieVideoEditor_PlayerPlugin)
Q_IMPORT_QML_PLUGIN(TechieVideoEditor_ShellPlugin)

int main(int argc, char** argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    return quick_test_main(argc, argv, "tve_qml_tests", TVE_QML_TESTS_DIR);
}

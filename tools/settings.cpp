// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QTimer>
#include <QMessageBox>
#include <KCMultiDialog>
#include <KPluginMetaData>
#include <QStandardPaths>
#include <QPluginLoader>

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    app.setApplicationName("fluidtiles-settings");
    app.setApplicationDisplayName("Fluid Tiles Settings");
    app.setDesktopFileName("org.fluidtiles.Settings");
    const auto path=QStandardPaths::locate(QStandardPaths::GenericDataLocation,"kwin/scripts/fluidtiles-controller/metadata.json");
    const auto package=KPluginMetaData::fromJsonFile(path);
    const auto metadata=KPluginMetaData(QPluginLoader(package.value("X-KDE-ConfigModule")).fileName());
    if (!metadata.isValid()) {
        QMessageBox::critical(nullptr,"Fluid Tiles Settings","The Fluid Tiles settings module could not be found. Reinstall the package.");
        return 1;
    }
    KCMultiDialog dialog;
    dialog.addModule(metadata,{"fluidtiles-controller","KWin/Script"});
    dialog.setFaceType(KPageDialog::Plain);
    dialog.setWindowTitle("Fluid Tiles Settings");
    dialog.resize(780,760);
    const bool test=qEnvironmentVariableIntValue("FLUIDTILES_SETTINGS_TEST")==1 && app.arguments().size()==2;
    int result=0;
    if (test) QTimer::singleShot(std::clamp(qEnvironmentVariableIntValue("FLUIDTILES_SETTINGS_HOLD_MS"),800,30000),&dialog,[&] {
        if (!dialog.findChild<QDoubleSpinBox *>("kcfg_InnerGap") || !dialog.findChild<QPlainTextEdit *>("kcfg_IgnoreTitles")
            || !dialog.grab().save(app.arguments()[1])) result=1;
        dialog.reject();
    });
    dialog.exec();
    return result;
}

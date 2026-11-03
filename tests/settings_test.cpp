// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise KDE's actual generic script KCModule, not a replacement form.
#include "settings.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QComboBox>
#include <KColorButton>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <KCModule>
#include <KCModuleLoader>
#include <KPluginMetaData>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KShell>
#include <iostream>
#include <stdexcept>

static void require(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    if (qEnvironmentVariableIntValue("FLUIDTILES_SETTINGS_TEST")!=1) return 2;
    try {
        const auto args=app.arguments();
        auto config=KSharedConfig::openConfig("kwinrc");
        const bool wasEnabled=KConfigGroup(config,"Plugins").readEntry("fluidtiles-controllerEnabled",false);
        const auto metadata=KPluginMetaData::findPluginById("kwin/effects/configs","kcm_kwin4_genericscripted");
        require(metadata.isValid(),"KDE generic script config module not found");
        auto module=std::unique_ptr<KCModule>(KCModuleLoader::loadModule(metadata,nullptr,{"fluidtiles-controller","KWin/Script"}));
        require(bool(module),"KDE could not instantiate Fluid Tiles settings");
        module->load();
        auto form=module->widget();
        auto spin=[&](const char *name) {
            auto w=form->findChild<QDoubleSpinBox *>(name);require(w,"Missing numeric settings field");return w;
        };
        auto text=[&](const char *name) {
            auto w=form->findChild<QPlainTextEdit *>(name);require(w,"Missing ignore-pattern settings field");return w;
        };
        auto section=[&](const char *key) {
            auto w=form->findChild<QGroupBox *>("kcfg_"+QString(key));require(w,"Missing independent appearance section");return w;
        };
        auto apply=[&](const QJsonObject &values) {
            for (auto key : {"InnerGap","OuterGap","SpringFrequency","BorderWidth","CornerRadius"}) if (values.contains(key))
                spin(qPrintable("kcfg_"+QString(key)))->setValue(values.value(key).toDouble());
            for (auto key : {"IgnoreApplications","IgnoreTitles","IgnoreRoles","AppearanceIgnoredApplications"}) if (values.contains(key))
                text(qPrintable("kcfg_"+QString(key)))->setPlainText(values.value(key).toString());
            // Compatibility for existing scenario fixtures; only independent keys are saved.
            if (values.contains("AppearanceEnabled")) for (auto key : {"OutlinesEnabled","RoundedCornersEnabled","TitlebarControlEnabled"})
                section(key)->setChecked(values.value("AppearanceEnabled").toBool());
            if (values.contains("TitlebarMode")) {
                const int mode=values.value("TitlebarMode").toInt();
                section("TitlebarControlEnabled")->setChecked(mode!=0);
                form->findChild<QComboBox *>("kcfg_TitlebarScope")->setCurrentIndex(mode==2?1:0);
            }
            for (auto key : {"OutlinesEnabled","RoundedCornersEnabled","TitlebarControlEnabled"}) if (values.contains(key))
                section(key)->setChecked(values.value(key).toBool());
            if (values.contains("UseSystemBorderColors")) form->findChild<QCheckBox *>("kcfg_UseSystemBorderColors")->setChecked(values.value("UseSystemBorderColors").toBool());
            if (values.contains("TitlebarScope")) form->findChild<QComboBox *>("kcfg_TitlebarScope")->setCurrentIndex(values.value("TitlebarScope").toInt());
            for (auto key : {"ActiveBorderColor","InactiveBorderColor"}) if (values.contains(key)) {
                auto widget=form->findChild<KColorButton *>("kcfg_"+QString(key));require(widget,"KDE color widget failed to load");widget->setColor(QColor(values.value(key).toString()));
            }
            QCoreApplication::processEvents();module->save();
        };
        if (args.value(1)=="apply") {
            const auto values=QJsonDocument::fromJson(args.value(2).toUtf8()).object();
            apply(values);
        } else {
            QJsonObject values{{"InnerGap",22.5},{"OuterGap",18.5},{"SpringFrequency",32},
                {"IgnoreApplications","org.example.*\nFirefox"},{"IgnoreTitles","*Picture-in-Picture*"},{"IgnoreRoles","pop-up*"},
                {"OutlinesEnabled",false},{"RoundedCornersEnabled",true},{"TitlebarControlEnabled",false},{"BorderWidth",3.5},{"CornerRadius",12.0},{"TitlebarScope",1},
                {"UseSystemBorderColors",false},{"ActiveBorderColor","#124488"},{"InactiveBorderColor","#556677"},
                {"AppearanceIgnoredApplications","org.test.*"}};
            apply(values);
            config->reparseConfiguration();
            KConfigGroup group(config,"Script-fluidtiles-controller");
            require(group.readEntry("InnerGap",0.0)==22.5,"UI did not save gap");
            require(group.readEntry("IgnoreApplications",QString{})==values.value("IgnoreApplications").toString(),"UI did not save multiline app rules");
            require(group.readEntry("IgnoreTitles",QString{})==values.value("IgnoreTitles").toString(),"UI did not save title rules");
            require(group.readEntry("IgnoreRoles",QString{})==values.value("IgnoreRoles").toString(),"UI did not save role rules");
            require(!group.readEntry("OutlinesEnabled",true) && group.readEntry("RoundedCornersEnabled",true) && !group.readEntry("TitlebarControlEnabled",true) && group.readEntry("BorderWidth",0.0)==3.5,"UI did not save appearance settings");
            require(group.readEntry("TitlebarScope",0)==1 && group.readEntry("CornerRadius",0.0)==12,"UI did not save corner/titlebar settings");
            require(group.readEntry("ActiveBorderColor",QColor{})==QColor("#124488"),"UI did not save active color");
            require(group.readEntry("InactiveBorderColor",QColor{})==QColor("#556677"),"UI did not save inactive color");
            spin("kcfg_InnerGap")->setValue(1);text("kcfg_IgnoreApplications")->setPlainText("unsaved");
            module->load();
            require(spin("kcfg_InnerGap")->value()==22.5,"UI reload did not restore saved gap");
            require(text("kcfg_IgnoreApplications")->toPlainText()==values.value("IgnoreApplications").toString(),"UI reload did not restore saved rules");
            require(spin("kcfg_BorderWidth")->value()==3.5 && form->findChild<QComboBox *>("kcfg_TitlebarScope")->currentIndex()==1,"UI did not reload appearance fields");
            require(form->findChild<KColorButton *>("kcfg_ActiveBorderColor")->color()==QColor("#124488"),"UI did not reload color control");
            module->defaults();
            for (auto key : {"OutlinesEnabled","RoundedCornersEnabled","TitlebarControlEnabled"}) require(section(key)->isChecked(),"Appearance section default must be enabled");
            require(spin("kcfg_BorderWidth")->value()==2 && spin("kcfg_CornerRadius")->value()==8,"Appearance dimension defaults incorrect");
            require(form->findChild<QComboBox *>("kcfg_TitlebarScope")->currentIndex()==0,"Titlebar default must hide only tiles");
            require(form->findChild<KColorButton *>("kcfg_ActiveBorderColor"),"Native color control is unavailable");
            require(form->findChild<KColorButton *>("kcfg_ActiveBorderColor")->color()==QColor("#3daee9")
                && form->findChild<KColorButton *>("kcfg_InactiveBorderColor")->color()==QColor("#6e6e6e"),"UI color defaults incorrect");
            require(text("kcfg_AppearanceIgnoredApplications")->toPlainText()==Fluid::defaultIgnoredApplications,"Appearance exclusion defaults incorrect");
            require(text("kcfg_IgnoreRoles")->toPlainText()==Fluid::defaultIgnoredRoles,"UI role defaults differ from native defaults");
            require(spin("kcfg_InnerGap")->value()==10 && spin("kcfg_OuterGap")->value()==10
                && spin("kcfg_SpringFrequency")->value()==24,"UI defaults incorrect");
            require(text("kcfg_IgnoreApplications")->toPlainText()==Fluid::defaultIgnoredApplications,"UI application defaults differ from native defaults");
            require(group.readEntry("InnerGap",0.0)==22.5,"Unsaved defaults changed stored preferences");
            if (!args.value(1).isEmpty()) {
                const QDir images(args.value(1));
                form->resize(640,540);form->show();QCoreApplication::processEvents();
                require(form->grab().save(images.filePath("settings-layout.png")),"Could not capture layout settings");
                form->findChild<QTabWidget *>("tabs")->setCurrentIndex(1);QCoreApplication::processEvents();
                require(form->grab().save(images.filePath("settings-rules.png")),"Could not capture rules settings");
                form->findChild<QTabWidget *>("tabs")->setCurrentIndex(2);QCoreApplication::processEvents();
                require(form->grab().save(images.filePath("settings-appearance.png")),"Could not capture appearance settings");
            }
            module->save();
            // Keep the historical compositor suite on its explicit baseline
            // profile after proving the real form's new defaults above.
            if (qEnvironmentVariableIntValue("FLUIDTILES_BASELINE")==1) apply(QJsonObject{{"AppearanceEnabled",false}});
        }
        config->reparseConfiguration();
        require(KConfigGroup(config,"Plugins").readEntry("fluidtiles-controllerEnabled",false)==wasEnabled,
            "Saving settings changed the tiling activation flag");
        const auto data=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const auto launcher=data+"/applications/org.fluidtiles.Settings.desktop";
        if (QFileInfo::exists(launcher)) {
            KDesktopFile file(launcher);
            KShell::Errors error;
            const auto command=KShell::splitArgs(file.desktopGroup().readEntry("Exec",QString{}),KShell::NoOptions,&error);
            require(error==KShell::NoError && command.size()==1,"Invalid desktop launcher command");
            require(command[0]==data+"/kwin/scripts/fluidtiles-controller/contents/native/fluidtiles-settings",
                "Desktop launcher does not point to installed settings tool");
            require(QFileInfo(command[0]).isExecutable(),"Settings tool not executable");
        }
        std::cout<<"Settings module passed\n";
    } catch (const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
}

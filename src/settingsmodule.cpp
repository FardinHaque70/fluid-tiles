// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_config.h"
#include "windowpicker.h"
#include <KCModule>
#include <KConfigLoader>
#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>
#include <QFile>

class FluidTilesSettings : public KCModule {
    Q_OBJECT
public:
    FluidTilesSettings(QObject *parent,const KPluginMetaData &metadata,const QVariantList &)
        : KCModule(parent,metadata) {
        Ui::FluidTilesConfig ui;
        ui.setupUi(widget());
        QFile schema(":/fluidtiles/main.xml");
        auto config=new KConfigLoader(KConfigGroup(KSharedConfig::openConfig("kwinrc"),"Script-fluidtiles-controller"),&schema,this);
        if (config->items().isEmpty()) qFatal("Fluid Tiles settings schema is missing or invalid");
        addConfig(config,widget());
        auto picker=[this](QPushButton *button,QPlainTextEdit *field,WindowPicker::Property property) {
            connect(button,&QPushButton::clicked,this,[this,field,property] {
                (new WindowPicker(property,field,widget()))->open();
            });
        };
        picker(ui.pickIgnoreApplications,ui.kcfg_IgnoreApplications,WindowPicker::Application);
        picker(ui.pickIgnoreTitles,ui.kcfg_IgnoreTitles,WindowPicker::Title);
        picker(ui.pickIgnoreRoles,ui.kcfg_IgnoreRoles,WindowPicker::Role);
        picker(ui.pickAppearanceIgnoredApplications,ui.kcfg_AppearanceIgnoredApplications,WindowPicker::Application);
    }
};
K_PLUGIN_CLASS_WITH_JSON(FluidTilesSettings,"settingsmodule.json")
#include "settingsmodule.moc"

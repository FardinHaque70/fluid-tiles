// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the packaged KCModule used by both settings entry points.
#include "settings.h"
#include "pickerrules.h"
#include "animation.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QComboBox>
#include <KColorButton>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QPushButton>
#include <QListView>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QDialog>
#include <QLabel>
#include <QElapsedTimer>
#include <QThread>
#include <QPluginLoader>
#include <QLineEdit>
#include <QToolButton>
#include <QInputDialog>
#include <QSlider>
#include <QKeyEvent>
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
        const auto path=QStandardPaths::locate(QStandardPaths::GenericDataLocation,"kwin/scripts/fluidtiles-controller/metadata.json");
        const auto package=KPluginMetaData::fromJsonFile(path);
        // Resolve exactly as KDE's KPluginModel::findConfig does.
        const auto metadata=KPluginMetaData(package.rawData(),QPluginLoader(package.value("X-KDE-ConfigModule")).fileName());
        require(metadata.isValid(),"Fluid Tiles settings module not found");
        auto module=std::unique_ptr<KCModule>(KCModuleLoader::loadModule(metadata,nullptr,{"fluidtiles-controller","KWin/Script"}));
        require(bool(module),"KDE could not instantiate Fluid Tiles settings");
        module->load();
        auto form=module->widget();
        require(!form->findChild<QLabel *>("applyHelp") && !form->findChild<QLabel *>("unitsHelp"),"Removed subtexts are still present");
        for (auto name : {"pickIgnoreApplications","pickIgnoreTitles","pickIgnoreRoles","pickAppearanceIgnoredApplications"})
            require(form->findChild<QPushButton *>(name),"Missing window picker");
        const auto literal=QStringLiteral("Editor [draft] * ?");
        Fluid::IgnorePatterns picked;
        picked.setText(Fluid::literalWindowRule(literal));
        require(picked.matches(literal) && !picked.matches("Editor d anything x"),"Picked values are not literal rules");
        require(Fluid::appendWindowRule("existing\n",literal)=="existing\n"+Fluid::literalWindowRule(literal),"Picker replaced existing rules");
        require(Fluid::appendWindowRule("EXISTING","existing")=="EXISTING", "Picker duplicated an existing rule");
        require(Fluid::appendWindowRule("existing","")=="existing" && Fluid::literalWindowRule("bad\nrule").isEmpty(),"Picker accepted an absent or multiline property");
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
            auto editor=form->findChild<QWidget *>("kcfg_AnimationSettings");require(editor,"Missing animation editor");
            if(values.contains("AnimationSettings"))editor->setProperty("animationSettings",QString::fromUtf8(QJsonDocument(values["AnimationSettings"].toObject()).toJson(QJsonDocument::Compact)));
            else if(values.contains("SpringFrequency")) {
                // Historical suites deliberately select the old motion baseline.
                Fluid::AnimationSettings animation;auto legacy=Fluid::AnimationSettings::legacyPreset(values["SpringFrequency"].toDouble());
                animation.document["customPresets"]=QJsonArray{legacy};animation.select("classic-previous");
                editor->setProperty("animationSettings",animation.serialized());
            }
            if (values.contains("ExpandSingleWindow")) form->findChild<QCheckBox *>("kcfg_ExpandSingleWindow")->setChecked(values.value("ExpandSingleWindow").toBool());
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
        if(args.value(1)=="animations") {
            auto editor=form->findChild<QWidget *>("kcfg_AnimationSettings");require(editor,"No animation settings property");
            auto presets=form->findChild<QComboBox *>("animationPresets");
            require(presets && presets->count()==2 && presets->itemText(0)=="Default" && presets->itemText(1)=="Elastic", "Expected only Default and Elastic presets");
            require(!form->findChild<QWidget *>("animationPreview") && !form->findChild<QPushButton *>("previewOpen"),"Tiling preview was not removed");
            const auto saved=editor->property("animationSettings").toString();
            form->resize(740,540);form->show();auto tabs=form->findChild<QTabWidget *>("tabs");tabs->setCurrentWidget(form->findChild<QWidget *>("animationsTab"));QCoreApplication::processEvents();
            auto state=[&]{return Fluid::AnimationSettings::read(editor->property("animationSettings").toString());};
            auto speed=spin("animationSpeed");
            require(speed->minimum()==.1 && speed->maximum()==2.,"Animation speed bounds are incorrect");
            require(!form->findChild<QSlider *>("animationSpeed"),"Animation speed is still a slider");
            for(double value : {.1,.37,2.}) {
                auto input=speed->findChild<QLineEdit *>();require(input,"Animation speed is not editable");
                input->setText(speed->locale().toString(value,'f',2));speed->interpretText();
                require(state().error.isEmpty() && std::abs(state().speed()-value)<1e-9,"Typed animation speed did not update draft");
                module->save();speed->setValue(1.);module->load();
                require(std::abs(speed->value()-value)<1e-9 && std::abs(state().speed()-value)<1e-9,"Animation speed did not survive Apply/reload");
            }
            speed->setValue(.09);require(speed->value()==.1 && state().speed()==.1,"Animation speed allowed a value below 0.1");
            speed->setValue(2.01);require(speed->value()==2. && state().speed()==2.,"Animation speed allowed a value above 2");
            editor->setProperty("animationSettings",saved);module->save();
            presets->setCurrentIndex(1);require(state().presetId()=="hyprland","Elastic does not select the original Hyprland recipe");
            auto configGroup=KConfigGroup(config,"Script-fluidtiles-controller");require(configGroup.readEntry("AnimationSettings",QString{})==saved,"Selecting preset saved without Apply");
            const auto archived=state().document["customPresets"];
            auto type=form->findChild<QComboBox *>("animationCurveType");type->setCurrentIndex(1);
            spin("animationX1")->setValue(.234);require(state().profile(Fluid::AnimationEvent::Open).curve.x1==.234,"Numeric curve edit missing");
            require(presets->currentIndex()==-1,"Edited factory recipe should show Custom settings");
            auto canvas=form->findChild<QWidget *>("animationCurveCanvas");QKeyEvent arrow(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&arrow);
            require(std::abs(state().profile(Fluid::AnimationEvent::Open).curve.x1-.244)<1e-6,"Keyboard curve edit missing");
            form->findChild<QCheckBox *>("animationsEnabled")->setChecked(false);
            form->findChild<QCheckBox *>("animationsEnabled")->setChecked(true);
            require(state().profile(Fluid::AnimationEvent::Open).enabled,"Master switch erased the event choice");
            module->save();const auto committed=editor->property("animationSettings").toString();
            presets->setCurrentIndex(0);module->load();require(editor->property("animationSettings").toString()==committed,"Reload failed to discard an unsaved preset");
            module->defaults();require(state().presetId()=="fluid" && state().document["customPresets"]==archived,"Defaults removed archived presets");
            require(configGroup.readEntry("AnimationSettings",QString{})==committed,"Defaults wrote before Apply");
            module->load();form->findChild<QPushButton *>("animationReset")->click();
            auto event=form->findChild<QComboBox *>("animationEvent");
            auto track=form->findChild<QComboBox *>("animationTrack");
            auto scroll=form->findChild<QScrollArea *>("animationOptionsScroll");
            require(scroll,"No size-constrained animation editor");
            auto fit=[&]{
                for(int pass=0;pass<5;++pass)QCoreApplication::processEvents();
                require(form->width()==740 && form->height()==540,"Animation form forced a larger window");
                if(scroll->verticalScrollBar()->maximum() || scroll->horizontalScrollBar()->maximum()) {
                    std::cerr<<"Editor overflow: event="<<event->currentIndex()<<" curve="<<type->currentIndex()<<" form="<<form->width()<<"x"<<form->height()<<" content="<<scroll->widget()->width()<<"x"<<scroll->widget()->height()<<" viewport="<<scroll->viewport()->width()<<"x"<<scroll->viewport()->height()<<"\n";
                    if(!args.value(2).isEmpty())form->grab().save(QDir(args.value(2)).filePath("animation-overflow.png"));
                }
                require(scroll->verticalScrollBar()->maximum()==0 && scroll->horizontalScrollBar()->maximum()==0,"Animation controls require scrolling at a normal window size");
                auto left=form->findChild<QWidget *>("animationEventPanel");auto right=form->findChild<QWidget *>("animationCurvePanel");
                require(right->x()>=left->geometry().right(),"Event and curve fields are not side by side");
            };
            for(int preset=0;preset<2;++preset){
                presets->setCurrentIndex(preset);
                for(int e=0;e<event->count();++e){
                    event->setCurrentIndex(e);
                    if(e>=3)form->findChild<QCheckBox *>("animationInherit")->setChecked(false);
                    for(int curve=0;curve<2;++curve){
                        type->setCurrentIndex(curve);fit();
                        if(!args.value(2).isEmpty() && e==0 && preset==1){
                            QDir images(args.value(2));require(form->grab().save(images.filePath(curve ? "animation-bezier.png" : "animation-editor.png")),"Cannot capture animation editor");
                        }
                    }
                    if(e<2){track->setCurrentIndex(1);fit();track->setCurrentIndex(0);}
                }
            }
            // Old non-gallery recipes must remain readable without changing preferences.
            Fluid::AnimationSettings old;old.select("hyde-fast");
            editor->setProperty("animationSettings",old.serialized());
            require(state().presetId()=="hyde-fast" && presets->currentIndex()==-1,"A previously saved recipe was changed on load");
            editor->setProperty("animationSettings",saved);module->save();
            require(KConfigGroup(config,"Plugins").readEntry("fluidtiles-controllerEnabled",false)==wasEnabled,"Animation settings changed activation");
            std::cout<<"Animation settings editor tests passed\n";return 0;
        }
        if (args.value(1)=="picker") {
            auto wait=[&](auto predicate) {
                QElapsedTimer timer;timer.start();
                while (!predicate() && timer.elapsed()<6000) {QCoreApplication::processEvents();QThread::msleep(10);}
                require(predicate(),"Timed out waiting for the real window picker");
            };
            form->show();
            const auto title=args.value(2);
            for (const auto &entry : {QPair{"pickIgnoreApplications","kcfg_IgnoreApplications"},QPair{"pickIgnoreTitles","kcfg_IgnoreTitles"},
                    QPair{"pickIgnoreRoles","kcfg_IgnoreRoles"},QPair{"pickAppearanceIgnoredApplications","kcfg_AppearanceIgnoredApplications"}}) {
                auto field=text(entry.second);
                const auto saved=field->toPlainText();
                form->findChild<QPushButton *>(entry.first)->click();
                auto picker=form->findChild<QDialog *>("windowPicker");require(picker,"Picker did not open");
                auto list=picker->findChild<QListView *>("openWindows");
                QModelIndex selected;
                wait([&] {
                    for (int row=0;row<list->model()->rowCount();++row) {
                        auto index=list->model()->index(row,0);
                        if (index.data().toString()==title) {selected=index;return true;}
                    }
                    return false;
                });
                picker->findChild<QLineEdit *>("windowPickerSearch")->setText(title);
                require(list->model()->rowCount()==1,"Picker search did not filter the open windows");
                selected=list->model()->index(0,0);
                list->setCurrentIndex(selected);
                auto values=picker->findChild<QComboBox *>("windowRuleValues");
                auto add=picker->findChild<QPushButton *>("addWindowRule");
                if (QString(entry.first)=="pickIgnoreRoles") {
                    wait([&] {return !picker->findChild<QLabel *>("windowPickerStatus")->text().contains("Reading");});
                    require(values->count()==0 && !add->isEnabled(),"Absent Wayland role offered a rule");
                    picker->reject();
                } else {
                    wait([&] {return add->isEnabled();});
                    const auto value=values->currentData().toString();
                    require(value==(QString(entry.first)=="pickIgnoreTitles" ? title : QStringLiteral("fluidtiles-demo")),"Picker queried the wrong window property");
                    const auto expected=Fluid::appendWindowRule(saved,value);
                    if (!args.value(3).isEmpty() && QString(entry.first)=="pickIgnoreApplications")
                        require(picker->grab().save(QDir(args.value(3)).filePath("settings-window-picker.png")),"Could not capture the window picker");
                    add->click();require(field->toPlainText()==expected,"Picker did not append its selected value");
                    require(module->needsSave(),"Picked rule did not mark settings changed");
                }
                QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
                // Reload discards the unsaved picked value, like Cancel.
                module->load();require(field->toPlainText()==saved,"Unsaved picker rule leaked into stored preferences");
            }
            std::cout<<"Real window pickers passed\n";
        } else if (args.value(1)=="apply") {
            const auto values=QJsonDocument::fromJson(args.value(2).toUtf8()).object();
            apply(values);
        } else {
            QJsonObject values{{"InnerGap",22.5},{"OuterGap",18.5},{"SpringFrequency",32},{"ExpandSingleWindow",false},
                {"IgnoreApplications","org.example.*\nFirefox"},{"IgnoreTitles","*Picture-in-Picture*"},{"IgnoreRoles","pop-up*"},
                {"OutlinesEnabled",false},{"RoundedCornersEnabled",true},{"TitlebarControlEnabled",false},{"BorderWidth",3.5},{"CornerRadius",12.0},{"TitlebarScope",1},
                {"UseSystemBorderColors",false},{"ActiveBorderColor","#124488"},{"InactiveBorderColor","#556677"},
                {"AppearanceIgnoredApplications","org.test.*"}};
            apply(values);
            config->reparseConfiguration();
            KConfigGroup group(config,"Script-fluidtiles-controller");
            require(group.readEntry("InnerGap",0.0)==22.5,"UI did not save gap");
            require(!group.readEntry("ExpandSingleWindow",true),"UI did not save the single-window option");
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
            require(form->findChild<QCheckBox *>("kcfg_ExpandSingleWindow")->isChecked(),"Single-window expansion must default on");
            for (auto key : {"OutlinesEnabled","RoundedCornersEnabled","TitlebarControlEnabled"}) require(section(key)->isChecked(),"Appearance section default must be enabled");
            require(spin("kcfg_BorderWidth")->value()==2 && spin("kcfg_CornerRadius")->value()==8,"Appearance dimension defaults incorrect");
            require(form->findChild<QComboBox *>("kcfg_TitlebarScope")->currentIndex()==0,"Titlebar default must hide only tiles");
            require(form->findChild<KColorButton *>("kcfg_ActiveBorderColor"),"Native color control is unavailable");
            require(form->findChild<KColorButton *>("kcfg_ActiveBorderColor")->color()==QColor("#3daee9")
                && form->findChild<KColorButton *>("kcfg_InactiveBorderColor")->color()==QColor("#6e6e6e"),"UI color defaults incorrect");
            require(text("kcfg_AppearanceIgnoredApplications")->toPlainText()==Fluid::defaultAppearanceIgnoredApplications,"Appearance exclusion defaults incorrect");
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
            if (qEnvironmentVariableIntValue("FLUIDTILES_TEST_SINGLE")!=1) apply(QJsonObject{{"ExpandSingleWindow",false}});
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

// SPDX-License-Identifier: GPL-3.0-or-later
#include "core.h"
#include "settings.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
#include <random>

static int checks = 0;
void require(bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    try {
        for (double scale : {1.,1.45,2.}) {
            const double b=Fluid::BorderGeometry::thickness(2,scale);
            require(std::abs(b*scale-std::round(b*scale))<1e-9,"outline thickness occupies physical pixels");
            require(Fluid::BorderGeometry::thickness(0,scale)==0,"zero width removes outline reservation");
            require(Fluid::BorderGeometry::thickness(.1,scale)*scale==1,"thin outlines remain one physical pixel");
            const QRectF a(10,10,700,900),c(720,10,700,900);
            Fluid::BorderGeometry first{Fluid::BorderGeometry::clientFrame(a,b),b};
            Fluid::BorderGeometry second{Fluid::BorderGeometry::clientFrame(c,b),b};
            require(first.outer()==a && second.outer()==c,"outline reservation round trips through frame geometry");
            require(second.outer().left()-first.outer().right()==10,"visible gaps survive outline reservation");
        }
        Fluid::IgnorePatterns patterns;
        patterns.setText("  org.kde.*  \n Firefox\n\n");
        require(patterns.matches("org.kde.konsole"),"app wildcard matches full app ID");
        require(patterns.matches("FIREFOX"),"app matching ignores case");
        require(!patterns.matches("myfirefox") && !patterns.matches("org.mozilla.firefox"),"plain patterns match whole identifiers");
        patterns.setText("*Picture-in-Picture*\nReport?.pdf\n*docs/open*");
        require(patterns.matches("Video Picture-in-Picture — Browser"),"title substring wildcard works");
        require(patterns.matches("Report1.pdf") && !patterns.matches("Report12.pdf"),"single-character wildcard works");
        require(patterns.matches("Project docs/open: notes"),"title wildcard matches path separators");
        patterns.setText("[notes]\n");
        require(!patterns.matches("unrelated"),"patterns do not become unanchored regular expressions");
        patterns.setText("\n  \n");
        require(patterns.empty() && !patterns.matches("anything"),"blank rules do not match everything");
        patterns.setText("*");
        require(!patterns.matches({}),"missing properties such as Wayland window roles do not match");
        patterns.setText(Fluid::defaultIgnoredApplications);
        for (auto id : {"krunner","yakuake","spectacle","kded5","xwaylandvideobridge","plasmashell",
                       "ksplashqml","org.kde.plasmashell","org.kde.polkit-kde-authentication-agent-1",
                       "org.kde.kruler","kruler","kwin_wayland","ksmserver-logout-greeter"})
            require(patterns.matches(id),"Krohnkite default application exclusion missing");
        require(!patterns.matches("org.kde.dolphin"),"ordinary Dolphin must not be ignored by default");
        patterns.setText(Fluid::defaultIgnoredRoles);
        require(patterns.matches("quake"),"Krohnkite default role exclusion missing");
        {
            // Anonymized exact geometry/minima from the user's fifth-window failure.
            const QRectF bounds(10.3448275862,10.3448275862,2627.5862068966,1427.3103448276);
            const double gap=Fluid::Tree::snap(10,1.45);
            QHash<QString,QSizeF> hints{{"files1",{225,198.10345}},{"settings",{930,683.10345}},
                {"files2",{225,198.10345}},{"chat",{475.8621,595.1724}},{"files3",{225,198.10345}}};
            Fluid::Tree tree;
            tree.insert("files1",{},bounds,gap,1.45,hints);
            tree.insert("settings","files1",bounds,gap,1.45,hints);
            tree.root->ratio=1327.5862/(bounds.width()-gap);
            tree.insert("files2","settings",bounds,gap,1.45,hints);
            tree.insert("chat","files2",bounds,gap,1.45,hints);
            auto saved=tree.snapshot();
            tree.insert("files3","chat",bounds,gap,1.45,hints);
            require(tree.minimum(hints,gap).height()>bounds.height(),"captured fifth window reproduces old rejection");
            tree.root=std::move(saved);
            require(tree.insertFitting("files3","chat",bounds,gap,1.45,hints),"fifth window fits by retrying split axis");
            require(tree.find("files3")->parent==tree.find("chat")->parent,"fallback preserves preferred insertion tile");
            const auto tiles=tree.layout(bounds,gap,1.45,hints);
            require(tiles.size()==5,"all five windows must tile");
            for (auto it=tiles.begin();it!=tiles.end();++it) {
                if (it->width()+1/1.45<hints[it.key()].width() || it->height()+1/1.45<hints[it.key()].height()) std::cerr<<it.key().toStdString()<<" rect "<<it->width()<<"x"<<it->height()<<" min "<<hints[it.key()].width()<<"x"<<hints[it.key()].height()<<"\n";
                require(it->width()+1/1.45>=hints[it.key()].width() && it->height()+1/1.45>=hints[it.key()].height(),"fifth-window layout respects minima");
                for (auto other=tiles.begin();other!=tiles.end();++other) if (it.key()<other.key())
                    require(!it->intersects(*other),"fifth-window layout never overlaps");
            }
        }
        {
            Fluid::Tree tree;
            const QRectF bounds(0,0,2000,1200);
            QHash<QString,QSizeF> hints{{"a",{1200,100}},{"b",{120,700}},{"c",{800,600}},{"huge",{1950,1150}}};
            tree.insertFitting("a",{},bounds,10,1,hints);tree.insertFitting("b","a",bounds,10,1,hints);
            require(tree.insertFitting("c","b",bounds,10,1,hints),"retry another leaf when both preferred axes fail");
            require(tree.find("c")->parent==tree.find("a")->parent,"feasible alternate insertion target selected");
            const auto before=tree.layout(bounds,10,1,hints);
            require(!tree.insertFitting("huge","c",bounds,10,1,hints),"truly infeasible insertion rejected");
            require(tree.layout(bounds,10,1,hints)==before && !tree.find("huge"),"failed insertion restores original layout");
        }
        Fluid::Tree t;
        const QRectF area{10,10,1580,880};
        // Enabling with existing windows inserts a batch before the first
        // presentation. The result must match opening them one at a time.
        {
            Fluid::Tree batch, sequential;
            for (const auto &id : {QString("a"),QString("b"),QString("c")}) {
                batch.insert(id,{},area,10,1.45);
                sequential.insert(id,{},area,10,1.45); sequential.layout(area,10,1.45);
            }
            const auto tiles=batch.layout(area,10,1.45);
            require(batch.find("b")->parent->axis==Fluid::Axis::Vertical,
                "batched third window must stack in portrait tile, not create three columns");
            require(tiles==sequential.layout(area,10,1.45),"batched and sequential insertion must agree");
            require(tiles["a"].height()==area.height(),"third window preserves unsplit full-height tile");
            require(tiles["b"].left()==tiles["c"].left() && tiles["b"].right()==tiles["c"].right(),
                "third window shares the selected column");
            Fluid::Tree resized;
            resized.insert("a",{},{0,0,4200,1000},10,1.45);
            resized.insert("b","a",{0,0,4200,1000},10,1.45);
            resized.layout({0,0,4200,1000},10,1.45);
            resized.insert("c","a",area,10,1.45);
            const auto updated=resized.layout(area,10,1.45);
            require(resized.find("a")->parent->axis==Fluid::Axis::Vertical,
                "insertion uses current work area after an output resize");
            require(updated["b"].height()==area.height(),"insertion splits selected tile and preserves its neighbor");
        }
        // Closing the full-height member promotes a formerly stacked pair.
        // Automatic pair placement must reorient it, retaining order/ratio.
        for (double scale : {1.,1.45,2.}) for (const QString removed : {"a","b","c"}) {
            Fluid::Tree pair;
            for (const QString id : {"a","b","c"}) { pair.insert(id,{},area,10,scale);pair.layout(area,10,scale); }
            pair.find("b")->parent->ratio=.6;
            pair.remove(removed);const double ratio=pair.root->ratio;
            pair.preferSideBySidePair(area,10,scale);
            const auto placed=pair.layout(area,10,scale);
            require(placed.size()==2 && pair.root->axis==Fluid::Axis::Horizontal,"two surviving windows prefer a vertical divider");
            require(pair.root->ratio==ratio,"automatic reorientation retains the surviving split ratio");
            require(pair.root->first->parent==pair.root.get() && pair.root->second->parent==pair.root.get(),"pair reorientation retains parent links");
            for(const auto &r:placed)require(r.height()==area.height(),"surviving pair uses full-height columns");
        }
        {
            Fluid::Tree pair;const QRectF portrait(0,0,700,1200);
            pair.insert("a",{},portrait,10,1);pair.insert("b","a",portrait,10,1);
            require(pair.preferSideBySidePair(portrait,10,1),"explicit pair preference also applies on portrait areas when clients fit");
            pair.root->axis=Fluid::Axis::Vertical;pair.root->ratio=.4;
            const auto before=pair.layout(area,10,1,{{"a",{900,200}},{"b",{900,200}}});
            require(!pair.preferSideBySidePair(area,10,1,{{"a",{900,200}},{"b",{900,200}}}),"impossible side-by-side minima retain the valid existing layout");
            require(pair.layout(area,10,1,{{"a",{900,200}},{"b",{900,200}}})==before && pair.root->ratio==.4,"failed pair preference is transactional");
            require(pair.preferSideBySidePair(area,10,1,{{"a",{200,700}},{"b",{200,700}}}),"normalizing before fit checks can rescue a tall-client pair");
            pair.insert("c","b",area,10,1);const auto three=pair.layout(area,10,1);
            require(!pair.preferSideBySidePair(area,10,1) && pair.layout(area,10,1)==three,"pair policy never repacks larger layouts");
        }
        t.insert("a",{},area); auto l = t.layout(area,10,1.45);
        require(l["a"]==area,"single window must fill work area");
        t.insert("b","a",area); l = t.layout(area,10,1.45);
        require(l["a"].right()+10==l["b"].left(),"shared boundary must preserve exact gap");
        require(l["a"].left()==area.left() && l["b"].right()==area.right(),"split covers area");
        t.insert("c","b",area); l=t.layout(area,10,1.45);
        require(l["b"].bottom()+10==l["c"].top(),"portrait child splits vertically");
        auto n=t.edgeSplit("a",Fluid::Axis::Horizontal,true);
        require(n!=nullptr,"resize finds shared ancestor");
        QHash<QString,QSizeF> mins{{"a",{300,100}},{"b",{200,100}},{"c",{200,100}}};
        t.resize(n,-10000,10,mins); l=t.layout(area,10,1.45,mins);
        require(l["a"].width()>=299.5,"minimum constraint clamps left resize");
        t.resize(n,10000,10,mins); l=t.layout(area,10,1.45,mins);
        require(l["b"].width()>=199.5,"minimum constraint clamps right resize");
        require(t.swap("a","c"),"swap succeeds");
        require(t.remove("b"),"remove succeeds");
        l=t.layout(area,10,1.45);
        require(l.size()==2 && !t.find("b"),"remove collapses tree");
        require(t.remove("a") && t.remove("c") && !t.root,"all nodes removed cleanly");
        require(!t.remove("missing"),"missing remove is harmless");
        t.insert("a",{},area);t.layout(area,10,1.45);
        t.insert("b","a",area);t.layout(area,10,1.45);
        t.insert("c","b",area);t.layout(area,10,1.45);
        auto saved=t.snapshot();
        require(t.placeBeside("a","c",Fluid::Axis::Vertical,false),"edge drop reinserts source");
        l=t.layout(area,10,1.45);
        require(std::abs(l["a"].bottom()+10-l["c"].top())<1e-8,"top drop places source above target");
        require(l.size()==3,"edge drop keeps all windows");
        t.root=std::move(saved);l=t.layout(area,10,1.45);
        require(t.find("a")->parent==t.root.get(),"snapshot restores parent links");
        require(!t.placeBeside("a","a",Fluid::Axis::Horizontal,true),"self drop is harmless");
        t.remove("a");t.remove("b");t.remove("c");
        // Repeated topology changes exercise unique_ptr/parent ownership.
        for (int round=0; round<80; ++round) {
            for (int i=0; i<15; ++i) { t.insert(QString::number(i),QString::number(i-1),area); t.layout(area,2,1.45); }
            for (int i=0; i<15; ++i) require(t.remove(QString::number((i*7)%15)),"random-order removal");
            require(!t.root,"tree empty after random-order removal");
        }
        // Stateful topology/resize sequences, including portrait, ordinary
        // landscape and ultrawide areas. Assert observable geometry invariants,
        // rather than mirroring the implementation's tree traversal.
        for (double scale : {1.,1.45,2.}) for (const QRectF bounds :
            {QRectF(0,0,2400,1800),QRectF(0,0,1800,2400),QRectF(0,0,4000,1400)}) {
            Fluid::Tree tree;
            std::mt19937 random(1729);
            QStringList ids;
            int next=0;
            for (int step=0;step<180;++step) {
                if (ids.isEmpty() || (ids.size()<8 && random()%3==0)) {
                    const auto id=QString::number(next++);
                    tree.insert(id,ids.isEmpty()?QString{}:ids[random()%ids.size()],bounds,8,scale);
                    ids.append(id);
                } else if (random()%4==0 && ids.size()>1) {
                    const auto index=random()%ids.size();tree.remove(ids[index]);ids.removeAt(index);
                } else if (ids.size()>1) {
                    const auto a=ids[random()%ids.size()],b=ids[random()%ids.size()];
                    if (random()%2) tree.swap(a,b);
                    else {
                        auto n=tree.find(a);
                        if (n->parent) {
                            auto p=n->parent;
                            const double coordinate=p->axis==Fluid::Axis::Horizontal
                                ? p->box.left()+p->box.width()*(.2+double(random()%60)/100)
                                : p->box.top()+p->box.height()*(.2+double(random()%60)/100);
                            tree.resize(p,coordinate,8,{});
                        }
                    }
                }
                const auto placed=tree.layout(bounds,8,scale);
                require(placed.size()==ids.size(),"state sequence keeps every live window exactly once");
                for (const auto &id : ids) {
                    const auto r=placed[id];
                    require(r.width()>0 && r.height()>0,"state sequence keeps positive tile sizes");
                    require(r.left()>=bounds.left()-.01 && r.top()>=bounds.top()-.01
                        && r.right()<=bounds.right()+.01 && r.bottom()<=bounds.bottom()+.01,
                        "state sequence keeps tiles inside their work area");
                    for (const auto &other : ids) if (id<other)
                        require(!r.intersects(placed[other]),"state sequence prevents overlapping tiles");
                }
            }
        }
        // Admission search with unequal real-client-like minima. Check
        // observable geometry and rollback across repeated additions/removals.
        for (double scale : {1.,1.45,2.}) for (QRectF bounds : {QRectF(0,0,2600,1400),QRectF(0,0,1400,2600)}) {
            Fluid::Tree tree;
            QHash<QString,QSizeF> hints;
            QStringList ids;
            std::mt19937 random(9047);
            const double gap=Fluid::Tree::snap(7,scale);
            for (int step=0;step<120;++step) {
                if (ids.size()>2 && random()%3==0) {
                    const int index=random()%ids.size();tree.remove(ids[index]);ids.removeAt(index);
                } else {
                    const auto id=QString::number(step);
                    hints[id]={120.+random()%800,80.+random()%600};
                    const auto before=tree.layout(bounds,gap,scale,hints);
                    if (tree.insertFitting(id,ids.isEmpty()?QString{}:ids[random()%ids.size()],bounds,gap,scale,hints)) ids.append(id);
                    else require(tree.layout(bounds,gap,scale,hints)==before,"admission search rolls back unsuccessful topology changes");
                }
                const auto placed=tree.layout(bounds,gap,scale,hints);
                require(placed.size()==ids.size(),"admission search preserves every accepted window");
                for (auto it=placed.begin();it!=placed.end();++it) {
                    if (it->width()+1/scale+1e-6<hints[it.key()].width() || it->height()+1/scale+1e-6<hints[it.key()].height()) std::cerr<<"scale "<<scale<<" step "<<step<<" bounds "<<bounds.width()<<"x"<<bounds.height()<<" id "<<it.key().toStdString()<<" rect "<<it->width()<<"x"<<it->height()<<" min "<<hints[it.key()].width()<<"x"<<hints[it.key()].height()<<" tree "<<tree.minimum(hints,gap).width()<<"x"<<tree.minimum(hints,gap).height()<<"\n";
                    require(it->width()+1/scale+1e-6>=hints[it.key()].width()
                        && it->height()+1/scale+1e-6>=hints[it.key()].height(),"admission search respects unequal minima within pixel rounding");
                    require(bounds.contains(*it),"admission search stays inside work area");
                    for (auto other=placed.begin();other!=placed.end();++other) if (it.key()<other.key())
                        require(!it->intersects(*other),"admission search prevents overlaps");
                }
            }
        }
        Fluid::Spring spring; spring.reset(0); spring.goal=100;
        for (int i=0;i<10;++i) spring.step(1./144);
        auto before=spring.value; auto velocity=spring.velocity; spring.goal=-50;
        require(spring.value==before && spring.velocity==velocity,"retarget keeps position and velocity");
        for (int i=0;i<300;++i) spring.step(1./144);
        require(!spring.moving() && spring.value==-50,"spring settles exactly");
        Fluid::Spring a,b; a.reset(0); b.reset(0); a.goal=b.goal=100;
        for(int i=0;i<30;++i) a.step(1./60);
        for(int i=0;i<72;++i) b.step(1./144);
        require(std::abs(a.value-b.value)<1e-8,"motion independent of frame rate");
        Fluid::Motion m; m.reset(area); m.target({100,200,600,400}); m.step(10,24);
        require(m.rect()==QRectF(100,200,600,400),"long stall completes instead of stretching duration");
        std::cout<<checks<<" checks passed\n";
    } catch(const std::exception &e) { std::cerr<<"FAILED: "<<e.what()<<"\n"; return 1; }
}

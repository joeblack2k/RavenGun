// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/Screenshots.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QPixmap>
#include <QTabWidget>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

#include "widgets/ControllersWindow.h"
#include "widgets/GameList.h"
#include "widgets/MainWindow.h"
#include "widgets/MoveConfigDialog.h"
#include "widgets/PairWizardDialog.h"
#include "widgets/PropertiesDialog.h"
#include "widgets/SettingsWindow.h"
#include "widgets/BigPictureWindow.h"
#include "widgets/PreLaunchWindow.h"
#include "widgets/SetupWizard.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QKeyEvent>

namespace rg::ui {

namespace {

QString slug(QString s) {
    s = s.toLower();
    s.replace(QLatin1Char(' '), QLatin1Char('-'));
    return s;
}

void save(QWidget* w, const QString& dir, const QString& name) {
    const QString path = QDir(dir).filePath(name + QStringLiteral(".png"));
    w->grab().save(path);
    qInfo("screenshot: %s", qPrintable(path));
}

}  // namespace

void RunScreenshots(MainWindow* w, const QString& dir, const QString& gameId) {
    QDir().mkpath(dir);
    struct Step {
        int delay;
        std::function<void()> f;
    };
    auto steps = std::make_shared<std::vector<Step>>();
    auto props = std::make_shared<QPointer<PropertiesDialog>>();
    auto ctl = std::make_shared<QPointer<ControllersWindow>>();
    auto move = std::make_shared<QPointer<MoveConfigDialog>>();
    auto settings = std::make_shared<QPointer<SettingsWindow>>();

    // Wachten tot de scan klaar is (hoogstens 3 minuten) en de engine antwoordt.
    steps->push_back({500, [w] {
                          w->resize(1280, 720);
                          w->show();
                          QElapsedTimer t;
                          t.start();
                          while ((w->core()->library()->scanning() || !w->core()->engine()->isConnected()) && t.elapsed() < 180000)
                              QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                      }});
    steps->push_back({2500, [w, gameId] {
                          w->SetListView(true);
                          w->gameList()->SelectGame(gameId);
                      }});
    steps->push_back({600, [w, dir] { save(w, dir, QStringLiteral("main-list")); }});
    steps->push_back({100, [w] { w->SetListView(false); }});
    steps->push_back({600, [w, dir] { save(w, dir, QStringLiteral("main-grid")); }});
    steps->push_back({100, [w, props, gameId] {
                          w->SetListView(true);
                          for (QWidget* top : QApplication::topLevelWidgets()) top->setProperty("rg_seen", true);
                          w->ShowPropertiesFor(gameId, 0, false);
                          for (QWidget* top : QApplication::topLevelWidgets())
                              if (auto* p = qobject_cast<PropertiesDialog*>(top)) *props = p;
                      }});
    for (int i = 0; i < 5; ++i) {
        steps->push_back({500, [props, i] {
                              if (*props) (*props)->ActivatePane(i);
                          }});
        steps->push_back({400, [props, dir, i] {
                              if (*props) save(*props, dir, QStringLiteral("properties-") + slug((*props)->PaneName(i)));
                          }});
    }
    steps->push_back({100, [props, w, ctl] {
                          if (*props) (*props)->close();
                          *ctl = w->ShowControllersWindow();
                      }});
    steps->push_back({2500, [ctl, dir] {
                          if (*ctl) save(*ctl, dir, QStringLiteral("controllers"));
                      }});
    steps->push_back({100, [w, move] {
                          *move = new MoveConfigDialog(w->core(), 1, w);
                          (*move)->show();
                      }});
    for (int i = 0; i < 5; ++i) {
        steps->push_back({i == 0 ? 2000 : 300, [move, i] {
                              if (*move) (*move)->tabs()->setCurrentIndex(i);
                          }});
        steps->push_back({400, [move, dir, i] {
                              if (*move) save(*move, dir, QStringLiteral("move-") + slug((*move)->tabs()->tabText(i)));
                          }});
    }
    steps->push_back({100, [move, ctl, w, dir] {
                          if (*move) (*move)->close();
                          if (*ctl) (*ctl)->close();
                          auto* pair = new PairWizardDialog(w->core(), w);
                          pair->setAttribute(Qt::WA_DeleteOnClose);
                          pair->show();
                          QTimer::singleShot(1200, pair, [pair, dir] {
                              save(pair, dir, QStringLiteral("pair-wizard"));
                              pair->hide();
                              pair->deleteLater();
                          });
                      }});
    steps->push_back({1600, [w, settings] { *settings = w->ShowSettingsWindow(0); }});
    for (int i = 0; i < 5; ++i) {
        steps->push_back({i == 0 ? 1500 : 300, [settings, i] {
                              if (*settings) (*settings)->ActivatePane(i);
                          }});
        steps->push_back({500, [settings, dir, i] {
                              if (*settings) save(*settings, dir, QStringLiteral("settings-") + slug((*settings)->PaneName(i)));
                          }});
    }
    // A3: Options en de route-keuze voor een paar games ($RAVENGUN_SCREENSHOT_GAMES, komma's; standaard vier).
    QStringList extra = qEnvironmentVariable("RAVENGUN_SCREENSHOT_GAMES").split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (extra.isEmpty()) extra = {QStringLiteral("tp:TC5"), QStringLiteral("tp:Terminator"), QStringLiteral("tp:TransformersShadowsRising"),
                                  QStringLiteral("tp:JurassicPark")};
    auto extraProps = std::make_shared<QPointer<PropertiesDialog>>();
    for (const QString& id : extra) {
        const QString tag = slug(id.section(QLatin1Char(':'), 1));
        steps->push_back({300, [w, extraProps, id] {
                              if (*extraProps) (*extraProps)->close();
                              w->ShowPropertiesFor(id, 1, false);
                              *extraProps = nullptr;
                              for (QWidget* top : QApplication::topLevelWidgets())
                                  if (auto* p = qobject_cast<PropertiesDialog*>(top); p && p->isVisible()) *extraProps = p;
                          }});
        steps->push_back({700, [extraProps, dir, tag] {
                              if (*extraProps) save(*extraProps, dir, QStringLiteral("options-") + tag);
                          }});
        steps->push_back({200, [extraProps] {
                              if (*extraProps) (*extraProps)->ActivatePane(0);
                          }});
        steps->push_back({500, [extraProps, dir, tag] {
                              if (*extraProps) save(*extraProps, dir, QStringLiteral("route-") + tag);
                          }});
    }
    // De keuzelijst van de route open (de lijst is een eigen venster).
    steps->push_back({200, [extraProps] {
                          if (*extraProps && (*extraProps)->RouteCombo()) (*extraProps)->RouteCombo()->showPopup();
                      }});
    steps->push_back({600, [extraProps, dir] {
                          if (!*extraProps || !(*extraProps)->RouteCombo()) return;
                          QComboBox* c = (*extraProps)->RouteCombo();
                          save(c->view()->window(), dir, QStringLiteral("route-choice-popup"));
                          c->hidePopup();
                      }});
    steps->push_back({100, [extraProps] {
                          if (*extraProps) (*extraProps)->close();
                      }});

    // A6: de wizard, alle acht stappen (er wordt niets aangeklikt).
    auto wiz = std::make_shared<QPointer<SetupWizard>>();
    steps->push_back({300, [w, wiz] {
                          *wiz = new SetupWizard(w->core(), w);
                          (*wiz)->setAttribute(Qt::WA_DeleteOnClose);
                          (*wiz)->show();
                      }});
    for (int i = 0; i < SetupWizard::Count; ++i) {
        steps->push_back({i == 0 ? 2500 : 900, [wiz, dir, i] {
                              if (*wiz) save(*wiz, dir, QStringLiteral("wizard-%1").arg(i + 1));
                          }});
        steps->push_back({100, [wiz] {
                              if (*wiz) (*wiz)->next();
                          }});
    }
    steps->push_back({100, [wiz] {
                          if (*wiz) (*wiz)->hide();
                      }});

    // A7: de fullscreen-modus (1920x1080): menu zonder vizier, met vizier, gekozen (wacht op het tweede schot), alles tonen,
    // gedimd na inactiviteit, schermbeveiliging en de vraag bij Exit.
    auto big = std::make_shared<QPointer<BigPictureWindow>>();
    steps->push_back({300, [w, big] {
                          *big = new BigPictureWindow(w->core());
                          (*big)->setAttribute(Qt::WA_DeleteOnClose);
                          (*big)->resize(1920, 1080);
                          (*big)->show();
                      }});
    steps->push_back({1200, [big, dir] {
                          if (*big) save(*big, dir, QStringLiteral("fullscreen-menu"));
                      }});
    steps->push_back({100, [big] {
                          if (*big) (*big)->InjectAim((*big)->TileCenterNorm(2) + QPointF(0.012, 0.02), true);
                      }});
    steps->push_back({500, [big, dir] {
                          if (*big) save(*big, dir, QStringLiteral("fullscreen-crosshair"));
                      }});
    steps->push_back({100, [big] {
                          if (*big) (*big)->InjectAim((*big)->TileCenterNorm(1), true, true);
                      }});
    steps->push_back({100, [big] {
                          if (*big) (*big)->InjectAim((*big)->TileCenterNorm(1), true, false);
                      }});
    steps->push_back({400, [big, dir] {
                          if (*big) save(*big, dir, QStringLiteral("fullscreen-armed"));
                      }});
    steps->push_back({100, [big] {
                          if (!*big) return;
                          (*big)->InjectAim(QPointF(0.5, 0.5), false);
                          QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
                          for (int i = 0; i < 6; ++i) QApplication::sendEvent(*big, &down);
                          QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                          QApplication::sendEvent(*big, &enter);  // de eerste knop: Show All Games
                      }});
    steps->push_back({600, [big, dir] {
                          if (*big) save(*big, dir, QStringLiteral("fullscreen-all-games"));
                      }});
    steps->push_back({100, [big] {
                          if (!*big) return;
                          QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                          QApplication::sendEvent(*big, &enter);  // terug naar alleen speelbare games
                          (*big)->SetIdleForScreenshot(BigPictureWindow::kIdleDimMs + 20000);
                      }});
    steps->push_back({500, [big, dir] {
                          if (*big) save(*big, dir, QStringLiteral("fullscreen-dimmed"));
                      }});
    steps->push_back({100, [big] {
                          if (*big) (*big)->SetIdleForScreenshot(BigPictureWindow::kIdleSaverMs + 1000);
                      }});
    steps->push_back({500, [big, dir] {
                          if (*big) save(*big, dir, QStringLiteral("fullscreen-saver"));
                      }});
    steps->push_back({100, [big] {
                          if (!*big) return;
                          QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                          QApplication::sendEvent(*big, &esc);  // wekt alleen het scherm
                          QApplication::sendEvent(*big, &esc);  // de vraag bij Exit
                      }});
    steps->push_back({500, [big, dir] {
                          if (*big) save(*big, dir, QStringLiteral("fullscreen-exit"));
                      }});
    // Het startscherm vóór een lightgun-game (docs/prelaunch.md), met ingespeelde antwoorden van game_ready (Begin() wordt niet
    // aangeroepen: er wordt nooit iets gestart): wachten op de Move, kalibratie van speler 1 met vizier, speler 2, meedoen.
    auto pre = std::make_shared<QPointer<PreLaunchWindow>>();
    auto prePlan = [](const char* json) { return QJsonDocument::fromJson(json).object(); };
    steps->push_back({200, [w, big, pre, prePlan] {
                          if (*big) (*big)->close();
                          rg::GameEntry g;
                          g.kind = rg::GameKind::TeknoParrot;
                          g.id = QStringLiteral("tp:Halo");
                          g.profile = QStringLiteral("Halo");
                          g.name = QStringLiteral("HALO Fireteam Raven");
                          *pre = new PreLaunchWindow(w->core(), g, PreLaunchWindow::Mode::Play);
                          (*pre)->setAttribute(Qt::WA_DeleteOnClose);
                          (*pre)->resize(1920, 1080);
                          (*pre)->InjectPlan(prePlan(R"({"lightgun":true,"gun_players":2,"ready":false,"waiting":[1],"optional_off":[],"calibrate":[]})"));
                      }});
    steps->push_back({500, [pre, dir] {
                          if (*pre) save(*pre, dir, QStringLiteral("prelaunch-wait-move"));
                      }});
    steps->push_back({100, [pre, prePlan] {
                          if (*pre) (*pre)->InjectPlan(prePlan(R"({"lightgun":true,"gun_players":2,"ready":true,"waiting":[],"optional_off":[],"calibrate":[1,2]})"));
                      }});
    steps->push_back({500, [pre] {
                          if (!*pre) return;
                          rg::bigpicture::AimState a;
                          a.valid = true;
                          a.pos = QPointF(0.56, 0.44);
                          (*pre)->InjectAim(a);
                      }});
    steps->push_back({200, [pre, dir] {
                          if (*pre) save(*pre, dir, QStringLiteral("prelaunch-calibrate-p1"));
                      }});
    steps->push_back({100, [pre] {
                          QKeyEvent skip(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
                          if (*pre) QApplication::sendEvent(*pre, &skip);   // speler 1 overslaan
                      }});
    steps->push_back({500, [pre, dir] {
                          if (*pre) save(*pre, dir, QStringLiteral("prelaunch-calibrate-p2"));
                      }});
    steps->push_back({100, [pre] {
                          QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                          if (*pre) QApplication::sendEvent(*pre, &esc);   // afbreken, niets starten
                      }});
    steps->push_back({200, [w, pre, prePlan] {
                          rg::GameEntry g;
                          g.kind = rg::GameKind::TeknoParrot;
                          g.id = QStringLiteral("tp:Halo");
                          g.profile = QStringLiteral("Halo");
                          g.name = QStringLiteral("HALO Fireteam Raven");
                          *pre = new PreLaunchWindow(w->core(), g, PreLaunchWindow::Mode::Play);
                          (*pre)->setAttribute(Qt::WA_DeleteOnClose);
                          (*pre)->resize(1920, 1080);
                          (*pre)->InjectPlan(prePlan(R"({"lightgun":true,"gun_players":2,"ready":true,"waiting":[],"optional_off":[2],"calibrate":[1]})"));
                      }});
    steps->push_back({500, [pre] {
                          QKeyEvent skip(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
                          if (*pre) QApplication::sendEvent(*pre, &skip);   // speler 1 overslaan: speler 2 mag meedoen
                      }});
    steps->push_back({1500, [pre, dir] {
                          if (*pre) save(*pre, dir, QStringLiteral("prelaunch-join"));
                          QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                          if (*pre) QApplication::sendEvent(*pre, &esc);
                      }});
    steps->push_back({200, [] { QApplication::quit(); }});

    auto run = std::make_shared<std::function<void(size_t)>>();
    *run = [steps, run](size_t i) {
        if (i >= steps->size()) return;
        QTimer::singleShot((*steps)[i].delay, qApp, [steps, run, i] {
            (*steps)[i].f();
            (*run)(i + 1);
        });
    };
    (*run)(0);
}

}  // namespace rg::ui

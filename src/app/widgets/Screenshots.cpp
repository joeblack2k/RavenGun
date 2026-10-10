// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/Screenshots.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
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

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Fullscreen-modus ("big picture", A7) voor Steam Game Mode en de tv: zwart scherm met grote game-iconen, te bedienen met
// de lightgun (eigen vizier uit de aim-stroom van de engine), muis, toetsenbord en gamepad. Logica zonder widgets:
// core/BigPicture.h. Tijdens een game verbergt het venster zich en het komt terug zodra de game stopt.
//
// OLED: alles is zwart met gedempte tekst en lijnen (geen lichte vlakken); na kIdleDimMs zonder invoer dimmen de iconen en
// schuift de inhoud langzaam heen en weer, na kIdleSaverMs blijft alleen een klein, bewegend tekstje over.
#include <QElapsedTimer>
#include <QHash>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include "core/AppCore.h"
#include "core/BigPicture.h"

namespace rg::ui {

class BigPictureWindow final : public QWidget {
    Q_OBJECT
public:
    static constexpr qint64 kIdleDimMs = 45 * 1000;
    static constexpr qint64 kIdleSaverMs = 5 * 60 * 1000;

    explicit BigPictureWindow(rg::AppCore* core, QWidget* parent = nullptr);
    ~BigPictureWindow() override;

    // Schermvullend tonen en de invoer (aim-stroom, gamepads) starten.
    void Open();
    // Voor schermafdrukken: vizier op een vaste plek (0..1), een tegel kiezen en de inactiviteit nabootsen.
    void InjectAim(const QPointF& norm, bool valid, bool trigger = false);
    void SelectIndex(int index);
    void SetIdleForScreenshot(qint64 idleMs);
    QPointF TileCenterNorm(int index) const;  // midden van tegel `index` als 0..1 (schermafdrukken)
    bool ShowAll() const { return show_all_; }
    int GameCount() const { return static_cast<int>(games_.size()); }

signals:
    void ExitRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void closeEvent(QCloseEvent* e) override;

private:
    enum class Kind { Game, ToggleAll, StopGame, Exit, ConfirmExit, Cancel };
    struct Target {
        Kind kind;
        int game = -1;
        QRectF rect;
        QString label;
    };
    void Rebuild();
    void Relayout();
    void OnAim(const rg::bigpicture::AimState& s);
    void OnGamepad(int action);
    void OnLauncherState();
    void Activate(int target);
    void MoveSelection(rg::bigpicture::Move m);
    void Touch();             // invoer gezien: inactiviteit terug naar nul
    bool WakeIfIdle();        // true = de invoer heeft alleen het scherm gewekt
    int TargetAt(const QPointF& p) const;
    qint64 Idle() const;
    QPointF DriftOffset() const;
    QPixmap IconFor(const rg::GameEntry& g, int size);
    void DrawStatus(QPainter& p);
    void DrawCrosshair(QPainter& p);

    rg::AppCore* core_;
    rg::AimStream aim_;
    rg::GamepadInput pads_;
    QList<rg::GameEntry> games_;
    rg::bigpicture::Layout layout_;
    QList<Target> targets_;
    int selected_ = 0;
    int first_row_ = 0;
    int visible_rows_ = 1;
    bool show_all_ = false;
    bool confirm_exit_ = false;
    bool hide_during_game_ = true;
    bool hidden_for_game_ = false;
    bool ignore_trigger_until_release_ = false;
    rg::bigpicture::ShotConfirm confirm_;
    rg::bigpicture::ShotConfirm::Result shot_;
    rg::bigpicture::AimState aim_state_;
    QPointF crosshair_;        // in vensterpixels
    bool crosshair_visible_ = false;
    QElapsedTimer clock_;
    qint64 last_input_ = 0;
    qint64 idle_override_ = -1;
    bool mouse_seen_ = false;
    QPointF last_mouse_;
    QTimer frame_;
    QString message_;          // melding bovenaan (Engels), bv. "Starting ..."
    QHash<QString, QPixmap> icon_cache_;
    rg::GameLauncher::State last_state_ = rg::GameLauncher::State::EngineOffline;
};

}  // namespace rg::ui

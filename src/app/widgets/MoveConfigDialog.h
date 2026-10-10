// SPDX-License-Identifier: GPL-3.0-or-later
// Instellingen van de PS Move van een speler, in tabbladen zoals Dolphin's MappingWindow (Source/Core/DolphinQt/Config/
// Mapping/MappingWindow.cpp, GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08): General, Aiming, Rumble,
// Buttons en Calibration. Alle wijzigingen gaan live naar de engine (IPC met `player`) en de engine bewaart ze.
#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <functional>

#include "core/AppCore.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;

namespace rg::ui {

// Een schuifregelaar met waarde-label die hoogstens elke 150 ms een verzoek stuurt (zoals createLiveSender in de webapp).
class LiveSlider;

class MoveConfigDialog final : public QDialog {
    Q_OBJECT
public:
    MoveConfigDialog(rg::AppCore* core, int player, QWidget* parent = nullptr);
    QTabWidget* tabs() { return m_tabs; }

private:
    QWidget* MakeGeneralTab();
    QWidget* MakeAimingTab();
    QWidget* MakeRumbleTab();
    QWidget* MakeButtonsTab();
    QWidget* MakeCalibrationTab();
    void Load();
    void Apply(const QJsonObject& status);
    void Send(const QString& cmd, QJsonObject fields, std::function<void(const rg::EngineReply&)> after = {});
    void ShowError(const QString& text);

    rg::AppCore* core_;
    int player_;
    QTabWidget* m_tabs = nullptr;
    QLabel* m_error = nullptr;
    QJsonObject m_status, m_defaults;
    bool m_loading = false;
    // General
    QLineEdit* m_name = nullptr;
    QString m_key;
    QLabel* m_info = nullptr;
    QLabel* m_battery = nullptr;
    QLabel* m_led = nullptr;
    // Aiming
    QMap<QString, QRadioButton*> m_fusion;
    QCheckBox* m_learning = nullptr;
    QCheckBox* m_filter = nullptr;
    LiveSlider* m_predict = nullptr;
    LiveSlider* m_min_cutoff = nullptr;
    LiveSlider* m_beta = nullptr;
    QSpinBox* m_h_edge = nullptr;
    QSpinBox* m_v_edge = nullptr;
    QComboBox* m_clock = nullptr;
    // Rumble
    QCheckBox* m_rumble_on = nullptr;
    LiveSlider* m_strength = nullptr;
    LiveSlider* m_pulse = nullptr;
    LiveSlider* m_repeat = nullptr;
    LiveSlider* m_pump = nullptr;
    // Buttons
    QMap<QString, QComboBox*> m_buttons;
    QLabel* m_buttons_state = nullptr;
    // Calibration
    QLabel* m_mag = nullptr;
    QLabel* m_bias = nullptr;
    QLabel* m_trim = nullptr;
    QTimer* m_refresh = nullptr;
};

}  // namespace rg::ui

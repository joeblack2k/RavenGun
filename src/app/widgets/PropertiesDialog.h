// SPDX-License-Identifier: GPL-3.0-or-later
// Eigenschappen van een game, opgebouwd als Dolphin's PropertiesDialog (Source/Core/DolphinQt/Config/PropertiesDialog.cpp,
// GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08): een StackedSettingsWindow met panelen
// General, Options, Display, Patches, Memory Card en Info.
#pragma once
#include <QList>
#include <QWidget>

#include "core/AppCore.h"
#include "core/DongleStatus.h"
#include "core/MemoryCard.h"
#include "core/TpProfile.h"
#include "widgets/StackedSettingsWindow.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;
class QPushButton;
class QFormLayout;

namespace rg::ui {

// Formulier voor de velden van het launch-profiel van een game, met widgets naar FieldType (Bool, Dropdown, Slider, Text).
// Beginwaarden komen uit de bibliotheek (`stored`, sleutel "Categorie/Veld"), anders uit het profiel; wijzigingen gaan via
// changedValues() naar de bibliotheek en daarna via rg::ProfileSync naar het profiel.
class TpFieldEditor : public QWidget {
    Q_OBJECT
public:
    TpFieldEditor(const QString& profilePath, const QList<rg::TpField>& fields, const QVariantMap& stored, QWidget* parent = nullptr);
    QVariantMap changedValues() const;
    void markSaved();
    bool isWritable() const { return writable_; }
    static QString keyOf(const rg::TpField& f) { return f.category + QLatin1Char('/') + f.name; }

private:
    struct Row {
        rg::TpField field;  // field.value = beginwaarde
        QWidget* widget = nullptr;
        QString current() const;
    };
    QString path_;
    QList<Row> rows_;
    bool writable_ = true;
};

class PropertiesDialog final : public StackedSettingsWindow {
    Q_OBJECT
public:
    PropertiesDialog(rg::AppCore* core, const rg::GameEntry& game, QWidget* parent = nullptr);
    // Voor tests en schermafdrukken: aantal optierijen in Options en opties die elders staan (Display, door de app geregeld).
    int OptionRowCount() const { return options_shown_; }
    int OptionsElsewhere() const { return options_elsewhere_; }
    QComboBox* RouteCombo() const { return route_combo_; }
    // Voor tests: het paneel Patches (lijst met vinkjes, effectieve instellingen en de knoppen "Open ... folder").
    QTreeWidget* PatchTree() const { return patch_tree_; }
    QTreeWidget* SettingsTree() const { return settings_tree_; }
    QPushButton* OpenUserFolderButton() const { return open_user_; }
    QPushButton* OpenShippedFolderButton() const { return open_shipped_; }
    // Voor tests: de groep Dongle in Options (checkbox, bron, statusregel, Refresh).
    QCheckBox* DongleCheck() const { return dongle_use_; }
    QComboBox* DongleSourceCombo() const { return dongle_source_; }
    QLabel* DongleStatusLabel() const { return dongle_status_; }
    QPushButton* DongleRefreshButton() const { return dongle_refresh_; }

private:
    QWidget* MakeGeneralPane();
    QWidget* MakeOptionsPane();
    QWidget* MakeDongleGroup();
    void RefreshDongle();
    QWidget* MakeDisplayPane();
    QWidget* MakePatchesPane();
    QWidget* MakeMemoryCardPane();
    QWidget* MakeInfoPane();
    void RefreshMemoryCard();
    void RefreshPatches();
    void SaveGeneral();

    rg::AppCore* core_;
    rg::GameEntry game_;
    rg::GameContext ctx_;
    QString profilePath_;  // UserProfiles/<P>.xml, of bij een eigen profiel de gebruikerslaag van game.ini (leeg zonder profiel)
    QList<rg::TpField> profileFields() const;
    // General
    QLineEdit* name_edit_ = nullptr;
    QComboBox* template_combo_ = nullptr;
    QPlainTextEdit* note_edit_ = nullptr;
    QComboBox* route_combo_ = nullptr;
    int options_shown_ = 0, options_elsewhere_ = 0;
    // Options > Dongle ([Dongle] in game.ini; de echte, fysieke dongle via USB pass-through)
    QCheckBox* dongle_use_ = nullptr;
    QComboBox* dongle_source_ = nullptr;
    QLabel* dongle_status_ = nullptr;
    QLabel* dongle_hint_ = nullptr;
    QPushButton* dongle_refresh_ = nullptr;
    int dongle_request_ = 0;   // alleen het laatste antwoord telt
    // Patches
    QTreeWidget* patch_tree_ = nullptr;
    QTreeWidget* settings_tree_ = nullptr;
    QPlainTextEdit* patch_desc_ = nullptr;
    QLabel* patch_status_ = nullptr;
    QPushButton* open_user_ = nullptr;
    QPushButton* open_shipped_ = nullptr;
    bool refreshing_patches_ = false;
    // Memory card
    QTreeWidget* mc_tree_ = nullptr;
    QPushButton* mc_delete_ = nullptr;
    QList<rg::MemoryCardFile> mc_files_;
};

}  // namespace rg::ui

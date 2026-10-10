// SPDX-License-Identifier: GPL-3.0-or-later
// Eigenschappen van een game, opgebouwd als Dolphin's PropertiesDialog (Source/Core/DolphinQt/Config/PropertiesDialog.cpp,
// GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08): een StackedSettingsWindow met panelen
// General, Options, Display, Memory Card en Info.
#pragma once
#include <QList>
#include <QWidget>

#include "core/AppCore.h"
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

private:
    QWidget* MakeGeneralPane();
    QWidget* MakeOptionsPane();
    QWidget* MakeDisplayPane();
    QWidget* MakeMemoryCardPane();
    QWidget* MakeInfoPane();
    void RefreshMemoryCard();
    void SaveGeneral();

    rg::AppCore* core_;
    rg::GameEntry game_;
    rg::GameContext ctx_;
    QString profilePath_;  // UserProfiles/<P>.xml (leeg zonder profiel)
    // General
    QLineEdit* name_edit_ = nullptr;
    QComboBox* template_combo_ = nullptr;
    QPlainTextEdit* note_edit_ = nullptr;
    QComboBox* route_combo_ = nullptr;
    int options_shown_ = 0, options_elsewhere_ = 0;
    // Memory card
    QTreeWidget* mc_tree_ = nullptr;
    QPushButton* mc_delete_ = nullptr;
    QList<rg::MemoryCardFile> mc_files_;
};

}  // namespace rg::ui

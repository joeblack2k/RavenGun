// SPDX-License-Identifier: GPL-2.0-or-later
// Overgenomen uit Dolphin (GPL-2.0-or-later), commit e6f3ae17627e4344da95b13424af5baf4c892b08:
//   Source/Core/DolphinQt/QtUtils/WrapInScrollArea.{h,cpp} (GetWrappedWidget, HintingScrollArea)
//   Source/Core/DolphinQt/QtUtils/NonDefaultQPushButton.{h,cpp}
// Copyright 2018/2022 Dolphin Emulator Project. Aanpassingen voor RavenGun: namespace, een helper voor een
// schuifregelaar met waarde-label en een groepskop.
#pragma once
#include <QPushButton>

class QLayout;
class QWidget;

namespace rg::ui {

// Een knop die geen standaardknop van het dialoogvenster wordt (Enter activeert hem niet).
class NonDefaultQPushButton : public QPushButton {
public:
    explicit NonDefaultQPushButton(const QString& text = {}, QWidget* parent = nullptr);
};

// Een scrollgebied dat de grootte van zijn widget voorstelt (Dolphin: GetWrappedWidget).
QWidget* GetWrappedWidget(QWidget* wrapped_widget);
// Zet een layout in een scrollgebied als layout van `parent` (Dolphin: WrapInScrollArea).
void WrapInScrollArea(QWidget* parent, QLayout* wrapped_layout);

}  // namespace rg::ui

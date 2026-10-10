// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Ontwikkelaarsoptie --screenshot-dir MAP: opent na elkaar het hoofdvenster (lijst en raster), Properties (alle panelen),
// Controllers (met de Move-instellingen per tabblad en de koppelwizard) en Settings (alle panelen), maakt van elk een
// afbeelding met QWidget::grab() en sluit de app. Bedoeld voor QT_QPA_PLATFORM=offscreen tegen een test-engine; er wordt
// niets aangeklikt dat iets verandert.
#include <QString>

namespace rg::ui {

class MainWindow;

void RunScreenshots(MainWindow* window, const QString& dir, const QString& gameId);

}  // namespace rg::ui

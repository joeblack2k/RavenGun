// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/PropertiesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

#include "core/AppPaths.h"
#include "core/FileSafety.h"
#include "core/GamepadText.h"
#include "core/GameOptions.h"
#include "core/Launcher.h"
#include "core/TextConfig.h"
#include "widgets/QtUtils.h"
#include "ravengun/gamesettings/gamepad_settings.hpp"

namespace rg::ui {

namespace {

const QRegularExpression& displayFieldRe() {
    static const QRegularExpression re(QStringLiteral("windowed|resolution|width|height|fullscreen|full screen|displaymode|stretch|borderless"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Velden die de app zelf bepaalt (de invoer loopt altijd via de virtuele RavenGun-apparaten).
bool managedField(const QString& name) { return name == QStringLiteral("Input API"); }

QLabel* hintLabel(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont f = l->font();
    f.setItalic(true);
    l->setFont(f);
    return l;
}

QLineEdit* readOnlyEdit(const QString& text) {
    auto* e = new QLineEdit(text);
    e->setReadOnly(true);
    e->setCursorPosition(0);
    return e;
}

bool isTrue(const QString& v) { return v == QStringLiteral("1") || v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0; }

QString groupOf(const rg::GameOption& o) {
    if (!o.category.isEmpty()) return o.category;
    if (o.target.kind == rg::OptionTarget::Kind::TpField && !o.target.category.isEmpty()) return o.target.category;
    if (o.target.kind == rg::OptionTarget::Kind::Env || o.target.kind == rg::OptionTarget::Kind::TpTag) return QObject::tr("Launch");
    // Opties in een configuratiebestand van de game: gegroepeerd per bestand.
    if (!o.target.file.isEmpty() && o.target.kind != rg::OptionTarget::Kind::TpField) {
        const QString name = QFileInfo(o.target.file).fileName();
        if (!name.isEmpty() && name.compare(QStringLiteral("teknoparrot.ini"), Qt::CaseInsensitive) != 0)
            return QObject::tr("Game file: %1").arg(name);
    }
    const int dot = o.key.indexOf(QLatin1Char('.'));
    if (dot > 0) {
        QString g = o.key.left(dot);
        g.replace(QLatin1Char('_'), QLatin1Char(' '));
        if (g == QStringLiteral("adj")) return QObject::tr("Operator settings");
        if (g == QStringLiteral("arg")) return QObject::tr("Arguments");
        g[0] = g[0].toUpper();
        return g;
    }
    return QObject::tr("Game options");
}

}  // namespace

// --- TpFieldEditor ----------------------------------------------------------------------------------------------------

QString TpFieldEditor::Row::current() const {
    if (auto* c = qobject_cast<QCheckBox*>(widget)) return c->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
    if (auto* c = qobject_cast<QComboBox*>(widget)) return c->currentText();
    if (auto* s = qobject_cast<QSpinBox*>(widget)) return QString::number(s->value());
    if (auto* e = qobject_cast<QLineEdit*>(widget)) return e->text();
    return field.value;
}

TpFieldEditor::TpFieldEditor(const QString& profilePath, const QList<rg::TpField>& fields, const QVariantMap& stored, QWidget* parent)
    : QWidget(parent), path_(profilePath) {
    const rg::WriteCheck w = rg::checkWritable(profilePath);
    writable_ = w.ok;
    auto* form = new QFormLayout(this);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (rg::TpField f : fields) {
        if (stored.contains(keyOf(f))) f.value = stored.value(keyOf(f)).toString();
        Row r;
        r.field = f;
        const QString type = f.type.toLower();
        if (type == QStringLiteral("bool")) {
            auto* c = new QCheckBox;
            c->setChecked(isTrue(f.value));
            r.widget = c;
        } else if (type == QStringLiteral("dropdown") && !f.options.isEmpty()) {
            auto* c = new QComboBox;
            c->addItems(f.options);
            if (!f.options.contains(f.value)) c->addItem(f.value);
            c->setCurrentText(f.value);
            r.widget = c;
        } else if ((type == QStringLiteral("slider") || type == QStringLiteral("numeric")) && f.hasMin && f.hasMax) {
            auto* s = new QSpinBox;
            s->setRange(f.min, f.max);
            s->setValue(f.value.toInt());
            r.widget = s;
        } else {
            r.widget = new QLineEdit(f.value);
        }
        r.widget->setEnabled(writable_);
        QString tip = f.hint;
        if (!writable_) tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + w.reason;
        r.widget->setToolTip(tip);
        QString label = f.name;
        if (!f.category.isEmpty() && f.category != QStringLiteral("General")) label = f.category + QStringLiteral(" / ") + f.name;
        form->addRow(label + QLatin1Char(':'), r.widget);
        rows_ << r;
    }
    if (!writable_) form->addRow(hintLabel(tr("These settings are read-only: %1").arg(w.reason)));
}

QVariantMap TpFieldEditor::changedValues() const {
    QVariantMap m;
    for (const Row& r : rows_)
        if (r.current() != r.field.value) m.insert(keyOf(r.field), r.current());
    return m;
}

void TpFieldEditor::markSaved() {
    for (Row& r : rows_) r.field.value = r.current();
}

// --- PropertiesDialog ------------------------------------------------------------------------------------------------

PropertiesDialog::PropertiesDialog(rg::AppCore* core, const rg::GameEntry& game, QWidget* parent)
    : StackedSettingsWindow(parent), core_(core), game_(game) {
    setWindowTitle(tr("Properties: %1").arg(game_.displayName()));
    ctx_ = core_->gameData()->context(game_, core_->library()->tpDir());
    if (game_.kind == rg::GameKind::TeknoParrot || (game_.kind == rg::GameKind::Unity && !game_.profile.isEmpty()))
        // Een game met een eigen profiel in game.ini: de velden komen uit game.ini en wijzigingen gaan naar de gebruikerslaag daarvan; anders
        // het profiel van TeknoParrotUi (UserProfiles/<P>.xml).
        profilePath_ = ctx_.ownProfile ? ctx_.userGameIni()
                                       : core_->library()->tpDir() + QStringLiteral("/UserProfiles/") + game_.profile + QStringLiteral(".xml");
    if (!ctx_.ownProfile && !QFileInfo::exists(profilePath_)) profilePath_.clear();

    AddWrappedPane(MakeGeneralPane(), tr("General"));
    AddWrappedPane(MakeOptionsPane(), tr("Options"));
    AddWrappedPane(MakeDisplayPane(), tr("Display"));
    AddWrappedPane(MakePatchesPane(), tr("Patches"));
    AddPane(MakeMemoryCardPane(), tr("Memory Card"));
    AddWrappedPane(MakeInfoPane(), tr("Info"));
    connect(this, &QDialog::finished, this, &PropertiesDialog::SaveGeneral);
    OnDoneCreatingPanes();
    resize(std::max(width(), 900), std::max(height(), 620));
}

// De ConfigValues van het profiel: uit game.ini bij een eigen profiel, anders uit UserProfiles/<P>.xml.
QList<rg::TpField> PropertiesDialog::profileFields() const {
    if (ctx_.ownProfile) return core_->gameData()->profileFields(ctx_.profile, core_->library()->tpDir());
    rg::TpProfile p;
    if (!profilePath_.isEmpty() && p.load(profilePath_)) return p.fields();
    return {};
}

QWidget* PropertiesDialog::MakeGeneralPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* box = new QGroupBox(tr("Game"));
    auto* form = new QFormLayout(box);
    const rg::GameOverride ov = core_->library()->override_(game_.id);
    name_edit_ = new QLineEdit(game_.displayName());
    name_edit_->setPlaceholderText(game_.name);
    form->addRow(tr("Name:"), name_edit_);
    form->addRow(tr("Type:"), new QLabel(rg::kindLabel(game_.kind)));
    form->addRow(tr("Game folder:"), readOnlyEdit(game_.folder));
    form->addRow(tr("Executable:"), readOnlyEdit(game_.exePath));
    form->addRow(tr("Game profile:"), readOnlyEdit(game_.profile.isEmpty() ? tr("(not set up yet)") : game_.profile));
    v->addWidget(box);

    if (game_.kind == rg::GameKind::Detected || game_.kind == rg::GameKind::Unconfigured) {
        auto* link = new QGroupBox(tr("Link to a known game"));
        auto* lf = new QFormLayout(link);
        template_combo_ = new QComboBox;
        template_combo_->setEditable(true);
        template_combo_->setInsertPolicy(QComboBox::NoInsert);
        template_combo_->addItem(tr("(not linked)"), QString());
        // Eerst de voorgestelde, dan alle bekende games (alfabetisch op naam).
        const QString tp = core_->library()->tpDir();
        QStringList seen;
        for (const QString& c : game_.candidates) {
            const rg::TemplateInfo t = rg::LibraryScanner::templateInfo(tp, c);
            template_combo_->addItem(tr("%1 (%2) - suggested").arg(t.name, t.profile), t.profile);
            seen << c;
        }
        QList<rg::TemplateInfo> all = rg::LibraryScanner::loadTemplates(tp);
        std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
        for (const auto& t : all) {
            if (seen.contains(t.profile)) continue;
            template_combo_->addItem(QStringLiteral("%1 (%2)%3").arg(t.name, t.profile, t.gunGame ? tr(" - lightgun") : QString()), t.profile);
        }
        template_combo_->completer()->setFilterMode(Qt::MatchContains);
        template_combo_->completer()->setCompletionMode(QCompleter::PopupCompletion);
        const QString cur = ov.templateProfile.isEmpty() ? game_.templateProfile : ov.templateProfile;
        const int idx = template_combo_->findData(cur);
        template_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
        lf->addRow(tr("Game:"), template_combo_);
        lf->addRow(hintLabel(tr("RavenGun recognises games by comparing the files in the folder with the known games of the "
                                "game runtime. Pick the right game if the guess is wrong, then use \"Set Up Game...\" in the "
                                "game's context menu.")));
        v->addWidget(link);
    }

    auto* route = new QGroupBox(tr("Input route"));
    auto* rf = new QFormLayout(route);
    const QString prof = ctx_.profile;
    const rg::RouteInfo ri = core_->gameData()->route(prof);
    // Keuze per game: de aanbevolen route (de meegeleverde patches/<Game>/game.ini) of een andere die voor deze game kan; bewaard
    // als [Input] Route in het gebruikers-game.ini (~/.local/share/ravengun/patches), dat de engine en play-halo.sh bij de volgende
    // start lezen.
    route_combo_ = new QComboBox;
    const QStringList routes = core_->gameData()->availableRoutes(prof);
    for (const QString& r : routes) {
        QString label = rg::GameData::routeLabel(r);
        if (r == ri.baseRoute) label += tr(" - recommended");
        route_combo_->addItem(label, r);
        route_combo_->setItemData(route_combo_->count() - 1, rg::GameData::routeDescription(r), Qt::ToolTipRole);
    }
    route_combo_->setCurrentIndex(std::max(0, route_combo_->findData(ri.route)));
    route_combo_->setEnabled(!prof.isEmpty() && routes.size() > 1 && game_.hasProfile());
    rf->addRow(tr("Route:"), route_combo_);
    auto* routeInfo = new QLabel;
    routeInfo->setWordWrap(true);
    rf->addRow(QString(), routeInfo);
    auto showRoute = [this, routeInfo, prof] {
        const QString r = route_combo_->currentData().toString();
        const rg::RouteInfo cur = core_->gameData()->route(prof);
        QStringList lines{rg::GameData::routeDescription(r)};
        if (r == QStringLiteral("tp-shm") && !cur.layout.isEmpty()) lines << tr("I/O layout: %1").arg(cur.layout);
        if ((r == QStringLiteral("hid-board") || r == QStringLiteral("jvs")) && !cur.board.isEmpty()) lines << tr("Gun board: %1").arg(cur.board);
        if (cur.userChoice) lines << tr("Your choice (the recommended route is: %1).").arg(rg::GameData::routeLabel(cur.baseRoute));
        routeInfo->setText(lines.join(QLatin1Char('\n')));
    };
    showRoute();
    connect(route_combo_, &QComboBox::activated, this, [this, prof, showRoute, routeInfo](int) {
        QString err;
        const QString r = route_combo_->currentData().toString();
        if (!core_->gameData()->setUserRoute(prof, r, &err)) {
            QMessageBox::warning(this, tr("Input route"), tr("Could not save the input route: %1").arg(err));
            route_combo_->setCurrentIndex(std::max(0, route_combo_->findData(core_->gameData()->route(prof).route)));
        }
        showRoute();
        core_->gameListModel()->reload();
        if (core_->launcher()->canStop() && core_->launcher()->runningProfile() == prof)
            routeInfo->setText(routeInfo->text() + QLatin1Char('\n') + tr("The game is running: the new route applies from the next start."));
    });
    QString routeHint = tr("Saved at once; the engine uses it the next time the game starts.");
    if (!core_->gameData()->userRoutesError().isEmpty()) routeHint += QLatin1Char('\n') + core_->gameData()->userRoutesError();
    if (routes.size() <= 1) routeHint = tr("This game has only one input route.");
    rf->addRow(hintLabel(routeHint));
    v->addWidget(route);
    v->addWidget(MakeGamepadGroup(prof));

    auto* notes = new QGroupBox(tr("Notes"));
    auto* nl = new QVBoxLayout(notes);
    const QString projectNote = core_->gameData()->note(prof);
    if (!projectNote.isEmpty()) nl->addWidget(hintLabel(projectNote));
    note_edit_ = new QPlainTextEdit(ov.note);
    note_edit_->setPlaceholderText(tr("Your own note for this game (shown in the game list)"));
    note_edit_->setMaximumHeight(90);
    nl->addWidget(note_edit_);
    v->addWidget(notes);
    v->addStretch(1);
    return w;
}

// De gamepad-indeling die deze game gebruikt (docs/gamepad.md): [Gamepad] in game.ini over de standaardindeling heen, met per regel
// de herkomst (standaard, meegeleverd game.ini of het game.ini van de gebruiker). Alleen lezen; wijzigen gaat in game.ini zelf.
QWidget* PropertiesDialog::MakeGamepadGroup(const QString& prof) {
    auto* box = new QGroupBox(tr("Gamepad"));
    auto* v = new QVBoxLayout(box);
    const auto gm = ravengun::gamesettings::gamepad_mapping_for(core_->gameData()->dirs(), prof.toStdString());
    v->addWidget(hintLabel(gm.has_section
                               ? tr("A gamepad next to the lightgun gives this game the controls below. This game changes the default "
                                    "mapping in the [Gamepad] section of its game.ini.")
                               : tr("A gamepad next to the lightgun gives this game the controls below. This game uses the default "
                                    "mapping (it has no [Gamepad] section in its game.ini).")));
    gamepad_tree_ = new QTreeWidget;
    gamepad_tree_->setHeaderLabels({tr("Gamepad control"), tr("Does"), tr("From")});
    gamepad_tree_->setRootIsDecorated(false);
    gamepad_tree_->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto& r : gm.rows) {
        auto* it = new QTreeWidgetItem(gamepad_tree_);
        it->setText(0, QString::fromStdString(r.control));
        it->setText(1, rg::GamepadText::target(QString::fromStdString(r.target)));
        it->setText(2, rg::GamepadText::source(QString::fromStdString(r.source)));
    }
    for (int c = 0; c < 3; ++c) gamepad_tree_->resizeColumnToContents(c);
    gamepad_tree_->setMinimumHeight(std::min(320, 28 + 22 * static_cast<int>(gm.rows.size())));
    v->addWidget(gamepad_tree_);
    for (const auto& w : gm.warnings) v->addWidget(hintLabel(tr("Ignored: %1").arg(QString::fromStdString(w))));
    v->addWidget(hintLabel(tr("To change it, add lines like \"RightX = wheel\", \"LB = btn3\" or \"LT = none\" under [Gamepad] in your "
                              "game.ini (Patches > Open user folder); your lines win over the shipped ones. Controls: A, B, X, Y, LB, RB, "
                              "LT, RT, Start, Back, Guide, LS, RS, DPadUp/Down/Left/Right, LeftX, LeftY, RightX, RightY. Actions: wheel, "
                              "gas, brake, trigger, reload, grenade, aux1, aux2, start, coin, btn1 to btn8, key:<name>, none. See "
                              "docs/gamepad.md. Applies from the next start of the game.")));
    return box;
}

void PropertiesDialog::SaveGeneral() {
    rg::GameOverride ov = core_->library()->override_(game_.id);
    const QString name = name_edit_->text().trimmed();
    ov.name = (name == game_.name) ? QString() : name;
    ov.note = note_edit_->toPlainText().trimmed();
    if (template_combo_) ov.templateProfile = template_combo_->currentData().toString();
    core_->library()->setOverride(game_.id, ov);
}

QWidget* PropertiesDialog::MakeDongleGroup() {
    namespace gs = ravengun::gamesettings;
    auto* box = new QGroupBox(tr("Dongle"));
    auto* v = new QVBoxLayout(box);
    dongle_use_ = new QCheckBox(tr("Use dongle"));
    dongle_use_->setToolTip(tr("Use the real, physical dongle you own, passed through over USB. Before the game starts RavenGun checks that the "
                               "dongle is plugged in and that the maker's own runtime is running; if not, the game does not start. "
                               "RavenGun never emulates a dongle."));
    v->addWidget(dongle_use_);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    dongle_source_ = new QComboBox;
    for (const rg::DongleSource& s : rg::dongleSources()) dongle_source_->addItem(s.label, s.value);
    form->addRow(tr("Dongle source:"), dongle_source_);
    v->addLayout(form);
    auto* h = new QHBoxLayout;
    dongle_status_ = new QLabel;
    dongle_status_->setWordWrap(true);
    dongle_status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dongle_refresh_ = new QPushButton(tr("Refresh"));
    h->addWidget(dongle_status_, 1);
    h->addWidget(dongle_refresh_);
    v->addLayout(h);
    dongle_hint_ = new QLabel;
    dongle_hint_->setWordWrap(true);
    dongle_hint_->setOpenExternalLinks(true);
    dongle_hint_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    dongle_hint_->hide();
    v->addWidget(dongle_hint_);

    // Beginstand: de samengevoegde lagen (standaard uit, bron usb-passthrough).
    const bool writable = !ctx_.profile.isEmpty() && !ctx_.layers.user.empty();
    gs::Dongle d;
    if (!ctx_.profile.isEmpty()) d = gs::dongle_of(gs::load_game(ctx_.layers, ctx_.profile.toStdString()).settings);
    else d.source = "usb-passthrough";
    const QString src = QString::fromStdString(d.source);
    int idx = dongle_source_->findData(src);
    if (idx < 0) {   // een bron die deze versie niet kent: laten zien, niet stilletjes vervangen
        dongle_source_->addItem(src, src);
        idx = dongle_source_->count() - 1;
    }
    dongle_source_->setCurrentIndex(idx);
    dongle_use_->setChecked(d.enabled);
    if (!writable) {
        dongle_use_->setEnabled(false);
        dongle_use_->setToolTip(tr("This game has no settings folder yet."));
    }
    auto apply = [this, writable] {
        const bool on = dongle_use_->isChecked();
        dongle_source_->setEnabled(writable && on);
        dongle_status_->setEnabled(on);
        dongle_refresh_->setEnabled(on);
        if (!on) {
            ++dongle_request_;
            dongle_status_->clear();
            dongle_hint_->hide();
        }
    };
    apply();
    // Schrijft alleen verschillen met de meegeleverde laag (standaard: uit en usb-passthrough) naar de gebruikerslaag van game.ini.
    connect(dongle_use_, &QCheckBox::toggled, this, [this, apply](bool on) {
        apply();
        QString err;
        if (!rg::OptionStore::writeSetting(ctx_, QStringLiteral("Dongle"), QStringLiteral("Enabled"), on ? QStringLiteral("true") : QStringLiteral("false"),
                                           &err, QStringLiteral("false"))) {
            dongle_status_->setText(QStringLiteral("<span style='color:#c0392b'>%1</span>").arg(err.toHtmlEscaped()));
            return;
        }
        if (on) RefreshDongle();
    });
    connect(dongle_source_, &QComboBox::activated, this, [this](int i) {
        QString err;
        if (!rg::OptionStore::writeSetting(ctx_, QStringLiteral("Dongle"), QStringLiteral("Source"), dongle_source_->itemData(i).toString(), &err,
                                           QStringLiteral("usb-passthrough")))
            dongle_status_->setText(QStringLiteral("<span style='color:#c0392b'>%1</span>").arg(err.toHtmlEscaped()));
        else RefreshDongle();
    });
    connect(dongle_refresh_, &QPushButton::clicked, this, [this] { RefreshDongle(); });
    if (d.enabled) RefreshDongle();
    return box;
}

void PropertiesDialog::RefreshDongle() {
    if (!dongle_use_ || !dongle_use_->isChecked()) return;
    const int req = ++dongle_request_;
    dongle_status_->setText(tr("Checking for a dongle..."));
    dongle_hint_->hide();
    rg::detectDongle(this, [this, req](const rg::DongleStatus& st) {
        if (req != dongle_request_ || !dongle_use_->isChecked()) return;   // verouderd antwoord, of intussen uitgezet
        dongle_status_->setText(st.text());
        dongle_status_->setToolTip(st.tooltip());
        QString hint;
        if (st.valid && !st.ok) {
            if (st.dongles.isEmpty()) hint = tr("Plug in the dongle and press Refresh. The game will not start without it.");
            else if (st.dongles.first().runtimeStatus == QStringLiteral("not installed") && !st.dongles.first().installUrl.isEmpty())
                hint = tr("The maker's runtime (%1) is not installed. RavenGun does not install it; use the official installation: <a href=\"%2\">%2</a>")
                           .arg(st.dongles.first().runtimeName.toHtmlEscaped(), st.dongles.first().installUrl.toHtmlEscaped());
            else if (st.dongles.first().runtimeStatus == QStringLiteral("not running"))
                hint = tr("The maker's runtime (%1) is installed but not running. Start its service and press Refresh.").arg(st.dongles.first().runtimeName.toHtmlEscaped());
        }
        dongle_hint_->setText(hint);
        dongle_hint_->setVisible(!hint.isEmpty());
    });
}

QWidget* PropertiesDialog::MakeOptionsPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->addWidget(MakeDongleGroup());
    const rg::GameOptionsEntry* entry = core_->gameData()->entryFor(game_);
    const rg::GameOverride stored = core_->library()->override_(game_.id);
    const QString optionsError = core_->gameData()->optionsError(game_.profile.isEmpty() ? game_.templateProfile : game_.profile);
    if (!optionsError.isEmpty())
        v->addWidget(hintLabel(tr("The options file of this game could not be read, so there are no options: %1").arg(optionsError)));
    else if (!entry || entry->options.isEmpty())
        v->addWidget(hintLabel(tr("There are no options for this game in its options.json.")));
    struct OptRow {
        rg::GameOption opt;
        QWidget* widget;
        QString initial;
    };
    auto rows = std::make_shared<QList<OptRow>>();
    QStringList coveredFields;  // "Categorie/Veld" die al een optie hebben
    if (entry) {
        QMap<QString, QFormLayout*> groups;
        for (const rg::GameOption& o : entry->options) {
            const bool tpField = o.target.kind == rg::OptionTarget::Kind::TpField;
            if (tpField) coveredFields << o.target.category + QLatin1Char('/') + o.target.field;
            // Resolutie/venster en gamescope staan onder Display; de invoer-API regelt de app zelf.
            if ((tpField && (displayFieldRe().match(o.target.field).hasMatch() || managedField(o.target.field))) ||
                (o.target.kind == rg::OptionTarget::Kind::Env && o.target.field == QStringLiteral("TP_GAMESCOPE"))) {
                ++options_elsewhere_;
                continue;
            }
            const QString cat = groupOf(o);
            if (!groups.contains(cat)) {
                auto* box = new QGroupBox(cat);
                auto* form = new QFormLayout(box);
                form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
                groups.insert(cat, form);
                v->addWidget(box);
            }
            QFormLayout* form = groups.value(cat);
            QString reason;
            const bool writable = rg::OptionStore::writable(o, ctx_, &reason);
            // De bibliotheek is de bron van waarheid; anders de huidige waarde in het doelbestand (of de standaard).
            const QString value = stored.options.contains(o.key) ? stored.options.value(o.key).toString() : rg::OptionStore::read(o, ctx_);
            QWidget* widget = nullptr;
            if (o.type == QStringLiteral("bool")) {
                auto* c = new QCheckBox;
                c->setChecked(value.compare(o.trueValue, Qt::CaseInsensitive) == 0 || isTrue(value));
                widget = c;
            } else if (o.type == QStringLiteral("enum")) {
                auto* c = new QComboBox;
                for (int i = 0; i < o.values.size(); ++i) c->addItem(o.labels.value(i, o.values[i]), o.values[i]);
                int idx = c->findData(value);
                if (idx < 0) {
                    c->addItem(value, value);
                    idx = c->count() - 1;
                }
                c->setCurrentIndex(idx);
                widget = c;
            } else if (o.type == QStringLiteral("int")) {
                auto* s = new QSpinBox;
                s->setRange(o.hasRange ? static_cast<int>(o.min) : -1000000, o.hasRange ? static_cast<int>(std::min(o.max, 2.0e9)) : 1000000);
                s->setSingleStep(std::max(1, static_cast<int>(o.step)));
                s->setValue(value.toInt());
                widget = s;
            } else {
                widget = new QLineEdit(value);
            }
            QString tip = o.description;
            if (o.target.kind != rg::OptionTarget::Kind::Unsupported)
                tip += (tip.isEmpty() ? QString() : QStringLiteral("\n\n")) + tr("Written to: %1").arg(o.target.describe());
            widget->setToolTip(tip);
            widget->setEnabled(writable);
            QWidget* field = widget;
            if (!writable) {
                auto* box = new QWidget;
                auto* h = new QHBoxLayout(box);
                h->setContentsMargins(0, 0, 0, 0);
                h->addWidget(widget, 1);
                auto* why = new QLabel(QStringLiteral("<i>%1</i>").arg(reason.toHtmlEscaped()));
                why->setToolTip(rg::OptionStore::targetFile(o.target, ctx_));
                h->addWidget(why);
                field = box;
            }
            form->addRow(o.name + QLatin1Char(':'), field);
            rows->append({o, widget, value});
            ++options_shown_;
        }
    }

    // Overige velden van het launch-profiel die options.json (nog) niet noemt.
    TpFieldEditor* tpEditor = nullptr;
    if (!profilePath_.isEmpty()) {
        QList<rg::TpField> fields;
        for (const rg::TpField& f : profileFields())
            if (!displayFieldRe().match(f.name).hasMatch() && !managedField(f.name) &&
                !coveredFields.contains(f.category + QLatin1Char('/') + f.name))
                fields << f;
        if (!fields.isEmpty()) {
            auto* box = new QGroupBox(tr("More game settings"));
            auto* bl = new QVBoxLayout(box);
            tpEditor = new TpFieldEditor(profilePath_, fields, stored.tpFields);
            bl->addWidget(tpEditor);
            v->addWidget(box);
        }
    }

    auto* buttons = new QHBoxLayout;
    auto* status = new QLabel;
    status->setWordWrap(true);
    auto* apply = new NonDefaultQPushButton(tr("Apply"));
    auto* defaults = new NonDefaultQPushButton(tr("Restore Defaults"));
    defaults->setEnabled(!rows->isEmpty());
    buttons->addWidget(status, 1);
    buttons->addWidget(defaults);
    buttons->addWidget(apply);
    v->addLayout(buttons);
    v->addStretch(1);
    apply->setEnabled(!rows->isEmpty() || tpEditor);

    auto currentOf = [](const OptRow& r) -> QString {
        if (auto* c = qobject_cast<QCheckBox*>(r.widget)) return c->isChecked() ? r.opt.trueValue : r.opt.falseValue;
        if (auto* c = qobject_cast<QComboBox*>(r.widget)) return c->currentData().toString();
        if (auto* s = qobject_cast<QSpinBox*>(r.widget)) return QString::number(s->value());
        if (auto* e = qobject_cast<QLineEdit*>(r.widget)) return e->text();
        return r.initial;
    };
    connect(defaults, &QPushButton::clicked, this, [rows] {
        for (const OptRow& r : *rows) {
            if (!r.widget->isEnabled() || r.opt.defaultValue.isEmpty()) continue;
            if (auto* c = qobject_cast<QCheckBox*>(r.widget)) c->setChecked(r.opt.defaultValue == r.opt.trueValue || isTrue(r.opt.defaultValue));
            else if (auto* c = qobject_cast<QComboBox*>(r.widget)) c->setCurrentIndex(std::max(0, c->findData(r.opt.defaultValue)));
            else if (auto* s = qobject_cast<QSpinBox*>(r.widget)) s->setValue(r.opt.defaultValue.toInt());
            else if (auto* e = qobject_cast<QLineEdit*>(r.widget)) e->setText(r.opt.defaultValue);
        }
    });
    connect(apply, &QPushButton::clicked, this, [this, rows, status, tpEditor, currentOf] {
        // Eerst in de bibliotheek (bron van waarheid), dan naar de uitvoerbestanden van de launcher (ProfileSync).
        int changed = 0;
        rg::GameOverride ov = core_->library()->override_(game_.id);
        for (OptRow& r : *rows) {
            if (!r.widget->isEnabled()) continue;
            const QString val = currentOf(r);
            if (val == r.initial) continue;
            ov.options.insert(r.opt.key, val);
            r.initial = val;
            ++changed;
        }
        if (tpEditor) {
            const QVariantMap ch = tpEditor->changedValues();
            for (auto it = ch.begin(); it != ch.end(); ++it) ov.tpFields.insert(it.key(), it.value());
            changed += static_cast<int>(ch.size());
            tpEditor->markSaved();
        }
        if (changed) core_->library()->setOverride(game_.id, ov);
        QStringList errors;
        rg::ProfileSync::apply(game_, *core_->library(), *core_->gameData(), &errors);
        if (ctx_.ownProfile) core_->gameData()->reloadGame(ctx_.profile);   // de gebruikerslaag van game.ini is veranderd
        if (!errors.isEmpty())
            status->setText(QStringLiteral("<span style='color:#c0392b'>%1</span>").arg(errors.join(QStringLiteral("<br>")).toHtmlEscaped()));
        else status->setText(changed ? tr("Saved. The new settings apply the next time the game starts.") : tr("Nothing changed."));
    });
    return w;
}

QWidget* PropertiesDialog::MakeDisplayPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    const rg::GameOverride stored = core_->library()->override_(game_.id);
    TpFieldEditor* editor = nullptr;
    if (!profilePath_.isEmpty()) {
        QList<rg::TpField> fields;
        for (const rg::TpField& f : profileFields())
            if (displayFieldRe().match(f.name).hasMatch()) fields << f;
        auto* box = new QGroupBox(tr("Resolution and window"));
        auto* bl = new QVBoxLayout(box);
        if (fields.isEmpty()) {
            bl->addWidget(hintLabel(tr("This game has no resolution or window settings.")));
        } else {
            editor = new TpFieldEditor(profilePath_, fields, stored.tpFields);
            bl->addWidget(editor);
        }
        v->addWidget(box);
    } else if (game_.kind == rg::GameKind::Unity) {
        v->addWidget(hintLabel(tr("Unity games take their resolution from patches/%1/unity.json (a copy in your own patches folder wins).").arg(game_.profile)));
    } else {
        v->addWidget(hintLabel(tr("This game is not set up yet, so there are no display settings.")));
    }

    // Gamescope: opgeslagen in de bibliotheek, bij het starten naar UserProfiles/<P>.env (TP_GAMESCOPE) geschreven.
    QCheckBox* gamescope = nullptr;
    const QString envPath = ctx_.ownProfile ? ctx_.userGameIni() : ctx_.envPath();
    if (!profilePath_.isEmpty() && !envPath.isEmpty()) {
        auto* box = new QGroupBox(tr("Gamescope"));
        auto* bl = new QVBoxLayout(box);
        bool found = false;
        QString val;
        if (ctx_.ownProfile) {
            // game.ini: [Display] Gamescope (alle lagen); true = aan.
            const QString v = rg::OptionStore::readSetting(ctx_, QStringLiteral("Display"), QStringLiteral("Gamescope"), &found).trimmed().toLower();
            val = (v == QStringLiteral("true") || v == QStringLiteral("1") || v == QStringLiteral("yes") || v == QStringLiteral("on")) ? QStringLiteral("1") : QStringLiteral("0");
        } else {
            QFile f(envPath);
            QString text;
            if (f.open(QIODevice::ReadOnly)) text = QString::fromUtf8(f.readAll());
            val = rg::keyvalue::get(text, QStringLiteral("TP_GAMESCOPE"), &found);
        }
        if (stored.options.contains(QStringLiteral("display.gamescope"))) {
            val = stored.options.value(QStringLiteral("display.gamescope")).toString();
            found = true;
        }
        gamescope = new QCheckBox(tr("Run the game in gamescope (scales a windowed game to full screen)"));
        gamescope->setChecked(found && val == QStringLiteral("1"));
        const rg::WriteCheck wc = rg::checkWritable(envPath);
        gamescope->setEnabled(wc.ok);
        if (!wc.ok) gamescope->setToolTip(wc.reason);
        bl->addWidget(gamescope);
        v->addWidget(box);
    }

    // Camera: [Camera] Use in game.ini (de gebruikerslaag). Off = geen camera; game = de camera uit Settings > Display > "Camera for
    // games" in het spel (op de Wine-route via de virtuele camera van de daemon). Een andere waarde (presence, setup) blijft staan.
    if (!ctx_.profile.isEmpty() && !ctx_.layers.user.empty()) {
        auto initialCam = std::make_shared<QString>();
        auto* box = new QGroupBox(tr("Camera"));
        auto* bl = new QVBoxLayout(box);
        *initialCam = rg::OptionStore::readSetting(ctx_, QStringLiteral("Camera"), QStringLiteral("Use")).trimmed().toLower();
        if (initialCam->isEmpty()) *initialCam = QStringLiteral("none");
        auto* camera = camera_combo_ = new QComboBox;
        camera->addItem(tr("Off"), QStringLiteral("none"));
        camera->addItem(tr("Use the camera in the game"), QStringLiteral("game"));
        if (camera->findData(*initialCam) < 0) camera->addItem(*initialCam, *initialCam);
        camera->setCurrentIndex(camera->findData(*initialCam));
        const rg::WriteCheck wc = rg::checkWritable(ctx_.userGameIni());
        camera->setEnabled(wc.ok);
        if (!wc.ok) camera->setToolTip(wc.reason);
        bl->addWidget(camera);
        auto* camStatus = hintLabel(tr("Which camera is used is set in Settings > Display > Camera for games; \"None\" there turns the "
                                       "camera off for every game."));
        bl->addWidget(camStatus);
        v->addWidget(box);
        // Direct opslaan, zoals de route onder General: gelijk aan de meegeleverde waarde = de sleutel uit de gebruikerslaag.
        connect(camera, &QComboBox::activated, this, [this, camera, initialCam, camStatus](int) {
            const QString val = camera->currentData().toString();
            if (val == *initialCam) return;
            QString err;
            if (!rg::OptionStore::writeSetting(ctx_, QStringLiteral("Camera"), QStringLiteral("Use"), val, &err, QStringLiteral("none"))) {
                camStatus->setText(QStringLiteral("<span style='color:#c0392b'>%1</span>").arg(err.toHtmlEscaped()));
                camera->setCurrentIndex(camera->findData(*initialCam));
                return;
            }
            *initialCam = val;
            core_->gameData()->reloadGame(ctx_.profile);
            camStatus->setText(tr("Saved. The new setting applies the next time the game starts."));
        });
    }

    auto* buttons = new QHBoxLayout;
    auto* status = new QLabel;
    status->setWordWrap(true);
    auto* apply = new NonDefaultQPushButton(tr("Apply"));
    apply->setEnabled(editor || gamescope);
    buttons->addWidget(status, 1);
    buttons->addWidget(apply);
    v->addLayout(buttons);
    v->addStretch(1);
    auto initialGs = std::make_shared<bool>(gamescope && gamescope->isChecked());
    connect(apply, &QPushButton::clicked, this, [this, editor, gamescope, status, initialGs] {
        rg::GameOverride ov = core_->library()->override_(game_.id);
        if (editor) {
            const QVariantMap ch = editor->changedValues();
            for (auto it = ch.begin(); it != ch.end(); ++it) ov.tpFields.insert(it.key(), it.value());
            editor->markSaved();
        }
        if (gamescope && gamescope->isEnabled() && gamescope->isChecked() != *initialGs) {
            ov.options.insert(QStringLiteral("display.gamescope"), gamescope->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
            *initialGs = gamescope->isChecked();
        }
        core_->library()->setOverride(game_.id, ov);
        QStringList errors;
        rg::ProfileSync::apply(game_, *core_->library(), *core_->gameData(), &errors);
        if (ctx_.ownProfile) core_->gameData()->reloadGame(ctx_.profile);   // de gebruikerslaag van game.ini is veranderd
        status->setText(errors.isEmpty() ? tr("Saved. The new settings apply the next time the game starts.")
                                         : QStringLiteral("<span style='color:#c0392b'>%1</span>").arg(errors.join(QStringLiteral("; ")).toHtmlEscaped()));
    });
    return w;
}

QWidget* PropertiesDialog::MakeMemoryCardPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->addWidget(hintLabel(tr("Save data, NVRAM and high scores of this game. Only files on this PC can be deleted; files on the "
                              "NAS and files the game needs are shown read-only.")));
    if (const rg::GameOptionsEntry* e = core_->gameData()->entryFor(game_); e && !e->memoryNotes.isEmpty()) v->addWidget(hintLabel(e->memoryNotes));
    mc_tree_ = new QTreeWidget;
    mc_tree_->setHeaderLabels({tr("File"), tr("Type"), tr("Size"), tr("Modified"), tr("Location")});
    mc_tree_->setRootIsDecorated(false);
    mc_tree_->setAlternatingRowColors(true);
    mc_tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mc_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    v->addWidget(mc_tree_, 1);
    auto* h = new QHBoxLayout;
    auto* refresh = new NonDefaultQPushButton(tr("Refresh"));
    auto* open = new NonDefaultQPushButton(tr("Open Folder"));
    mc_delete_ = new NonDefaultQPushButton(tr("Delete..."));
    mc_delete_->setEnabled(false);
    h->addWidget(refresh);
    h->addWidget(open);
    h->addStretch(1);
    h->addWidget(mc_delete_);
    v->addLayout(h);
    connect(refresh, &QPushButton::clicked, this, &PropertiesDialog::RefreshMemoryCard);
    connect(mc_tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        bool any = false;
        for (QTreeWidgetItem* it : mc_tree_->selectedItems()) any = any || mc_files_.value(it->data(0, Qt::UserRole).toInt()).deletable;
        mc_delete_->setEnabled(any);
    });
    connect(open, &QPushButton::clicked, this, [this] {
        const auto sel = mc_tree_->selectedItems();
        const int i = sel.isEmpty() ? -1 : sel.first()->data(0, Qt::UserRole).toInt();
        const QString dir = i >= 0 ? QFileInfo(mc_files_.value(i).path).path() : (ctx_.viewDir.isEmpty() ? ctx_.rootDir : ctx_.viewDir);
        if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(mc_delete_, &QPushButton::clicked, this, [this] {
        QList<rg::MemoryCardFile> del;
        for (QTreeWidgetItem* it : mc_tree_->selectedItems()) {
            const rg::MemoryCardFile f = mc_files_.value(it->data(0, Qt::UserRole).toInt());
            if (f.deletable) del << f;
        }
        if (del.isEmpty()) return;
        QStringList lines;
        bool unknown = false;
        for (const auto& f : del) {
            lines << f.path + (f.eraseEffect.isEmpty() ? QString() : QStringLiteral("\n    ") + f.eraseEffect);
            unknown = unknown || f.safeToErase == QStringLiteral("unknown");
        }
        QString text = tr("Delete %n file(s)? This cannot be undone.", nullptr, static_cast<int>(del.size())) + QStringLiteral("\n\n") +
                       lines.join(QLatin1Char('\n'));
        if (unknown) text += tr("\n\nFor some files it is not known whether the game can recreate them. Make a backup first.");
        if (QMessageBox::warning(this, tr("Delete Save Data"), text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QStringList errors;
        for (const auto& f : del) {
            QString err;
            if (!rg::MemoryCard::remove(f, &err)) errors << err;
        }
        if (!errors.isEmpty()) QMessageBox::warning(this, tr("Delete Save Data"), errors.join(QLatin1Char('\n')));
        RefreshMemoryCard();
    });
    RefreshMemoryCard();
    return w;
}

void PropertiesDialog::RefreshMemoryCard() {
    QList<rg::MemoryCardSpec> specs;
    if (const rg::GameOptionsEntry* e = core_->gameData()->entryFor(game_)) specs = e->memoryCard;
    mc_files_ = rg::MemoryCard::list(ctx_, specs);
    mc_tree_->clear();
    const QLocale loc;
    for (int i = 0; i < mc_files_.size(); ++i) {
        const rg::MemoryCardFile& f = mc_files_[i];
        auto* it = new QTreeWidgetItem(mc_tree_);
        // Relatief aan de gamemap of de view (korter); het volledige pad staat in de tooltip.
        QString shown = f.path;
        for (const QString& base : {ctx_.viewDir, ctx_.rootDir})
            if (!base.isEmpty() && f.path.startsWith(base + QLatin1Char('/'))) {
                shown = QDir(base).relativeFilePath(f.path);
                break;
            }
        it->setText(0, shown);
        it->setToolTip(0, f.path + QStringLiteral("\n") + f.label + (f.eraseEffect.isEmpty() ? QString() : QStringLiteral("\n") + tr("If deleted: %1").arg(f.eraseEffect)));
        it->setText(1, f.kind.isEmpty() ? f.label : f.kind);
        it->setToolTip(1, f.label);
        it->setText(2, loc.formattedDataSize(f.size));
        it->setText(3, loc.toString(f.modified, QLocale::ShortFormat));
        it->setText(4, f.onNas ? tr("NAS (read-only)") : f.deletable ? tr("This PC") : tr("This PC (protected)"));
        if (!f.reason.isEmpty()) it->setToolTip(4, f.reason);
        it->setData(0, Qt::UserRole, i);
    }
    if (mc_files_.isEmpty()) {
        auto* it = new QTreeWidgetItem(mc_tree_);
        it->setText(0, tr("No save data found."));
        it->setFlags(Qt::NoItemFlags);
        it->setData(0, Qt::UserRole, -1);
    }
    for (int c = 1; c < 5; ++c) mc_tree_->resizeColumnToContents(c);
}

QWidget* PropertiesDialog::MakePatchesPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    const QString prof = ctx_.profile;
    const rg::GameData* data = core_->gameData();
    v->addWidget(hintLabel(tr("Patches change a copy of a file of the game runtime when the game starts (never the original, and never "
                              "a file on the NAS). Tick a patch to turn it on; your choice is saved in your own settings folder and "
                              "applies from the next start. Patches are applied by the direct start only.")));

    auto* box = new QGroupBox(tr("Patches"));
    auto* bl = new QVBoxLayout(box);
    patch_tree_ = new QTreeWidget;
    patch_tree_->setRootIsDecorated(false);
    patch_tree_->setHeaderLabels({tr("Patch"), tr("Category"), tr("Kind"), tr("Source")});
    patch_tree_->setUniformRowHeights(true);
    bl->addWidget(patch_tree_, 1);
    patch_desc_ = new QPlainTextEdit;
    patch_desc_->setReadOnly(true);
    patch_desc_->setMaximumHeight(110);
    patch_desc_->setPlaceholderText(tr("Select a patch to read why it exists."));
    bl->addWidget(patch_desc_);
    patch_status_ = hintLabel(QString());
    bl->addWidget(patch_status_);
    v->addWidget(box, 2);

    auto* sbox = new QGroupBox(tr("Settings (game.ini)"));
    auto* sl = new QVBoxLayout(sbox);
    sl->addWidget(hintLabel(tr("The settings that apply to this game: the defaults for its type, the settings shipped with RavenGun and "
                               "your own. They are read-only here; edit your own game.ini in your settings folder.")));
    settings_tree_ = new QTreeWidget;
    settings_tree_->setRootIsDecorated(false);
    settings_tree_->setHeaderLabels({tr("Section"), tr("Key"), tr("Value"), tr("From")});
    settings_tree_->setUniformRowHeights(true);
    sl->addWidget(settings_tree_, 1);
    v->addWidget(sbox, 2);

    auto* row = new QHBoxLayout;
    open_user_ = new QPushButton(tr("Open user folder"));
    open_user_->setToolTip(tr("Your own settings and patches for this game. They win over the shipped ones, key by key."));
    open_shipped_ = new QPushButton(tr("Open shipped folder"));
    open_shipped_->setToolTip(tr("The settings and patches that come with RavenGun (read-only)."));
    row->addWidget(open_user_);
    row->addWidget(open_shipped_);
    row->addStretch(1);
    v->addLayout(row);
    connect(open_user_, &QPushButton::clicked, this, [this, prof] {
        const QString dir = core_->gameData()->userGameDir(prof);
        if (dir.isEmpty()) return;
        QDir().mkpath(dir);   // zodat de bestandsbeheerder iets te openen heeft
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(open_shipped_, &QPushButton::clicked, this, [this, prof] {
        const QString dir = core_->gameData()->shippedGameDir(prof);
        if (!dir.isEmpty() && QFileInfo(dir).isDir()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    if (prof.isEmpty()) {
        patch_status_->setText(tr("Set up the game first: patches and settings belong to a game profile."));
        patch_tree_->setEnabled(false);
        open_user_->setEnabled(false);
        open_shipped_->setEnabled(false);
        return w;
    }
    open_shipped_->setEnabled(QFileInfo(data->shippedGameDir(prof)).isDir());
    open_user_->setEnabled(!data->userGameDir(prof).isEmpty());
    RefreshPatches();
    connect(patch_tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        patch_desc_->setPlainText(it ? it->data(0, Qt::UserRole + 1).toString() : QString());
    });
    connect(patch_tree_, &QTreeWidget::itemChanged, this, [this, prof](QTreeWidgetItem* it, int column) {
        if (column != 0 || refreshing_patches_) return;
        QString err;
        const bool on = it->checkState(0) == Qt::Checked;
        if (!core_->gameData()->setPatchEnabled(prof, it->data(0, Qt::UserRole).toString(), on, &err)) {
            QMessageBox::warning(this, tr("Patches"), tr("Could not save the choice: %1").arg(err));
        } else if (core_->launcher()->canStop() && core_->launcher()->runningProfile() == prof) {
            patch_status_->setText(tr("The game is running: the change applies from the next start."));
        }
        // Pas na dit signaal opnieuw opbouwen: clear() verwijdert het item dat itemChanged nog uitzendt (Qt gebruikt het daarna nog).
        QMetaObject::invokeMethod(this, [this] { RefreshPatches(); }, Qt::QueuedConnection);
    });
    return w;
}

void PropertiesDialog::RefreshPatches() {
    const QString prof = ctx_.profile;
    const rg::GameData* data = core_->gameData();
    refreshing_patches_ = true;
    const QString keep = patch_tree_->currentItem() ? patch_tree_->currentItem()->data(0, Qt::UserRole).toString() : QString();
    patch_tree_->clear();
    const QList<rg::PatchInfo> patches = data->patches(prof);
    QTreeWidgetItem* select = nullptr;
    for (const rg::PatchInfo& p : patches) {
        auto* it = new QTreeWidgetItem(patch_tree_);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(0, p.enabled ? Qt::Checked : Qt::Unchecked);
        it->setText(0, p.name);
        it->setText(1, p.category);
        it->setText(2, p.kind.isEmpty() ? tr("?") : p.appliedByCore ? tr("%1 (informational)").arg(p.kind) : p.kind);
        it->setText(3, p.userDefined ? tr("Yours") : p.userSet ? tr("Shipped (your choice)") : tr("Shipped"));
        it->setData(0, Qt::UserRole, p.name);
        QStringList lines;
        lines << (p.reason.isEmpty() ? tr("(no description)") : p.reason);
        if (!p.target.isEmpty()) lines << tr("Target: %1").arg(p.target);
        if (!p.version.isEmpty()) lines << tr("For game version: %1").arg(p.version);
        if (!p.source.isEmpty()) lines << tr("Source: %1").arg(p.source);
        if (p.appliedByCore) lines << tr("The OpenParrot core already applies this by itself; RavenGun does not apply it again. Shown for information only.");
        if (p.userSet) lines << tr("Shipped setting: %1.").arg(p.shippedEnabled ? tr("on") : tr("off"));
        if (p.kind != QStringLiteral("file") && !p.kind.isEmpty()) lines << tr("This kind of patch is not applied yet.");
        const QString text = lines.join(QStringLiteral("\n\n"));
        it->setData(0, Qt::UserRole + 1, text);
        for (int c = 0; c < 4; ++c) it->setToolTip(c, text);
        if (p.name == keep) select = it;
    }
    for (int c = 0; c < 4; ++c) patch_tree_->resizeColumnToContents(c);
    if (select) patch_tree_->setCurrentItem(select);
    const QStringList rejected = data->patchErrors(prof);
    if (!rejected.isEmpty()) patch_status_->setText(tr("Patches that were rejected (every patch needs a known Category):") + QLatin1Char('\n') + rejected.join(QLatin1Char('\n')));
    else if (patches.isEmpty()) patch_status_->setText(tr("This game has no patches."));
    else if (!patch_status_->text().contains(tr("running"))) patch_status_->setText(QString());
    refreshing_patches_ = false;

    settings_tree_->clear();
    for (const rg::SettingEntry& e : data->settings(prof)) {
        auto* it = new QTreeWidgetItem(settings_tree_);
        it->setText(0, e.section);
        it->setText(1, e.key);
        it->setText(2, e.value);
        it->setText(3, e.layer == QStringLiteral("user") ? tr("Yours") : e.layer == QStringLiteral("shipped") ? tr("Shipped") : tr("Type default"));
        it->setToolTip(2, e.value);
    }
    for (int c = 0; c < 4; ++c) settings_tree_->resizeColumnToContents(c);
}

QWidget* PropertiesDialog::MakeInfoPane() {
    auto* w = new QWidget;
    auto* form = new QFormLayout(w);
    const QString prof = ctx_.profile;
    QString emulationProfile, is64;
    if (ctx_.ownProfile) {
        emulationProfile = core_->gameData()->profileValue(prof, QStringLiteral("EmulationProfile"));
        is64 = core_->gameData()->profileValue(prof, QStringLiteral("Is64Bit"));
    } else if (!profilePath_.isEmpty()) {
        rg::TpProfile p;
        if (p.load(profilePath_)) {
            emulationProfile = p.tag(QStringLiteral("EmulationProfile"));
            is64 = p.tag(QStringLiteral("Is64Bit"));
        }
    }
    const rg::RouteInfo ri = core_->gameData()->route(prof);
    const rg::GameOptionsEntry* e = core_->gameData()->entryFor(game_);
    auto add = [form](const QString& k, const QString& v) {
        auto* l = new QLabel(v.isEmpty() ? QStringLiteral("-") : v);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        form->addRow(k, l);
    };
    add(tr("Name:"), game_.displayName());
    add(tr("Platform:"), e && !e->platform.isEmpty() ? e->platform : game_.platform);
    add(tr("Genre:"), game_.genre);
    add(tr("Lightgun game:"), game_.gunGame ? tr("Yes") : tr("No / unknown"));
    add(tr("I/O board:"), !ri.board.isEmpty() ? ri.board : !ri.layout.isEmpty() ? ri.layout : emulationProfile);
    add(tr("Input route:"), rg::GameData::routeLabel(ri.route));
    add(tr("Status:"), core_->gameListModel()->statusText(game_));
    add(tr("Notes:"), core_->gameData()->note(prof));
    add(tr("Game folder:"), ctx_.rootDir + (rg::isOnNetwork(ctx_.rootDir) ? tr("  (NAS)") : QString()));
    add(tr("Executable:"), game_.exePath);
    add(tr("Local view:"), ctx_.viewDir);
    // Technische gegevens van de runtime (TeknoParrot), alleen hier en in Settings > Paths.
    add(tr("TeknoParrot emulator:"), game_.emulator + (is64.isEmpty() ? QString() : (is64 == QStringLiteral("true") ? tr(" (64-bit)") : tr(" (32-bit)"))));
    add(tr("TeknoParrot profile:"), prof.isEmpty() ? QString() : prof + (profilePath_.isEmpty() ? tr(" (no user profile yet)") : QStringLiteral(" - ") + profilePath_));
    add(tr("TeknoParrot emulation profile:"), emulationProfile);
    add(tr("Launcher:"), core_->launcher()->backend() ? core_->launcher()->backend()->id() : QString());
    add(tr("Library ID:"), game_.id);
    return w;
}

}  // namespace rg::ui

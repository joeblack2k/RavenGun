// SPDX-License-Identifier: GPL-3.0-or-later
// OLED-bescherming (`ravengun --saver`): schermvullend zwart venster zonder rand, met een statusbord in gewone taal.
// Wie de kamer binnenloopt ziet in een oogopslag: draait er nu een spel, welk spel wordt getest, gaat het goed of
// slecht, de hoeveelste stap is het, waar dient de test voor, wat is al gedaan en wat nog niet.
//
// Het bord komt uit twee kleine bestanden onder ~/.local/state/ravengun/ (XDG_STATE_HOME):
//   saver-status.txt  de stand van het werk; de coordinator houdt het bij (scripts/rg-saver-status.sh).
//   saver-game.txt    draait er nu een spel; scripts/oled-saver.sh schrijft het elke paar seconden.
//
// Formaat van saver-status.txt: `sleutel: waarde` per regel, UTF-8; lege regels en regels met # tellen niet.
//   spel:  Halo                                   verplicht
//   bezig: Richten en schieten testen             verplicht  (wat er nu gebeurt, in gewone taal)
//   stand: goed | slecht | bezig                  verplicht  (bezig = nog geen uitslag)
//   stap:  3 van 5                                optioneel  (ook 3/5)
//   doel:  Zien of de trekker in het spel aankomt optioneel  (waar de test voor dient)
//   klaar: Beeld op de tv; Munt en start          optioneel  (met ; ertussen)
//   nog:   Twee minuten spelen                    optioneel  (met ; ertussen)
//   gehaald: Rabbids; Luigi's Mansion             optioneel  (spellen die 5 minuten attract of spel haalden; met ;)
//   vraag: Wil je Halo met of zonder kalibratie? optioneel  (een vraag voor de eigenaar: grote pulserende balk bovenaan)
//   doelen: Alle 13 spellen halen; Wiki bijwerken optioneel  (de grotere doelen, klein getoond; met ;)
// Daarnaast saver-feed.txt: een regel per gebeurtenis, `2026-10-14T21:03:11 tekst` (scripts/rg-saver-status.sh --feed);
// het bord toont de nieuwste paar met hun tijd. Ontbreekt dat bestand, dan is er gewoon geen activiteitenlijst.
//
// Een verkeerd gelezen status is erger dan geen status. Ontbreekt het bestand, mist er een verplichte sleutel of
// staat er iets in dat niet klopt, dan toont het bord DAT, met de reden -- nooit iets dat op "alles goed" lijkt.
// Een status die lang niet is bijgewerkt wordt als oud gemeld.
//
// Alles springt elke kMoveIntervalMs naar een nieuwe plek zodat er niets inbrandt. Geen engine, geen hoofdvenster,
// geen tray, geen browserbalk.
#pragma once
#include <QElapsedTimer>
#include <QLabel>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

namespace rg::ui {

struct SaverStatus {
    bool ok = false;
    QString error;            // gewone taal; gevuld als ok == false
    QString game, doing, state, purpose;
    int step = 0, steps = 0;  // 0 = niet opgegeven
    QStringList done, todo, passed, goals;
    QString question;         // leeg = geen vraag voor de eigenaar
    qint64 updated = 0;       // laatste wijziging van het bestand (seconden sinds epoch); 0 = onbekend
};

// Draait er nu een spel? Dat is iets anders dan "het spel is gehaald" (5 minuten attract of spel, niet het
// beginscherm: dat is vaak de kalibratie); het bord zegt er daarom bij dat het nog geen uitslag is. Unknown = we weten het niet (het bestand ontbreekt of is oud): dat wordt ook zo getoond.
// Een gebeurtenis uit saver-feed.txt.
struct SaverFeedItem {
    qint64 time = 0;   // seconden sinds epoch
    QString text;
};

enum class SaverGame { Unknown, None, Visible, Hidden };

class SaverWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMoveIntervalMs = 12000;
    static constexpr int kStaleStatusSecs = 30 * 60;   // daarna heet de status oud
    static constexpr int kFeedKeep = 50;               // zoveel regels houdt het script in saver-feed.txt
    static constexpr int kFeedShown = 7;               // zoveel toont het bord
    static constexpr int kStaleGameSecs = 20;          // saver-game.txt wordt elke paar seconden geschreven
    explicit SaverWindow(QWidget* parent = nullptr);

    // Schermvullend op het primaire scherm tonen (muisaanwijzer verborgen).
    void Open();
    // De bestanden opnieuw inlezen, het bord bijwerken en het naar een willekeurige plek zetten.
    void Reposition();
    // Voor tests en `--saver-snapshot`: het bord op `size` tonen (midden, zonder verspringen, pulsfase vast) en als PNG bewaren.
    bool SaveSnapshot(const QString& png, const QSize& size);
    QString ClockText() const { return clock_->text(); }
    QRect ClockGeometry() const { return panel_->geometry(); }
    QStringList ShownLines() const { return shown_; }

    // --- zonder venster te gebruiken (ook door `ravengun --saver-check` en de tests) ---
    static QString StatusPath();   // <state>/ravengun/saver-status.txt
    static QString GamePath();     // <state>/ravengun/saver-game.txt
    static QString FeedPath();     // <state>/ravengun/saver-feed.txt
    // De nieuwste `max` regels van de feed, nieuwste eerst. Regels die niet kloppen worden overgeslagen.
    static QList<SaverFeedItem> ParseFeed(const QString& text, int max);
    static QList<SaverFeedItem> LoadFeed(const QString& path, int max);
    // De tekst van een statusbestand ontleden. Streng: alles wat niet klopt geeft ok == false met de reden.
    static SaverStatus ParseStatus(const QString& text);
    // Het bestand lezen; ontbreekt het of is het niet te openen, dan ok == false met de reden.
    static SaverStatus LoadStatus(const QString& path);
    static SaverGame ParseGame(const QString& text);
    static SaverGame LoadGame(const QString& path, qint64 now);
    // Het bord in gewone taal, een regel per element (kop eerst). `now` in seconden sinds epoch.
    static QStringList Lines(const SaverStatus& s, SaverGame g, qint64 now);

private:
    QWidget* panel_;     // het bord (getekend door SaverPanel)
    QLabel* clock_;      // losse klok-tekst voor ClockText() in tests
    QElapsedTimer clockStart_;
    QStringList shown_;  // wat er nu staat, als gewone regels
    void Layout(bool center);
    void Pulse();

    QTimer timer_;
    QTimer pulseTimer_;   // alleen actief als er een vraag staat
};

}  // namespace rg::ui

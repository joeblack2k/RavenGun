// Instellingen van de plugin, bewaard door BepInEx in BepInEx/config/<GUID>.cfg (tekstbestand; de view-opbouw van
// tools/dev/aagames-setup.py zet de waarden uit packaging/aagames-games.json erin).
using System;
using BepInEx.Configuration;
using UnityEngine;

namespace RavenGun.Unity
{
    public enum CrosshairMode
    {
        FromDaemon,   // volg het veld HideCrosshairs van de daemon
        Show,         // vizier van de game altijd tonen
        Hide          // vizier van de game altijd verbergen
    }

    public sealed class Settings
    {
        public readonly ConfigEntry<int> Port;
        public readonly ConfigEntry<int> InputTimeoutMs;
        public readonly ConfigEntry<bool> MouseFallback;

        public readonly ConfigEntry<bool> ForceResolution;
        public readonly ConfigEntry<int> Width;
        public readonly ConfigEntry<int> Height;
        public readonly ConfigEntry<bool> Fullscreen;
        public readonly ConfigEntry<bool> HideMouseCursor;

        public readonly ConfigEntry<CrosshairMode> Crosshair;

        public readonly ConfigEntry<bool> DisableIoBoard;
        public readonly ConfigEntry<bool> SuppressGunErrors;
        public readonly ConfigEntry<bool> KeyboardEnabled;
        public readonly ConfigEntry<bool> Verbose;

        public readonly ConfigEntry<string>[] KeyCoin = new ConfigEntry<string>[4];
        public readonly ConfigEntry<string>[] KeyStart = new ConfigEntry<string>[4];
        public readonly ConfigEntry<string> KeyTest;
        public readonly ConfigEntry<string> KeyService;
        public readonly ConfigEntry<string> KeyMenuUp;
        public readonly ConfigEntry<string> KeyMenuDown;
        public readonly ConfigEntry<string> KeyMenuSelect;

        public Settings(ConfigFile c)
        {
            Port = c.Bind("Kanaal", "Poort", 33610, "TCP-poort op 127.0.0.1 waar de daemon (ravengund) verbinding mee maakt. De server luistert nooit op een ander adres.");
            InputTimeoutMs = c.Bind("Kanaal", "InvoerTimeoutMs", 1500, "Komt er zolang geen pakket van de daemon, dan telt de invoer als neutraal (triggers los) en valt de game terug op de muis.");
            MouseFallback = c.Bind("Kanaal", "MuisAlsTerugval", true, "Zonder verbonden daemon werkt de game zoals bij de muis: linkermuisknop is de trekker.");

            ForceResolution = c.Bind("Scherm", "ResolutieAfdwingen", true, "Zet de resolutie van het gamevenster op Breedte x Hoogte (anders doet de game zelf zijn keuze).");
            Width = c.Bind("Scherm", "Breedte", 1920, "Breedte van het gamevenster in pixels. Dit is ook de schaal van de aim-pixels die de daemon stuurt.");
            Height = c.Bind("Scherm", "Hoogte", 1080, "Hoogte van het gamevenster in pixels.");
            Fullscreen = c.Bind("Scherm", "Beeldvullend", true, "Beeldvullend scherm (samen met de opstartoptie -popupwindow een randloos venster).");
            HideMouseCursor = c.Bind("Scherm", "MuisaanwijzerVerbergen", true, "Verberg de aanwijzer van het besturingssysteem zolang de daemon de aim levert.");

            Crosshair = c.Bind("Vizier", "Modus", CrosshairMode.FromDaemon, "FromDaemon: volg de daemon, Show: altijd tonen, Hide: altijd verbergen. Het verbergen is alleen visueel; schieten en richten blijven werken.");

            DisableIoBoard = c.Bind("Bord", "IOBordUitschakelen", true, "Zet het I/O-bord van de game uit en vervang het door dit kanaal en het toetsenbord (geen IO BOARD ERROR). Alleen op false zetten op een echte kast.");
            SuppressGunErrors = c.Bind("Bord", "GeweerFoutenOnderdrukken", true, "Onderdruk de fatale geweerfout van de game (ArcadeManager.GunFatalError, die de game na een tijd afsluit). Er is geen echt geweer: de aim komt uit de daemon.");
            KeyboardEnabled = c.Bind("Toetsen", "ToetsenbordActief", true, "Munt, start, test, service en menu via het toetsenbord (dit is ook hoe de virtuele toetsen van de daemon aankomen).");
            Verbose = c.Bind("Log", "Uitgebreid", false, "Extra logregels (verbindingen, toestandswijzigingen).");

            string[] coin = { "Alpha5", "Alpha6", "Alpha7", "Alpha8" };
            string[] start = { "Alpha1", "Alpha2", "Alpha3", "Alpha4" };
            for (int i = 0; i < 4; i++)
            {
                KeyCoin[i] = c.Bind("Toetsen", "Munt" + (i + 1), coin[i], "Unity-toetsnaam (KeyCode) voor een munt van speler " + (i + 1) + ".");
                KeyStart[i] = c.Bind("Toetsen", "Start" + (i + 1), start[i], "Unity-toetsnaam voor de startknop van speler " + (i + 1) + ".");
            }
            KeyTest = c.Bind("Toetsen", "Test", "Alpha0", "Testknop (operatormenu van de game).");
            KeyService = c.Bind("Toetsen", "Service", "Alpha9", "Servicetoets: geeft speler 1 een gratis credit (zoals de servicetoets op een kast).");
            KeyMenuUp = c.Bind("Toetsen", "MenuOmhoog", "UpArrow", "Menu omhoog (operatormenu).");
            KeyMenuDown = c.Bind("Toetsen", "MenuOmlaag", "DownArrow", "Menu omlaag (operatormenu).");
            KeyMenuSelect = c.Bind("Toetsen", "MenuKies", "Return", "Menu kiezen (operatormenu).");
        }

        // Toetsnaam naar KeyCode; None bij een onbekende naam.
        public static KeyCode Parse(string name)
        {
            if (string.IsNullOrEmpty(name)) return KeyCode.None;
            try { return (KeyCode)Enum.Parse(typeof(KeyCode), name.Trim(), true); }
            catch (Exception) { return KeyCode.None; }
        }
    }
}

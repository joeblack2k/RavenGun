// Gemeenschappelijke kern van de RavenGun-plugin: eigenaar van de server, de instellingen, het toetsenbord, het scherm
// en de uitvoer naar de daemon. De game-specifieke delen zitten achter IGame.
//
// Foutafhandeling: niets in deze klasse mag de game laten crashen. Elke tik is omgeven met try/catch; een fout wordt
// één keer per soort gelogd (daarna een korte tijd stil) en de game loopt door. Zonder daemon valt de game terug op de muis.
using System;
using System.Collections.Generic;
using BepInEx.Logging;
using UnityEngine;

namespace RavenGun.Unity
{
    public static class Core
    {
        // Waarden van de enum KaboomButton.Button van de games (identiek in Tomb Raider en Rabbids Hollywood).
        public const int BtnOperatorPress = 1, BtnOperatorRelease = 5;
        public const int BtnUpPress = 2, BtnUpRelease = 6;
        public const int BtnDownPress = 3, BtnDownRelease = 7;
        public const int BtnSelectPress = 4, BtnSelectRelease = 8;
        public static int BtnStartPress(int player) { return 9 + 4 * player; }
        public static int BtnStartRelease(int player) { return 11 + 4 * player; }

        public static ManualLogSource Log;
        public static Settings Cfg;
        public static IGame Game;

        static Server server;
        static readonly InputState snap = new InputState();
        static readonly OutputState lastSent = new OutputState();
        static readonly OutputState cur = new OutputState();
        static bool daemonActive;
        static bool lastHidden;
        static float nextCrosshair, nextResCheck, nextStatus;
        static int resTries;
        static int frame;
        static readonly Dictionary<string, float> errorQuiet = new Dictionary<string, float>();

        // De game moet de aim en knoppen van de daemon gebruiken (verbonden, verse pakketten, invoer ingeschakeld).
        public static bool DaemonActive { get { return daemonActive; } }

        public static void Init(ManualLogSource log, Settings cfg, IGame game)
        {
            Log = log; Cfg = cfg; Game = game;
            log.LogMessage("RavenGun-plugin voor " + game.Name + " (kanaalprotocol versie 1)");
            server = new Server(game.Variant, cfg.Port.Value, delegate (string m) { Log.LogMessage(m); },
                                delegate (string m) { if (Cfg.Verbose.Value) Log.LogInfo(m); });
            server.Start(delegate ()
            {
                // Bij elke nieuwe verbinding de huidige stand meesturen (zonder eenmalige vlaggen).
                OutputState o = new OutputState();
                o.CopyFrom(cur);
                Array.Clear(o.Recoil, 0, 4);
                Array.Clear(o.Damaged, 0, 4);
                return Protocol.BuildOutput(Game.Variant, o);
            });
            lastHidden = false;
            ApplyResolution(true);
        }

        public static void Shutdown()
        {
            try { if (server != null) server.Stop(); } catch (Exception) { }
        }

        static void Fail(string where, Exception e)
        {
            float now = Time.realtimeSinceStartup;
            float until;
            if (errorQuiet.TryGetValue(where, out until) && now < until) return;
            errorQuiet[where] = now + 30f;
            Log.LogError(where + ": " + e.GetType().Name + ": " + e.Message + " (30 s stil voor deze melding)");
            if (Cfg.Verbose.Value) Log.LogError(e.ToString());
        }

        // Elke Unity-frame, vanuit Update() van de plugin (hoofdthread).
        public static void Tick()
        {
            frame++;
            try { UpdateInput(); } catch (Exception e) { Fail("invoer", e); }
            try { if (Cfg.KeyboardEnabled.Value) Keys(); } catch (Exception e) { Fail("toetsen", e); }
            try { UpdateStatus(); } catch (Exception e) { Fail("uitvoer", e); }
            try { Screen_(); } catch (Exception e) { Fail("scherm", e); }
            try { Crosshair_(); } catch (Exception e) { Fail("vizier", e); }
        }

        static void UpdateInput()
        {
            long age = server.Snapshot(snap);
            bool fresh = server.Connected && age >= 0 && age <= Cfg.InputTimeoutMs.Value;
            bool active = fresh && snap.Enable;
            if (active != daemonActive)
            {
                daemonActive = active;
                Log.LogMessage(active ? "Invoer van de daemon actief." :
                    (server.Connected ? "Invoer van de daemon niet (meer) actief (geen pakketten of invoer uit)." : "Geen daemon verbonden."));
                if (!active) { try { Game.ReleaseGuns(); } catch (Exception e) { Fail("loslaten", e); } }
            }
        }

        // Pakketgegevens voor de game-implementatie (alleen geldig terwijl DaemonActive).
        public static InputState Input { get { return snap; } }

        // ---- Toetsenbord ----
        static readonly KeyCode[] kCoin = new KeyCode[4];
        static readonly KeyCode[] kStart = new KeyCode[4];
        static KeyCode kTest, kService, kUp, kDown, kSelect;
        static bool keysParsed;

        static void ParseKeys()
        {
            for (int i = 0; i < 4; i++) { kCoin[i] = Settings.Parse(Cfg.KeyCoin[i].Value); kStart[i] = Settings.Parse(Cfg.KeyStart[i].Value); }
            kTest = Settings.Parse(Cfg.KeyTest.Value);
            kService = Settings.Parse(Cfg.KeyService.Value);
            kUp = Settings.Parse(Cfg.KeyMenuUp.Value);
            kDown = Settings.Parse(Cfg.KeyMenuDown.Value);
            kSelect = Settings.Parse(Cfg.KeyMenuSelect.Value);
            keysParsed = true;
        }

        static void Edge(KeyCode k, int press, int release)
        {
            if (k == KeyCode.None) return;
            if (Input_GetKeyDown(k)) Game.PressButton(press);
            else if (Input_GetKeyUp(k)) Game.PressButton(release);
        }

        static bool Input_GetKeyDown(KeyCode k) { return UnityEngine.Input.GetKeyDown(k); }
        static bool Input_GetKeyUp(KeyCode k) { return UnityEngine.Input.GetKeyUp(k); }

        static void Keys()
        {
            if (!keysParsed) ParseKeys();
            for (int i = 0; i < 4; i++)
            {
                if (kCoin[i] != KeyCode.None && Input_GetKeyDown(kCoin[i])) Game.AddCoin(i);
                Edge(kStart[i], BtnStartPress(i), BtnStartRelease(i));
            }
            if (kService != KeyCode.None && Input_GetKeyDown(kService)) Game.AddCoin(0);
            Edge(kTest, BtnOperatorPress, BtnOperatorRelease);
            Edge(kUp, BtnUpPress, BtnUpRelease);
            Edge(kDown, BtnDownPress, BtnDownRelease);
            Edge(kSelect, BtnSelectPress, BtnSelectRelease);
        }

        // ---- Uitvoer naar de daemon ----
        // Een schot of treffer krijgt altijd een eigen bericht, zodat de daemon ze telt.
        public static void NoteRecoil(int player)
        {
            if (player < 0 || player > 3) return;
            Flag(true, player);
        }

        public static void NoteDamaged(int player)
        {
            if (player < 0 || player > 3) return;
            Flag(false, player);
        }

        static void Flag(bool recoil, int player)
        {
            try
            {
                OutputState o = new OutputState();
                o.CopyFrom(cur);
                Array.Clear(o.Recoil, 0, 4);
                Array.Clear(o.Damaged, 0, 4);
                if (recoil) o.Recoil[player] = 1; else o.Damaged[player] = 1;
                server.Send(Protocol.BuildOutput(Game.Variant, o));
            }
            catch (Exception e) { Fail("vlag", e); }
        }

        // Lampstand van de startknop (Rabbids) of "speelt" (Tomb Raider) kan ook vanuit game-hooks komen.
        public static void SetPlaying(int player, bool on)
        {
            if (player < 0 || player > 3) return;
            cur.Playing[player] = (byte)(on ? 1 : 0);
        }

        static void UpdateStatus()
        {
            float now = Time.realtimeSinceStartup;
            if (now < nextStatus) return;
            nextStatus = now + 0.05f;   // 20 Hz is ruim genoeg voor credits, ammo en leven
            OutputState prev = new OutputState();
            prev.CopyFrom(cur);
            Game.PollStatus(cur);
            if (!cur.SameStatus(lastSent))
            {
                lastSent.CopyFrom(cur);
                OutputState o = new OutputState();
                o.CopyFrom(cur);
                Array.Clear(o.Recoil, 0, 4);
                Array.Clear(o.Damaged, 0, 4);
                server.Send(Protocol.BuildOutput(Game.Variant, o));
                if (Cfg.Verbose.Value) Log.LogInfo("Stand: credits " + cur.Credits[0] + "," + cur.Credits[1] + " ammo " + cur.Ammo[0] + " leven " + cur.Life[0] + " speelt " + cur.Playing[0]);
            }
        }

        // ---- Scherm ----
        static void ApplyResolution(bool first)
        {
            if (!Cfg.ForceResolution.Value) return;
            int w = Cfg.Width.Value, h = Cfg.Height.Value;
            if (w < 320 || h < 240) return;
            Log.LogMessage("Resolutie " + w + "x" + h + (Cfg.Fullscreen.Value ? " beeldvullend" : " in venster") + " instellen (nu " + Screen.width + "x" + Screen.height + ", beeldvullend=" + Screen.fullScreen + ")");
            Screen.SetResolution(w, h, Cfg.Fullscreen.Value);
        }

        static void Screen_()
        {
            float now = Time.realtimeSinceStartup;
            if (Cfg.ForceResolution.Value && now >= nextResCheck)
            {
                nextResCheck = now + 3f;
                // De game kan zijn eigen keuze terugzetten (laden van de configuratie); een paar keer herstellen.
                if (resTries < 5 && (Screen.width != Cfg.Width.Value || Screen.height != Cfg.Height.Value))
                {
                    resTries++;
                    ApplyResolution(false);
                }
            }
            bool hide = Cfg.HideMouseCursor.Value && daemonActive;
            if (Cursor.visible == hide) Cursor.visible = !hide;
        }

        // ---- Vizier ----
        public static bool CrosshairHidden()
        {
            switch (Cfg.Crosshair.Value)
            {
                case CrosshairMode.Show: return false;
                case CrosshairMode.Hide: return true;
                default: return daemonActive && snap.HideCrosshairs;
            }
        }

        static void Crosshair_()
        {
            float now = Time.realtimeSinceStartup;
            if (now < nextCrosshair) return;
            nextCrosshair = now + 0.2f;   // de game maakt vizieren soms opnieuw (nieuw level); dus steeds opnieuw toepassen
            bool hide = CrosshairHidden();
            if (hide || lastHidden) Game.SetCrosshairHidden(hide);
            lastHidden = hide;
        }
    }
}

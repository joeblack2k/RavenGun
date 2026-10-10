// RavenGun-plugin voor Tomb Raider Arcade (Adrenaline Amusements, Unity 2017/2018, Mono).
//
// Wat de game hier doet (zelf onderzocht in Assembly-CSharp.dll, alleen gelezen):
//  - KaboomManager is de brug naar het I/O-bord; KaboomAdrioFxPlusFourFeeders._Initialize opent het bord via een native
//    bibliotheek. ArcadeManager.CheckIOBoard toont een foutscherm als KaboomManager.IsKaboomConnected() onwaar is.
//  - InputManager.MouseControl is de invoer zonder echt geweer: viewportpositie van de muis naar PlayerManager
//    (PlayerMoveCrosshair, PlayerPressTrigger, PlayerShoot, PlayerTriggerRelease, PlayerReload).
//  - KaboomManager.StartMotorPwm is de recoil (trilling) naar het bord.
//  - KaboomManager.KaboomOnKeyboardEvent en KaboomOnCoinEvent zijn de gebeurtenissen die het bord zou melden.
// Deze plugin vervangt het bord door dit kanaal en het toetsenbord en stuurt MouseControl vanuit de daemon.
using System;
using System.Collections.Generic;
using System.Reflection;
using BepInEx;
using HarmonyLib;
using SBK;
using UnityEngine;

namespace RavenGun.Unity.TombRaider
{
    [BepInPlugin("nl.studiodamon.ravengun.tombraider", "RavenGun Tomb Raider Arcade", "1.0.0")]
    public class Plugin : BaseUnityPlugin
    {
        Harmony harmony;

        void Awake()
        {
            try
            {
                Settings cfg = new Settings(Config);
                Core.Init(Logger, cfg, new TombRaiderGame());
                harmony = new Harmony("nl.studiodamon.ravengun.tombraider");
                Patches.Apply(harmony, cfg);
            }
            catch (Exception e)
            {
                Logger.LogError("Start van de RavenGun-plugin mislukt: " + e + " (de game loopt door zonder plugin)");
            }
        }

        void Update()
        {
            if (Core.Game != null) Core.Tick();
        }

        void OnDestroy()
        {
            Core.Shutdown();
        }
    }

    static class Patches
    {
        static readonly int[] lastRecoilFrame = { -1, -1, -1, -1 };
        static FieldInfo playerIdField;

        static void P(Harmony h, Type t, string name, string pre, string post, Type[] args = null)
        {
            try
            {
                MethodBase m = args == null ? AccessTools.Method(t, name) : AccessTools.Method(t, name, args);
                if (m == null) { Core.Log.LogWarning("Patch overgeslagen: " + t.Name + "." + name + " bestaat niet"); return; }
                HarmonyMethod prefix = pre == null ? null : new HarmonyMethod(typeof(Patches), pre);
                HarmonyMethod postfix = post == null ? null : new HarmonyMethod(typeof(Patches), post);
                h.Patch(m, prefix, postfix);
                if (Core.Cfg.Verbose.Value) Core.Log.LogInfo("Patch: " + t.Name + "." + name);
            }
            catch (Exception e) { Core.Log.LogError("Patch " + t.Name + "." + name + " mislukt: " + e.Message); }
        }

        public static void Apply(Harmony h, Settings cfg)
        {
            playerIdField = typeof(Player).GetField("m_ID", Util.AnyInstance);
            // Invoer van de daemon in plaats van muis of geweer.
            P(h, typeof(InputManager), "MouseControl", "MouseControl_Prefix", null);
            // Recoil en treffers naar de daemon.
            P(h, typeof(KaboomManager), "StartMotorPwm", "StartMotorPwm_Prefix", null);
            P(h, typeof(Player), "ReceiveDamage", null, "ReceiveDamage_Postfix");
            if (cfg.DisableIoBoard.Value)
            {
                // Het bord niet openen (geen native bibliotheek aanroepen) en de bordcontrole van de game overslaan.
                P(h, typeof(KaboomAdrioFxPlusFourFeeders), "_Initialize", "Skip_Prefix", null);
                P(h, typeof(ArcadeManager), "CheckIOBoard", "Skip_Prefix", null);
            }
            if (cfg.Verbose.Value) Util.TraceQuit(h, typeof(KaboomManager), typeof(ArcadeManager), typeof(OperatorPopup));
            if (cfg.SuppressGunErrors.Value)
            {
                // Zonder echt geweer meldt de game een fatale geweerfout (en sluit zichzelf daarna af); deze invoer komt uit de daemon.
                P(h, typeof(ArcadeManager), "GunFatalError", "Skip_Prefix", null);
            }
        }

        static bool Skip_Prefix() { return false; }

        static bool MouseControl_Prefix()
        {
            if (!Core.DaemonActive) return true;   // zonder daemon: de muis van de game zelf
            try { Core.Game.DriveGuns(Core.Input); }
            catch (Exception e) { Core.Log.LogError("DriveGuns: " + e.Message); }
            return false;
        }

        static bool StartMotorPwm_Prefix(KaboomOutput.MotorPwmId MotorId, ref bool __result)
        {
            // De game vraagt alleen trilling als de operator die niet heeft uitgezet (zoals de originele methode).
            if (!ArcadeManager.VibrationDisabled)
            {
                int p = ((int)MotorId) % 4;   // Big_P1..Big_P4 = 0..3, Small_P1..Small_P4 = 4..7
                int f = Time.frameCount;
                if (lastRecoilFrame[p] != f)   // een schot dat beide motoren aanstuurt telt één keer
                {
                    lastRecoilFrame[p] = f;
                    Core.NoteRecoil(p);
                }
            }
            __result = true;
            return !Core.Cfg.DisableIoBoard.Value;   // bord uit: de aanroep niet doorgeven
        }

        static void ReceiveDamage_Postfix(Player __instance)
        {
            if (playerIdField == null) return;
            try { Core.NoteDamaged((int)playerIdField.GetValue(__instance)); } catch (Exception) { }
        }
    }

    sealed class TombRaiderGame : IGame
    {
        readonly bool[] hold = new bool[4];
        readonly bool[] prevTrigger = new bool[4];
        readonly bool[] prevReload = new bool[4];
        MethodInfo miKey, miCoin;
        CrosshairWindow crosshairWindow;
        FieldInfo crosshairsField;

        public Variant Variant { get { return RavenGun.Unity.Variant.TombRaider; } }
        public string Name { get { return "Tomb Raider Arcade"; } }

        public TombRaiderGame()
        {
            miKey = typeof(KaboomManager).GetMethod("KaboomOnKeyboardEvent", Util.AnyInstance);
            miCoin = typeof(KaboomManager).GetMethod("KaboomOnCoinEvent", Util.AnyInstance);
            crosshairsField = typeof(CrosshairWindow).GetField("m_Crosshairs", Util.AnyInstance);
        }

        public void PressButton(int b)
        {
            KaboomManager km = Singleton<KaboomManager>.Instance;
            if (km == null || miKey == null) return;
            miKey.Invoke(km, new object[] { (KaboomButton.Button)b });
        }

        public void AddCoin(int player)
        {
            KaboomManager km = Singleton<KaboomManager>.Instance;
            if (km == null || miCoin == null) return;
            ushort[] c = new ushort[4];
            c[player] = 1;
            miCoin.Invoke(km, new object[] { c[0], c[1], c[2], c[3] });
        }

        public void DriveGuns(InputState s)
        {
            PlayerManager pm = Singleton<PlayerManager>.Instance;
            GameplayManager gm = Singleton<GameplayManager>.Instance;
            ArcadeManager am = Singleton<ArcadeManager>.Instance;
            WindowManager wm = Singleton<WindowManager>.Instance;
            if (pm == null || gm == null || am == null || wm == null) return;
            bool bot = pm.GetComponentInChildren<BotPlayer>() != null;
            if (bot)
            {
                if (Core.Cfg.Verbose.Value && Time.frameCount % 300 == 0) Core.Log.LogInfo("DriveGuns: een BotPlayer is actief, invoer overgeslagen");
                return;
            }
            Player[] list = pm.m_PlayerList;
            if (list == null) return;
            Camera cam = wm.UICamera((CanvasType)0);
            bool crosshairsOn = InputManager.AreCrosshairsEnabled();

            for (int i = 0; i < 4; i++)
            {
                ID id = (ID)i;
                Vector3 vp = Util.Viewport(cam, s.X[i], s.Y[i]);
                bool gun = am.GunEnabled(i);
                Player pl = i < list.Length ? list[i] : null;

                if (!gm.IsDemo)
                {
                    bool move = gun && pl != null && pl.IsPlaying();
                    if (!move && i == 0) move = wm.GetWindow((WindowID.ID)10) != null;
                    if (move) pm.PlayerMoveCrosshair(vp, id);
                }
                if (Core.Cfg.Verbose.Value && s.Trigger[i] != prevTrigger[i])
                    Core.Log.LogInfo("Speler " + (i + 1) + " trekker " + (s.Trigger[i] ? "in" : "los") + ": gun=" + gun + " alive=" + (pm.GetPlayerByID(id) != null && pm.GetPlayerByID(id).Alive) +
                                     " vizier=" + crosshairsOn + " demo=" + gm.IsDemo + " toestand=" + gm.CurrentState + " bot=" + bot);
                if (!gun) { prevTrigger[i] = s.Trigger[i]; prevReload[i] = s.Reload[i]; continue; }

                if (s.Trigger[i])
                {
                    Player p = pm.GetPlayerByID(id);
                    if (p != null && p.Alive)   // gezondheid > 0 (zoals de originele muisinvoer voor speler 1)
                    {
                        pm.PlayerPressTrigger(vp, id);
                        if (crosshairsOn)
                        {
                            pm.PlayerShoot(vp, id, hold[i], true);
                            hold[i] = true;
                        }
                        else if (!hold[i])
                        {
                            hold[i] = true;
                            if (gm.CurrentState == (GameplayManager.State)6 && !TRA.Arcade.Manager.VideoPlaybackManager.Instance.IsPlaying)
                                pm.PlayPlayerSound((SBK.Audio.AudioID_Global)777, id);
                        }
                    }
                }
                else if (prevTrigger[i])
                {
                    if (crosshairsOn) pm.PlayerTriggerRelease(id);
                    hold[i] = false;
                }
                if (s.Reload[i] && !prevReload[i]) pm.PlayerReload(id);
                if (!hold[i]) pm.StopHoldingFire(id);
                prevTrigger[i] = s.Trigger[i];
                prevReload[i] = s.Reload[i];
            }
        }

        public void ReleaseGuns()
        {
            PlayerManager pm = Singleton<PlayerManager>.Instance;
            for (int i = 0; i < 4; i++)
            {
                if ((hold[i] || prevTrigger[i]) && pm != null)
                {
                    if (InputManager.AreCrosshairsEnabled()) pm.PlayerTriggerRelease((ID)i);
                    pm.StopHoldingFire((ID)i);
                }
                hold[i] = false; prevTrigger[i] = false; prevReload[i] = false;
            }
        }

        // Munitie in het magazijn zoals het HUD die toont (PlayerHud.UpdateAmmo): bij een power-up de power-upwaarde.
        static int Ammo(Player pl)
        {
            if (pl == null) return 0;
            PlayerWeapon g = pl.CurrentGun;
            if (g == null) return 0;
            return g.PowerUpType != (PowerUpType)4 ? g.PowerUpClipRemaining : g.ClipRemaining;
        }

        public void PollStatus(OutputState o)
        {
            KaboomManager km = Singleton<KaboomManager>.Instance;
            PlayerManager pm = Singleton<PlayerManager>.Instance;
            Player[] list = pm != null ? pm.m_PlayerList : null;
            for (int i = 0; i < 4; i++)
            {
                if (km != null) o.Credits[i] = km.GetCoinCount((ID)i);
                Player pl = (list != null && i < list.Length) ? list[i] : null;
                o.Playing[i] = (byte)(pl != null && pl.IsPlaying() ? 1 : 0);
                o.Life[i] = pl != null ? pl.Health : 0f;
                o.Ammo[i] = Ammo(pl);
            }
        }

        public void SetCrosshairHidden(bool hidden)
        {
            if (crosshairWindow == null) crosshairWindow = UnityEngine.Object.FindObjectOfType<CrosshairWindow>();
            if (crosshairWindow == null || crosshairsField == null) return;
            Array arr = crosshairsField.GetValue(crosshairWindow) as Array;
            if (arr == null) return;
            for (int i = 0; i < arr.Length; i++)
            {
                object info = arr.GetValue(i);
                FieldInfo trf = info.GetType().GetField("m_CrosshairTr", Util.AnyInstance);
                Transform tr = trf != null ? trf.GetValue(info) as Transform : null;
                Util.SetVisible(tr, !hidden);
            }
        }
    }
}

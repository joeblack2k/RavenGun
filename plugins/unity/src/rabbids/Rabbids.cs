// RavenGun-plugin voor Rabbids Hollywood Arcade (Adrenaline Amusements, Unity, Mono).
//
// Wat de game hier doet (zelf onderzocht in Assembly-CSharp.dll, alleen gelezen): zelfde bordstructuur als Tomb Raider
// (KaboomManager, ArcadeManager.CheckIOBoard, StartMotorPwm voor recoil). De invoer zonder echt geweer is
// NewInputManager.MouseControl: de muis wordt omgezet naar het kijkvenster van de GameplayCamera en via de
// EventManager met berichtnamen (PlayerCrosshairMove, PlayerPressTrigger, PlayerShoot, PlayerTriggerRelease) naar de
// speler gestuurd. Dit bestand doet hetzelfde, maar voor vier spelers en met de aim uit de daemon.
using System;
using System.Reflection;
using BepInEx;
using HarmonyLib;
using SBK;
using UnityEngine;

namespace RavenGun.Unity.RabbidsHollywood
{
    [BepInPlugin("nl.studiodamon.ravengun.rabbidshollywood", "RavenGun Rabbids Hollywood Arcade", "1.0.0")]
    public class Plugin : BaseUnityPlugin
    {
        Harmony harmony;

        void Awake()
        {
            try
            {
                Settings cfg = new Settings(Config);
                Core.Init(Logger, cfg, new RabbidsGame());
                harmony = new Harmony("nl.studiodamon.ravengun.rabbidshollywood");
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

        static void P(Harmony h, Type t, string name, string pre, string post)
        {
            try
            {
                MethodBase m = AccessTools.Method(t, name);
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
            P(h, typeof(NewInputManager), "MouseControl", "MouseControl_Prefix", null);
            P(h, typeof(KaboomManager), "StartMotorPwm", "StartMotorPwm_Prefix", null);
            P(h, typeof(KaboomManager), "SetPlayerLight", "SetPlayerLight_Prefix", null);
            P(h, typeof(KaboomManager), "ClearPlayerLight", "ClearPlayerLight_Prefix", null);
            if (cfg.DisableIoBoard.Value)
            {
                P(h, typeof(KaboomAdrioFxPlusFourFeeders), "_Initialize", "Skip_Prefix", null);
                P(h, typeof(ArcadeManager), "CheckIOBoard", "Skip_Prefix", null);
            }
            if (cfg.SuppressGunErrors.Value)
            {
                // Zonder echt geweer meldt de game een fatale geweerfout (en sluit zichzelf daarna af); deze invoer komt uit de daemon.
                P(h, typeof(ArcadeManager), "GunFatalError", "Skip_Prefix", null);
            }
        }

        static bool Skip_Prefix() { return false; }

        static bool MouseControl_Prefix()
        {
            if (!Core.DaemonActive) return true;
            try { Core.Game.DriveGuns(Core.Input); }
            catch (Exception e) { Core.Log.LogError("DriveGuns: " + e.Message); }
            return false;
        }

        static bool StartMotorPwm_Prefix(KaboomOutput.MotorPwmId MotorId, ref bool __result)
        {
            int p = ((int)MotorId) % 4;   // Big_P1..Big_P4 = 0..3, Small_P1..Small_P4 = 4..7
            int f = Time.frameCount;
            if (lastRecoilFrame[p] != f)
            {
                lastRecoilFrame[p] = f;
                Core.NoteRecoil(p);
            }
            __result = true;
            return !Core.Cfg.DisableIoBoard.Value;
        }

        // De startknoplamp van een speler (bit 0..3 van het masker) is in deze game de "StartLamp" voor de daemon.
        static bool SetPlayerLight_Prefix(KaboomOutput.OutputPlayerLight i_Mask, ref bool __result)
        {
            int m = (int)i_Mask;
            for (int i = 0; i < 4; i++) if ((m & (1 << i)) != 0) Core.SetPlaying(i, true);
            __result = true;
            return !Core.Cfg.DisableIoBoard.Value;
        }

        static bool ClearPlayerLight_Prefix(KaboomOutput.OutputPlayerLight i_Mask, ref bool __result)
        {
            int m = (int)i_Mask;
            for (int i = 0; i < 4; i++) if ((m & (1 << i)) != 0) Core.SetPlaying(i, false);
            __result = true;
            return !Core.Cfg.DisableIoBoard.Value;
        }
    }

    sealed class RabbidsGame : IGame
    {
        readonly bool[] hold = new bool[4];
        readonly bool[] prevTrigger = new bool[4];
        MethodInfo miKey, miCoin;
        FieldInfo allInput;
        CrosshairWindow crosshairWindow;
        FieldInfo crosshairsField;

        public Variant Variant { get { return RavenGun.Unity.Variant.RabbidsHollywood; } }
        public string Name { get { return "Rabbids Hollywood Arcade"; } }

        public RabbidsGame()
        {
            miKey = typeof(KaboomManager).GetMethod("KaboomOnKeyboardEvent", Util.AnyInstance);
            miCoin = typeof(KaboomManager).GetMethod("KaboomOnCoinEvent", Util.AnyInstance);
            allInput = typeof(NewInputManager).GetField("m_AllInputEnable", Util.AnyInstance);
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
            NewInputManager im = NewInputManager.ActiveInstance;
            ArcadeManager am = Singleton<ArcadeManager>.Instance;
            if (im == null || am == null) return;
            if (allInput != null && !(bool)allInput.GetValue(im)) return;   // de game staat geen invoer toe (zoals de originele methode)
            Camera cam = SceneSingleton<GameplayCamera>.Exists() ? SceneSingleton<GameplayCamera>.Main.Camera : null;
            bool crosshairsOn = im.AreCrosshairsEnabled;
            string move = NewInputManager.BaseMessage.PlayerCrosshairMove.ToString();
            string press = NewInputManager.BaseMessage.PlayerPressTrigger.ToString();
            string shoot = NewInputManager.BaseMessage.PlayerShoot.ToString();
            string release = NewInputManager.BaseMessage.PlayerTriggerRelease.ToString();

            for (int i = 0; i < 4; i++)
            {
                ID id = (ID)i;
                if (!am.GunEnabled(i)) { prevTrigger[i] = s.Trigger[i]; continue; }
                Vector3 vp = Util.Viewport(cam, s.X[i], s.Y[i]);
                EventManager<Vector3, ID>.TriggerEvent(move, vp, id);
                if (s.Trigger[i])
                {
                    EventManager<Vector3, ID>.TriggerEvent(press, vp, id);
                    if (crosshairsOn)
                    {
                        EventManager<Vector3, ID>.TriggerEvent(shoot, vp, id);
                    }
                    else if (!hold[i])
                    {
                        hold[i] = true;
                        ArcadeGameplayManager gm = Singleton<ArcadeGameplayManager>.Instance;
                        if (gm != null && gm.CurrentState == (ArcadeGameplayManager.State)4)
                            SBK.Audio.AudioManager.Instance.Play((SBK.Audio.AudioID_Global)452, null);
                    }
                }
                else if (prevTrigger[i])
                {
                    if (crosshairsOn) EventManager<ID>.TriggerEvent(release, id);
                    hold[i] = false;
                }
                prevTrigger[i] = s.Trigger[i];
            }
        }

        public void ReleaseGuns()
        {
            NewInputManager im = NewInputManager.ActiveInstance;
            string release = NewInputManager.BaseMessage.PlayerTriggerRelease.ToString();
            for (int i = 0; i < 4; i++)
            {
                if ((hold[i] || prevTrigger[i]) && im != null && im.AreCrosshairsEnabled)
                    EventManager<ID>.TriggerEvent(release, (ID)i);
                hold[i] = false; prevTrigger[i] = false;
            }
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
                o.Life[i] = pl != null ? pl.m_Health : 0f;
                // Playing = StartLamp: gezet door SetPlayerLight/ClearPlayerLight van de game, hier niet overschreven
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

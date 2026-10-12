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
            if (cfg.Verbose.Value) Util.TraceQuit(h, typeof(KaboomManager), typeof(ArcadeManager), typeof(OperatorPopup));
            if (cfg.SuppressGunErrors.Value)
            {
                // Zonder echt geweer meldt de game een fatale geweerfout (en sluit zichzelf daarna af); deze invoer komt uit de daemon.
                P(h, typeof(ArcadeManager), "GunFatalError", "Skip_Prefix", null);
            }

            // De Matrix-dongle (SBK.Matrix.MatrixManager). Bij het opstarten controleert de game twee dingen en
            // sluit zichzelf anders af met "**** DONGLE NOT FOUND" gevolgd door UnityEngine.Application.Quit:
            //   1. Dongle_Count(85) moet > 0 zijn;
            //   2. Dongle_ReadSerNr(dongle, aantal, 85) moet > 0 zijn; dat getal wordt het "dongle-serienummer".
            // Zonder dongle geeft de originele matrix64.dll op beide 0, en de controle eindigt met code 3.
            // Wij beantwoorden daarom juist die drie aanroepen zelf; matrix64.dll wordt verder niet gebruikt.
            // De game leest geen dongle-geheugen: Dongle_ReadData/Dongle_WriteData komen in het spelpad niet voor.
            // Bewijs en afweging: docs/matrix-layer.md. API: findings/MATRIX-dongle-API.md.
            //
            // DE SAVEGAME-VERSLEUTELING LOOPT VIA DEZELFDE DONGLE, en die mag je NIET half beantwoorden.
            // SBK.Matrix.MatrixManager roept Dongle_EncryptData/Dongle_DecryptData aan op dertien plaatsen:
            // Encrypt uit KPHIPOINDLP, BMNNMICCONF, EDNBLKCLMOI, NDIBJIHIEGD, NKLMBDBGNGM en MIBFIOJCFDO;
            // Decrypt uit AJPGJFKKFIB, EFOGIECKCJK, LKFFFBKBLFF, GDIPNEDGNPL, CBJBMICIDFJ, PGOAGOPNHGP en
            // BABEGMOMBOG (gemeten met re/dnlib.py op de Assembly-CSharp.dll van deze game).
            // De buffer gaat per blok van 8 byte naar binnen als `ref int`, en de aanroeper gaat alleen verder
            // als het dongle-serienummer > 0 is -- met onze 69420 gebeuren ze dus echt.
            // Zonder antwoord schrijft en leest de originele matrix64.dll zonder dongle, en dan ziet de game bij
            // elke start "Bad PKCS7 padding" en zet hij zijn savegames terug ("Savegame corrupted: ... will be
            // resetted"). Wij beantwoorden ze daarom ook, en precies zoals onze eigen matrix64.dll dat doet:
            // 0 terug en de buffer ongemoeid. Dat blijft alleen consistent zolang BEIDE richtingen een no-op
            // zijn: het spel leest dan terug wat het zelf heeft weggeschreven. Wat deze laag niet kan: gegevens
            // ontsleutelen die met een echte dongle zijn versleuteld.
            // Twee routes voor dezelfde dongle, zie docs/matrix-layer.md:
            //  - de patch (standaard): wij beantwoorden deze aanroepen in de managed wrapper;
            //  - de drop-in (RAVENGUN_MATRIX_DLL=1): patches/RabbidsHollywood/dropin/matrix64.dll
            //    doet hetzelfde in de DLL zelf, zodat deze plugin niets hoeft te patchen.
            // De schakelaar bestaat zodat te bewijzen is welke route werkt; de launcher zet hem
            // wanneer hij de drop-in plaatst (dropin.ini, docs/dropins.md).
            // LET OP: die schakelaar kan zichzelf niet controleren. Op de uitgeleverde route staat geen drop-in,
            // dus wie hem daar zet haalt hiermee de dongle-antwoorden weg en dan sterft de game weer na ~70 s.
            if (Environment.GetEnvironmentVariable("RAVENGUN_MATRIX_DLL") == "1")
            {
                Core.Log.LogInfo("Matrix: de drop-in matrix64.dll beantwoordt de dongle-controle (patches uit)");
            }
            else
            {
                P(h, typeof(MatrixDLLCSharp.MatrixWrapper), "Init_MatrixAPI", "MatrixInit_Prefix", null);
                P(h, typeof(MatrixDLLCSharp.MatrixWrapper), "Dongle_Find", "MatrixFind_Prefix", null);
                P(h, typeof(MatrixDLLCSharp.MatrixWrapper), "Dongle_Count", "MatrixCount_Prefix", null);
                P(h, typeof(MatrixDLLCSharp.MatrixWrapper), "Dongle_ReadSerNr", "MatrixSerNr_Prefix", null);
                P(h, typeof(MatrixDLLCSharp.MatrixWrapper), "Dongle_EncryptData", "MatrixEncrypt_Prefix", null);
                P(h, typeof(MatrixDLLCSharp.MatrixWrapper), "Dongle_DecryptData", "MatrixDecrypt_Prefix", null);
            }
        }

        static bool Skip_Prefix() { return false; }

        // --- De Matrix-dongle ---------------------------------------------------------------
        // De poort in de aanroepen is 85, niet 40: SBK.Matrix.MatrixManager.Awake roept KDLGNPHHMKI aan en die
        // gebruikt Dongle_Count(85) en Dongle_ReadSerNr(..., 85). De variant met poort 40 (EGIEJAEHODM) wordt
        // door niets aangeroepen.
        static bool MatrixInit_Prefix(ref short __result)
        {
            __result = 0;   // 0 = in orde; een negatieve waarde betekent "MatrixAPI niet geinitialiseerd"
            if (Core.Cfg.Verbose.Value) Core.Log.LogInfo("Matrix: Init_MatrixAPI -> 0 (onze eigen dongle-laag)");
            return false;
        }

        static bool MatrixFind_Prefix(ref short __result)
        {
            __result = 1;
            if (Core.Cfg.Verbose.Value) Core.Log.LogInfo("Matrix: Dongle_Find -> 1");
            return false;
        }

        // 2 en niet 1: er staat nog een tweede, ongebruikte controlevariant in de assembly die "aantal > 1" eist.
        // De variant die Awake werkelijk aanroept eist alleen > 0. Met 2 voldoen we aan allebei; TeknoParrot kiest
        // in zijn eigen matrix32.dll dezelfde waarde.
        static bool MatrixCount_Prefix(short Port, ref short __result)
        {
            __result = 2;
            Core.Log.LogInfo("Matrix: Dongle_Count(" + Port + ") -> 2 (onze eigen dongle-laag)");
            return false;
        }

        // Het serienummer moet > 0 zijn, anders geeft de controle code 3 en volgt alsnog "DONGLE NOT FOUND".
        // We geven een vaste, zelfgekozen waarde terug: er wordt geen echt serienummer van een dongle gelezen of
        // gelogd (regel: geen unieke identificaties uitlezen of loggen). 0x10F2C is dezelfde waarde die
        // TeknoParrot's eigen matrix32.dll teruggeeft.
        static bool MatrixSerNr_Prefix(ref int __result)
        {
            __result = 0x10F2C;
            Core.Log.LogInfo("Matrix: Dongle_ReadSerNr -> " + __result + " (onze eigen dongle-laag, vaste waarde)");
            return false;
        }

        // --- De savegame-versleuteling van dezelfde dongle ------------------------------------
        // Zie het blok in Apply(): 0 terug en de buffer ongemoeid, aan BEIDE kanten. Half beantwoorden is
        // erger dan niet beantwoorden, want dan schrijft het spel half versleutelde savegames weg.
        //
        // Deze twee prefixen vragen GEEN enkel argument van het origineel op, alleen het resultaat. Daardoor
        // binden ze ongeacht de parameternamen en ongeacht de vorm van de buffer (de wrapper geeft die door als
        // `ref int`, per blok van 8 byte), en blijft de buffer gegarandeerd ongemoeid: we raken hem niet aan.
        // Dat is hier bewust: MatrixCount_Prefix hierboven bindt wél op naam (`short Port`), en precies dat is
        // eerder misgegaan -- met `poort` in plaats van `Port` mislukte die patch en stierf de game alsnog.
        //
        // Per richting wordt alleen de EERSTE aanroep gelogd; anders zou één savegame het log vullen. Die twee
        // regels zijn tegelijk het bewijs dat geen van beide richtingen vergeten is: staat er na een sessie maar
        // één van de twee, dan is het savegame-pad half beantwoord.
        static bool encryptLogged, decryptLogged;

        static bool MatrixEncrypt_Prefix(ref short __result)
        {
            __result = 0;   // 0 = gelukt; het spel neemt de buffer over zoals hij is
            if (!encryptLogged)
            {
                encryptLogged = true;
                Core.Log.LogInfo("Matrix: Dongle_EncryptData -> 0, buffer ongemoeid (onze eigen dongle-laag)");
            }
            return false;
        }

        static bool MatrixDecrypt_Prefix(ref short __result)
        {
            __result = 0;   // idem: 0 = gelukt, buffer ongemoeid
            if (!decryptLogged)
            {
                decryptLogged = true;
                Core.Log.LogInfo("Matrix: Dongle_DecryptData -> 0, buffer ongemoeid (onze eigen dongle-laag)");
            }
            return false;
        }

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

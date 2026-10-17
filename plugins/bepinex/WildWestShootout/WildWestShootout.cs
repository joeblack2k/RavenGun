// SPDX-License-Identifier: GPL-3.0-or-later
// RavenGun-plugin voor Wild West Shootout (Unity 5.6, 32 bits, CowBoy.exe), op de directe route zonder de kern van TeknoParrot.
//
// Wat de game op de kast doet (gelezen in Assembly-CSharp.dll, 2026-10-17):
//   - het richtpunt komt van een industriele camera (MVSDK.BaseCamera, MindVision) die de lichtvlek van het geweer volgt; bij elk
//     schot flitst het beeld wit (GameUIController.ShowFire, fireWhite) zodat de camera het geweer ziet;
//   - trekker, munt en het testmenu komen van een serieel I/O-bord (BaseCom over LSerialPort, pakketten van 8 bytes): 0xDD met per
//     geweer bit 0 = trekker (BaseGun.RecvGunData -> BaseGun.OnShoot/OnShootUp), 16 = naar het testmenu (OnIntoSetting), 0xCC =
//     munt (OnCoin -> InputController.InCoinAction); GameStart wacht op het bord en meet de camera voordat de attract begint;
//   - de dongle is een SuperDog (BaseCom.CheckDog).
// De game heeft een eigen toetsenbordroute (GameData.KeyBoard), maar die staat uit en niets in de game zet hem aan.
//
// Deze plugin bedient de game op het niveau van die gebeurtenissen, uit het bordbeeld van RavenGun (shared/Bordbeeld.cs):
//   - BaseGun.GetGunPos/GetGunRealPixPos: het richtpunt (fractie, oorsprong linksonder / schermpixels); buiten beeld InvalidPoint
//     (de game herlaadt dan zelf, ShootController telt dat);
//   - trekker van speler 1/2: BaseGun.OnShoot/OnShootUp, zoals het bord dat doet (ook de keuzeschermen luisteren daarnaar);
//   - munt 1/2: InputController.InCoinAction(speler, 1, true) (of InCoinActionSingle bij een enkele ticketautomaat);
//   - herladen: PlayerUIController.ReLoadBullets van die speler; start: InputController.ConfirmAction(speler);
//   - test: het testmenu (InputController.IntoSetting); daarin service = key1 (kiezen), menu omhoog/omlaag = key2/key3;
//   - uit: de camera (MVSDK.BaseCamera: alles slaagt zonder iets te doen), het zenden naar het bord, de bordcontrole
//     (InputController.SendCheckIO), de dongle (BaseCom.CheckDog), de witte flits, de muisroute van twee keuzeschermen
//     (GameBeginUIController/GameChooseUI2.Update; hun schoten komen via OnShoot) en de bord- en cameracontrole bij het opstarten
//     (GameStart gaat meteen naar de attract);
//   - BaseCom.IsOpen is altijd waar (er is geen bord dat op de controle antwoordt).
// Indeling uit het WartranTroopers-profiel van deze game: trekker en herladen per speler, munt 1/2; de app/IO-brug levert de rest.
using System;
using System.Collections.Generic;
using System.Reflection;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using RavenGun.Bepinex.Gedeeld;
using UnityEngine;

namespace RavenGun.Bepinex.WildWestShootout
{
    [BepInPlugin("nl.studiodamon.ravengun.wildwestshootout", "RavenGun.WildWestShootout", "1.0.0")]
    [BepInDependency("com.tp.wws", BepInDependency.DependencyFlags.SoftDependency)]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource L;
        internal static Bordbeeld B;
        const int Spelers = 2;   // BaseGun.MaxGun

        static Type tBaseGun, tInput, tGameData, tPlayerType, tGameUI;
        static bool verbergCursor;

        void Awake()
        {
            L = Logger;
            B = new Bordbeeld(L);
            TpPlugin.ZetUit("com.tp.wws", L);
            tBaseGun = AccessTools.TypeByName("MVSDK.BaseGun");
            tInput = AccessTools.TypeByName("InputController");
            tGameData = AccessTools.TypeByName("GameData");
            tPlayerType = AccessTools.TypeByName("PlayerType");
            tGameUI = AccessTools.TypeByName("GameUIController");
            Harmony h = new Harmony("nl.studiodamon.ravengun.wildwestshootout");
            Haak(h, tBaseGun, "GetGunPos", new[] { typeof(int) }, "GunPos", null);
            Haak(h, tBaseGun, "GetGunRealPixPos", new[] { typeof(int) }, "GunPixPos", null);
            Haak(h, AccessTools.TypeByName("BaseCom"), "get_IsOpen", null, null, "AltijdOpen");
            Haak(h, AccessTools.TypeByName("BaseCom"), "CheckDog", null, "Overslaan", null);
            Haak(h, AccessTools.TypeByName("LSerialPort"), "DiretSend", null, "Overslaan", null);
            Haak(h, AccessTools.TypeByName("LSerialPort"), "SendData", new[] { typeof(byte[]), typeof(int) }, "Overslaan", null);
            Haak(h, AccessTools.TypeByName("LSerialPort"), "SendData", new[] { typeof(List<byte>), typeof(int) }, "Overslaan", null);
            Haak(h, tInput, "SendCheckIO", null, "Overslaan", null);
            Haak(h, tGameUI, "ShowFire", null, null, "GeenFlits");
            Haak(h, tGameUI, "Update", null, "VizierSpel", null);
            Haak(h, AccessTools.TypeByName("ShowGamePointController"), "Update", null, "VizierPunten", null);
            Haak(h, AccessTools.TypeByName("GameBeginUIController"), "Update", null, "Overslaan", null);
            Haak(h, AccessTools.TypeByName("GameChooseUI2"), "Update", null, "Overslaan", null);
            Haak(h, AccessTools.TypeByName("GameStart"), "FixedUpdate", null, "MeteenNaarAttract", null);
            Haak(h, AccessTools.TypeByName("SettingController"), "Update", null, "Testmenu", null);
            Camera(h);
            L.LogMessage("RavenGun.WildWestShootout: " + gezet + " van " + gevraagd + " haken gezet");
            Venster();
        }

        static int gezet, gevraagd;

        static void Haak(Harmony h, Type t, string naam, Type[] args, string prefix, string postfix)
        {
            gevraagd++;
            MethodInfo m = t == null ? null : (args == null ? AccessTools.Method(t, naam) : AccessTools.Method(t, naam, args));
            if (m == null) { L.LogWarning("niet gevonden: " + (t == null ? "?" : t.FullName) + "." + naam); return; }
            h.Patch(m, prefix: prefix == null ? null : new HarmonyMethod(typeof(Plugin), prefix),
                    postfix: postfix == null ? null : new HarmonyMethod(typeof(Plugin), postfix));
            gezet++;
        }

        // De camera van het geweer bestaat niet: elke methode van MVSDK.BaseCamera slaagt zonder iets te doen (bool = waar, de rest
        // de standaardwaarde, bij SetImageResolution/SetExposureState 0 = CAMERA_STATUS_SUCCESS).
        static void Camera(Harmony h)
        {
            Type t = AccessTools.TypeByName("MVSDK.BaseCamera");
            string[] namen = { "Play", "Pause", "InitCamera", "CaptureThreadProc", "GetFrameData", "UpdateExposure", "GetImageResolution",
                               "SetImageResolution", "LoadDefaultConfig", "SetMonochrome", "SetExposureState", "Close", "set_AnalogGain" };
            gevraagd += namen.Length;
            if (t == null) { L.LogWarning("niet gevonden: MVSDK.BaseCamera"); return; }
            HashSet<string> gezien = new HashSet<string>();
            foreach (MethodInfo m in AccessTools.GetDeclaredMethods(t))
            {
                if (Array.IndexOf(namen, m.Name) < 0 || m.IsStatic) continue;
                h.Patch(m, prefix: new HarmonyMethod(typeof(Plugin), m.ReturnType == typeof(bool) ? "Waar" : "Overslaan"));
                if (gezien.Add(m.Name)) gezet++;
            }
        }

        static void Venster()
        {
            TpIni ini = TpIni.Lees();
            bool vol = !ini.Waar("General/Windowed", false);
            verbergCursor = ini.Waar("General/HideCursor", true);
            if (ini.Waar("General/CustomResolution", false))
                Screen.SetResolution(ini.Getal("General/ResolutionWidth", 1920), ini.Getal("General/ResolutionHeight", 1080), vol);
            else
                Screen.fullScreen = vol;
        }

        static bool Overslaan() { return false; }
        static bool Waar(ref bool __result) { __result = true; return false; }
        static void AltijdOpen(ref bool __result) { __result = true; }

        // --- richten --------------------------------------------------------------------------------------------------------------
        static Vector2 Ongeldig()
        {
            FieldInfo f = AccessTools.Field(tBaseGun, "InvalidPoint");
            return f == null ? new Vector2(-1f, -1f) : (Vector2)f.GetValue(null);
        }

        static bool GunPos(int gun_id, ref Vector2 __result)
        {
            B.Ververs();
            if (gun_id < 0 || gun_id >= Spelers || !B.Verbonden || B.BuitenBeeld(gun_id)) { __result = Ongeldig(); return false; }
            __result = B.Fractie(gun_id);
            return false;
        }

        static bool GunPixPos(int gun_id, ref Vector2 __result)
        {
            GunPos(gun_id, ref __result);
            if (__result.x >= 0f) __result = new Vector2(__result.x * Screen.width, __result.y * Screen.height);
            return false;
        }

        // Het vizier op het scherm: het canvas van de game rekent in 1920x1080 met de oorsprong linksonder.
        static void Vizier(object ui, bool alleenAlsAan, bool laadEinde)
        {
            if (alleenAlsAan)
            {
                FieldInfo fs = AccessTools.Field(tGameData, "FrontSight");
                if (fs != null && (int)fs.GetValue(null) != 1) return;
            }
            Array punten = Traverse.Create(ui).Field("gunPoint").GetValue() as Array;
            if (punten == null) return;
            MethodInfo spelerData = AccessTools.Method(tGameData, "GetPlayerData", new[] { typeof(int) });
            for (int i = 0; i < punten.Length; i++)
            {
                Component punt = punten.GetValue(i) as Component;
                if (punt == null) continue;
                object pd = spelerData == null ? null : spelerData.Invoke(null, new object[] { i });
                bool kan = pd != null && (bool)AccessTools.Method(pd.GetType(), "CanShoot").Invoke(pd, null);
                RectTransform rt = Traverse.Create(punt).Field("mRectTF").GetValue() as RectTransform;
                if (kan && !laadEinde && i < Spelers && B.Verbonden && !B.BuitenBeeld(i) && rt != null)
                {
                    Vector2 f = B.Fractie(i);
                    rt.anchoredPosition = new Vector2(f.x * 1920f, f.y * 1080f);
                }
                else
                {
                    punt.transform.localPosition = new Vector3(-1920f, -1080f);
                }
            }
        }

        static bool VizierSpel(object __instance)
        {
            Vizier(__instance, true, (bool)Traverse.Create(__instance).Field("isLoadEnd").GetValue());
            return false;
        }

        static bool VizierPunten(object __instance) { Vizier(__instance, false, false); return false; }

        static void GeenFlits(object __instance)
        {
            GameObject wit = Traverse.Create(__instance).Field("fireWhite").GetValue() as GameObject;
            if (wit != null) wit.SetActive(false);
        }

        // --- opstarten en testmenu ------------------------------------------------------------------------------------------------
        static int startFrame = -1;

        static bool MeteenNaarAttract(object __instance)
        {
            // Een keer per GameStart: de bordcontrole en de cameracalibratie (BaseCollect) hebben geen zin zonder bord en camera.
            if (startFrame >= 0 && Time.frameCount - startFrame < 300) return false;
            startFrame = Time.frameCount;
            MethodInfo m = AccessTools.Method(__instance.GetType(), "EnterGame");
            if (m != null) m.Invoke(__instance, null);
            return false;
        }

        static bool Testmenu(object __instance)
        {
            if (B.Neer(Indeling.Service)) Roep(__instance, "key1BtnAction");
            if (B.Neer(Indeling.VolumeOp)) Roep(__instance, "key2BtnAction");
            if (B.Neer(Indeling.VolumeNeer)) Roep(__instance, "key3BtnAction");
            return true;
        }

        static void Roep(object o, string naam)
        {
            MethodInfo m = o == null ? null : AccessTools.Method(o.GetType(), naam);
            if (m != null) m.Invoke(o, null);
        }

        // --- knoppen per frame ----------------------------------------------------------------------------------------------------
        void Update()
        {
            if (verbergCursor && Cursor.visible) Cursor.visible = false;
            B.Ververs();
            if (!B.Verbonden) return;
            object invoer = tInput == null ? null : AccessTools.Field(tInput, "current").GetValue(null);
            object geweer = tBaseGun == null ? null : AccessTools.Property(tBaseGun, "Instance").GetValue(null, null);
            for (int p = 0; p < Spelers; p++)
            {
                object speler = Enum.ToObject(tPlayerType, p);
                if (geweer != null && B.Neer(Indeling.Trekker[p])) Delegaat(geweer, "OnShoot", p);
                if (geweer != null && B.Op(Indeling.Trekker[p])) Delegaat(geweer, "OnShootUp", p);
                if (B.Neer(p == 0 ? Indeling.Munt1 : Indeling.Munt2) && invoer != null) Munt(invoer, speler);
                if (B.Neer(Indeling.Start[p]) && invoer != null) Delegaat(invoer, "ConfirmAction", speler);
                if (B.Neer(Indeling.Herladen[p])) Herlaad(p);
            }
            if (B.Neer(Indeling.Test) && invoer != null && !(bool)AccessTools.Field(tGameData, "isSetting").GetValue(null))
                Roep(invoer, "IntoSetting");
        }

        static void Munt(object invoer, object speler)
        {
            if ((bool)AccessTools.Field(tGameData, "SingleTicketMachine").GetValue(null)) Delegaat(invoer, "InCoinActionSingle", 1, true);
            else Delegaat(invoer, "InCoinAction", speler, 1, true);
        }

        static void Herlaad(int p)
        {
            object ui = tGameUI == null || !(bool)AccessTools.Property(tGameUI, "HadInstance").GetValue(null, null)
                ? null : AccessTools.Property(tGameUI, "Instance").GetValue(null, null);
            Array spelers = ui == null ? null : Traverse.Create(ui).Field("playerUI").GetValue() as Array;
            if (spelers != null && p < spelers.Length) Roep(spelers.GetValue(p), "ReLoadBullets");
        }

        static void Delegaat(object o, string veld, params object[] args)
        {
            Delegate d = Traverse.Create(o).Field(veld).GetValue() as Delegate;
            if (d == null) return;
            try { d.DynamicInvoke(args); }
            catch (Exception e) { L.LogWarning(veld + ": " + (e.InnerException ?? e).Message); }
        }
    }
}

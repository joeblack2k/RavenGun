// SPDX-License-Identifier: GPL-3.0-or-later
// RavenGun-plugin voor Mars Sortie (Unity, 64 bits, namespace MRCQ), op de directe route zonder de kern van TeknoParrot.
//
// Wat de game op de kast doet (gelezen in Assembly-CSharp.dll, 2026-10-17): alle invoer loopt via de signaallaag MRCQ.Hardware
// (SetInputKey/SetInputAxis per naam, GetInputKeyDown/GetInputAxis voor de game). Die laag wordt gevuld door het I/O-bord GMB
// (MRCQ.GMB, MRCQ.GMBHelper) en de lasercamera's; de dongle is een SuperDog (MRCQ.SuperDogManager.DoCheckKey, dog_windows_x64).
// Zonder bord en camera meldt SystemError een I/O-fout.
//
// Deze plugin vult de signaallaag zelf uit het bordbeeld van RavenGun (plugins/bepinex/shared/Bordbeeld.cs):
//   - vlak voor Hardware.Update de knoppen (de game telt zelf de flanken en de munten);
//   - GetInputAxis("x01".."y04"): de richtpunten als fractie 0..1, oorsprong linksonder (zoals ShowLaserPoints ze als UI-anker
//     gebruikt);
//   - het bord (GMB, GMBHelper), de externe games (GameLoader.LoadExternalGames), de dongle (DoCheckKey = waar), de foutmeldingen
//     van bord en camera en de vaste toetsenbordtoetsen van de signalen (SignalKey: simulator/mouse) staan uit.
// De indeling van de knoppen volgt het WartranTroopers-profiel van deze game (welke knop welk signaal is; feiten uit het profiel):
//   test -> OK, munt 1/2 -> coinsIn01/02, start/trekker/herladen van speler N -> startGameKey/fire/switchWeapon0N,
//   menu omhoog/omlaag = granaat/aux1 van speler 1, menu links/rechts = granaat/aux1 van speler 2.
using System;
using System.Reflection;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using RavenGun.Bepinex.Gedeeld;
using UnityEngine;

namespace RavenGun.Bepinex.MarsSortie
{
    [BepInPlugin("nl.studiodamon.ravengun.marssortie", "RavenGun.MarsSortie", "1.0.0")]
    [BepInDependency("com.tp.ms", BepInDependency.DependencyFlags.SoftDependency)]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource L;
        internal static Bordbeeld B;
        static MethodInfo zetKey, zetGroepKey;
        static bool verbergCursor;

        void Awake()
        {
            L = Logger;
            B = new Bordbeeld(L);
            TpPlugin.ZetUit("com.tp.ms", L);
            Harmony h = new Harmony("nl.studiodamon.ravengun.marssortie");
            Type hw = AccessTools.TypeByName("MRCQ.Hardware");
            zetKey = AccessTools.Method(hw, "SetInputKey", new[] { typeof(string), typeof(bool) });
            zetGroepKey = AccessTools.Method(hw, "SetInputKey", new[] { typeof(int), typeof(string), typeof(bool) });
            int n = 0;
            n += Haak(h, hw, "Update", null, "VoorHardwareUpdate", null);
            n += Haak(h, hw, "GetInputAxis", new[] { typeof(string) }, null, "NaGetInputAxis");
            n += Haak(h, AccessTools.TypeByName("MRCQ.GMB"), "Start", null, "Overslaan", null);
            n += Haak(h, AccessTools.TypeByName("MRCQ.GMB"), "Update", null, "Overslaan", null);
            n += Haak(h, AccessTools.TypeByName("MRCQ.GMBHelper"), "Start", null, "Overslaan", null);
            n += Haak(h, AccessTools.TypeByName("MRCQ.GMBHelper"), "LateUpdate", null, "Overslaan", null);
            n += Haak(h, AccessTools.TypeByName("GameLoader"), "LoadExternalGames", null, "Overslaan", null);
            n += Haak(h, AccessTools.TypeByName("MRCQ.SuperDogManager"), "DoCheckKey", null, "DongleGoed", null);
            n += Haak(h, AccessTools.TypeByName("MRCQ.SystemError"), "SetIOBoardError", null, "Overslaan", null);
            n += Haak(h, AccessTools.TypeByName("MRCQ.SystemError"), "SetLaserCamError", null, "Overslaan", null);
            n += Haak(h, AccessTools.TypeByName("MRCQ.SignalKey"), "Update", null, "GeenSimulator", null);
            L.LogMessage("RavenGun.MarsSortie: " + n + " van 11 haken gezet");
            Venster();
        }

        static int Haak(Harmony h, Type t, string naam, Type[] args, string prefix, string postfix)
        {
            MethodInfo m = t == null ? null : (args == null ? AccessTools.Method(t, naam) : AccessTools.Method(t, naam, args));
            if (m == null) { L.LogWarning("niet gevonden: " + (t == null ? "?" : t.FullName) + "." + naam); return 0; }
            h.Patch(m, prefix: prefix == null ? null : new HarmonyMethod(typeof(Plugin), prefix),
                    postfix: postfix == null ? null : new HarmonyMethod(typeof(Plugin), postfix));
            return 1;
        }

        // Venster en cursor uit teknoparrot.ini (de opties van de app: Windowed, HideCursor, eigen resolutie).
        static void Venster()
        {
            TpIni ini = TpIni.Lees();
            bool vol = !ini.Waar("General/Windowed", false);
            verbergCursor = ini.Waar("General/HideCursor", true);
            if (!ini.Waar("General/CustomResolution", false)) { Screen.fullScreen = vol; return; }
            int w = ini.Getal("General/ResolutionWidth", 1920), hg = ini.Getal("General/ResolutionHeight", 1080);
            Screen.SetResolution(w, hg, vol);
            L.LogMessage("venster: " + w + "x" + hg + (vol ? " schermvullend" : " in een venster"));
        }

        void Update()
        {
            if (verbergCursor && Cursor.visible) Cursor.visible = false;
        }

        static bool Overslaan() { return false; }

        static bool DongleGoed(ref bool __result) { __result = true; return false; }

        // Een virtuele toets (isVKey) met een vaste toetsenbordtoets of muisknop ("simulator", "mouse" in de XML van de game) zet in
        // SignalKey.Update zijn stand eerst op onwaar en neemt dan alleen het toetsenbord over: wat SetInputKey zette, ging dan elke frame
        // verloren. Zonder die vaste toetsen blijft de stand van het bordbeeld staan.
        static void GeenSimulator(ref string ___simulator, ref int ___mouse) { ___simulator = null; ___mouse = -1; }

        static void Zet(string naam, bool aan) { if (zetKey != null) zetKey.Invoke(null, new object[] { naam, aan }); }
        static void Zet(int groep, string naam, bool aan) { if (zetGroepKey != null) zetGroepKey.Invoke(null, new object[] { groep, naam, aan }); }

        static void VoorHardwareUpdate()
        {
            Zet("OK", B.Ingedrukt(Indeling.Test));
            Zet("coinsIn01", B.Ingedrukt(Indeling.Munt1));
            Zet("coinsIn02", B.Ingedrukt(Indeling.Munt2));
            Zet("up", B.Ingedrukt(Indeling.Granaat[0]));
            Zet("down", B.Ingedrukt(Indeling.Aux1[0]));
            Zet("left", B.Ingedrukt(Indeling.Granaat[1]));
            Zet("right", B.Ingedrukt(Indeling.Aux1[1]));
            for (int p = 0; p < 4; p++)
            {
                Zet(p, "startGameKey", B.Ingedrukt(Indeling.Start[p]));
                Zet(p, "fire", B.Ingedrukt(Indeling.Trekker[p]));
                Zet("switchWeapon0" + (p + 1), B.Ingedrukt(Indeling.Herladen[p]));
            }
        }

        static void NaGetInputAxis(string axisName, ref float __result)
        {
            if (axisName == null || axisName.Length != 3 || axisName[1] != '0') return;
            int p = axisName[2] - '1';
            if (p < 0 || p > 3 || (axisName[0] != 'x' && axisName[0] != 'y')) return;
            B.Ververs();
            if (!B.Verbonden) return;
            Vector2 f = B.Fractie(p);
            __result = Mathf.Clamp01(axisName[0] == 'x' ? f.x : f.y);
        }
    }
}

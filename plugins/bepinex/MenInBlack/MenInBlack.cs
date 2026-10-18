// SPDX-License-Identifier: GPL-3.0-or-later
// RavenGun-plugin voor Men in Black (SEGA Amusements, Unity 2020.3, 64 bits), op de directe route zonder de shell van de kast.
//
// Op de kast start Shell.exe de game en praat ermee (MIB.MIBClient, de berichten van de shell: munten, instellingen, de
// security-challenge van de DK2-dongle). Zonder shell stopt de game zichzelf, op twee plaatsen (gemeten 2026-10-17, IL van
// Assembly-CSharp.dll en het logboek van deze plugin):
//   1. MIBClient.ProcessMessage (coroutine): zolang IsConnected() waar is, worden berichten verstuurd; daarna Application.Quit().
//   2. MIBCore.Update: na vier onbeantwoorde security-challenges "Security Failed", SecurityShutdown = true en de coroutine
//      ExitGameSequance (munten uit, lampen uit, Application.Quit()).
// Deze plugin onderdrukt Application.Quit() (de game wordt gestopt door RavenGun, niet door zichzelf) en houdt de teller van de
// security-challenge op 0, zodat ExitGameSequance niet start en de munten blijven werken.
//
// INVOER (speelproef 2026-10-18: "PRESS START BUTTON TO JOIN" bleef staan). Wat de game leest (gemeten aan Assembly-CSharp.dll en de
// InputManager in globalgamemanagers):
//   - start, trekker: Unity-knoppen "PlayerStart1/2" (joystick button 5/6) en "Player1/2Trigger1" (joystick button 7,8 / 9,10): het
//     I/O-bord van de kast meldt zich als joystick. Op de kast zijn er dus geen toetsen of muisknoppen voor;
//   - richten: Input.GetAxis("X Axis0/1", "Y Axis0/1") gemiddeld over 8 frames in MIBFilterInput, daarna gekalibreerd met
//     ShellData.ini (C:/Sega/ShellData, van de shell);
//   - MIBCore.Update zet DisableInputs = TRUE zodra Input.GetJoystickNames() een naam geeft. Onze virtuele geweren, het toetsenbord en
//     elk echt gamepad zijn voor Wine/Unity joysticks, dus DisableInputs stond altijd aan: CreditsUI.CheckCreditTaken (start) deed niets
//     en GetFilterInput gaf (0,0). Dat is de oorzaak van "start komt niet aan".
// De plugin leest daarom het bordbeeld van RavenGun (plugins/bepinex/shared/Bordbeeld.cs, indeling WartranTroopers) en
//   1. zet DisableInputs na elke MIBCore.Update op onwaar;
//   2. beantwoordt GetButton/GetButtonDown/GetButtonUp voor de vier knopnamen uit het bordbeeld (andere namen gaan door);
//   3. levert de richtpunten als schermpunt (oorsprong linksonder, zoals Unity) rechtstreeks uit GetFilterInput, zonder kalibratie;
//   4. telt de muntknop (munt 1) als een credit voor de game (MIBCore.SetCreditValue), voor als de shell-instellingen geen freeplay zijn.
using System;
using System.Reflection;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using RavenGun.Bepinex.Gedeeld;
using UnityEngine;

namespace RavenGun.Bepinex.MenInBlack
{
    [BepInPlugin("nl.studiodamon.ravengun.meninblack", "RavenGun.MenInBlack", "1.0.0")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource L;
        internal static Bordbeeld B;

        void Awake()
        {
            L = Logger;
            B = new Bordbeeld(L);
            Harmony h = new Harmony("nl.studiodamon.ravengun.meninblack");
            h.Patch(AccessTools.Method(typeof(UnityEngine.Application), "Quit", new Type[0]),
                    prefix: new HarmonyMethod(typeof(Plugin), nameof(GeenQuit)));
            MethodInfo update = AccessTools.Method(AccessTools.TypeByName("MIB.MIBCore"), "Update");
            if (update != null) h.Patch(update, prefix: new HarmonyMethod(typeof(Plugin), nameof(SecurityGehaald)));
            else L.LogWarning("MIB.MIBCore.Update niet gevonden: de security-challenge blijft zoals hij is");
            if (update != null) h.Patch(update, postfix: new HarmonyMethod(typeof(Plugin), nameof(InvoerAan)));
            Type filter = AccessTools.TypeByName("MIBFilterInput");
            MethodInfo gfi = filter == null ? null : AccessTools.Method(filter, "GetFilterInput", new[] { typeof(int) });
            if (gfi != null) h.Patch(gfi, postfix: new HarmonyMethod(typeof(Plugin), nameof(Richt)));
            else L.LogWarning("MIBFilterInput.GetFilterInput niet gevonden: geen richtpunt uit het bordbeeld");
            int n = 0;
            n += HaakInvoer(h, "GetButton", nameof(KnopIngedrukt));
            n += HaakInvoer(h, "GetButtonDown", nameof(KnopNeer));
            n += HaakInvoer(h, "GetButtonUp", nameof(KnopOp));
            L.LogMessage("RavenGun.MenInBlack: Application.Quit() onderdrukt, security-challenge als gehaald (geen kast-shell); invoer uit het bordbeeld, "
                         + n + " van 3 knopaanroepen aangehaakt");
        }

        static int HaakInvoer(Harmony h, string naam, string prefix)
        {
            MethodInfo m = AccessTools.Method(typeof(Input), naam, new[] { typeof(string) });
            if (m == null) { L.LogWarning("UnityEngine.Input." + naam + "(string) niet gevonden"); return 0; }
            h.Patch(m, prefix: new HarmonyMethod(typeof(Plugin), prefix));
            return 1;
        }

        // Knopnaam -> bit in het bordbeeld (Indeling uit Bordbeeld.cs). -1 = niet van ons, de game krijgt het antwoord van Unity.
        static int Bit(string naam)
        {
            if (naam == null) return -1;
            switch (naam)
            {
                case "PlayerStart1": return Indeling.Start[0];
                case "PlayerStart2": return Indeling.Start[1];
                case "Player1Trigger1": return Indeling.Trekker[0];
                case "Player2Trigger1": return Indeling.Trekker[1];
                default: return -1;
            }
        }

        static bool KnopIngedrukt(string buttonName, ref bool __result)
        {
            int b = Bit(buttonName);
            if (b < 0) return true;
            __result = B.Ingedrukt(b);
            return false;
        }

        static bool KnopNeer(string buttonName, ref bool __result)
        {
            int b = Bit(buttonName);
            if (b < 0) return true;
            __result = B.Neer(b);
            return false;
        }

        static bool KnopOp(string buttonName, ref bool __result)
        {
            int b = Bit(buttonName);
            if (b < 0) return true;
            __result = B.Op(b);
            return false;
        }

        static FieldInfo uit;
        static int gemeldInvoer;

        // Elke MIBCore.Update berekent DisableInputs opnieuw uit GetJoystickNames(); wij zetten het er direct achter weer terug.
        static void InvoerAan()
        {
            if (uit == null) uit = AccessTools.Field(AccessTools.TypeByName("MIB.MIBCore"), "DisableInputs");
            if (uit == null) return;
            if (gemeldInvoer == 0 && (bool)uit.GetValue(null))
            {
                gemeldInvoer = 1;
                L.LogMessage("MIBCore.DisableInputs stond aan (de game ziet een joystick); op onwaar gezet, de invoer komt uit het bordbeeld");
            }
            uit.SetValue(null, false);
        }

        // MIBFilterInput.GetFilterInput(index): schermpunt van speler index+1 (oorsprong linksonder), 0..Screen.width/height.
        static void Richt(int index, ref Vector3 __result)
        {
            if (index < 0 || index > 1) return;
            B.Ververs();
            if (!B.Verbonden) return;
            Vector2 f = B.Fractie(index);
            __result = new Vector3(Mathf.Clamp01(f.x) * Screen.width, Mathf.Clamp01(f.y) * Screen.height, 0f);
        }

        // Munt 1 (rand omhoog) = een credit meer. De game telt zelf af bij een start; in freeplay doet dit niets zichtbaars.
        static int credits;
        static MethodInfo zetCredit;
        static ConstructorInfo creditData;

        void Update()
        {
            if (!B.Neer(Indeling.Munt1)) return;
            try
            {
                Type core = AccessTools.TypeByName("MIB.MIBCore");
                object inst = AccessTools.Property(core, "Instance").GetValue(null, null);
                if (inst == null) return;
                if (zetCredit == null) zetCredit = AccessTools.Method(core, "SetCreditValue");
                if (creditData == null) creditData = AccessTools.TypeByName("MIB.MIBData_Credits").GetConstructor(new[] { typeof(byte), typeof(byte), typeof(byte), typeof(byte) });
                FieldInfo u = AccessTools.Field(core, "UniversalCredits");
                credits = Math.Min(99, (int)u.GetValue(inst) + 1);
                zetCredit.Invoke(inst, new object[] { creditData.Invoke(new object[] { (byte)0, (byte)credits, (byte)0, (byte)99 }) });
                L.LogMessage("munt: credits = " + credits);
            }
            catch (Exception e) { L.LogWarning("munt niet doorgegeven: " + e.Message); }
        }

        static int quits;

        static bool GeenQuit()
        {
            // Een keer met de aanroeper in het logboek; daarna stil (ProcessMessage vraagt het elke berichtcyclus opnieuw).
            if (quits++ == 0) L.LogWarning("Application.Quit() van de game onderdrukt; aangeroepen vanuit:\n" + Environment.StackTrace);
            return false;
        }

        static FieldInfo teller;

        static void SecurityGehaald(object __instance)
        {
            if (teller == null) teller = AccessTools.Field(__instance.GetType(), "m_securityCounter");
            if (teller != null) teller.SetValue(__instance, 0);
        }
    }
}

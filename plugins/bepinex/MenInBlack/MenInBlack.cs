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
using System;
using System.Reflection;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;

namespace RavenGun.Bepinex.MenInBlack
{
    [BepInPlugin("nl.studiodamon.ravengun.meninblack", "RavenGun.MenInBlack", "1.0.0")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource L;

        void Awake()
        {
            L = Logger;
            Harmony h = new Harmony("nl.studiodamon.ravengun.meninblack");
            h.Patch(AccessTools.Method(typeof(UnityEngine.Application), "Quit", new Type[0]),
                    prefix: new HarmonyMethod(typeof(Plugin), nameof(GeenQuit)));
            MethodInfo update = AccessTools.Method(AccessTools.TypeByName("MIB.MIBCore"), "Update");
            if (update != null) h.Patch(update, prefix: new HarmonyMethod(typeof(Plugin), nameof(SecurityGehaald)));
            else L.LogWarning("MIB.MIBCore.Update niet gevonden: de security-challenge blijft zoals hij is");
            L.LogMessage("RavenGun.MenInBlack: Application.Quit() onderdrukt, security-challenge als gehaald (geen kast-shell)");
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

// SPDX-License-Identifier: GPL-3.0-or-later
// RavenGun-plugin voor Night Hunter - After Dark, Chapter 2 (UNIS, Unity, 64 bits, qumo2_en.exe), op de directe route zonder de kern
// van TeknoParrot.
//
// Wat de game op de kast doet (gelezen in Assembly-CSharp.dll, 2026-10-17): de geweren komen van een HID-bord (hid_for_qumo_*,
// myjvs_server.gun_is_fire) en worden gelezen door Game_Device_data_gun_core/_hardware; de game kent ook een muisroute
// (config_gun.GUN_INPUT_WAY.MOUSE: Input.mousePosition en de linkermuisknop). Munt, start, wapen en speciaal komen van
// toetsenbordobjecten (input_manage.init_input_obj_list) en het bord; het testmenu (zzp_houtai_*) leest het bord via
// zzp_houtai_read_hardware.get_num_by_hardware.
//
// Deze plugin zet de game op de muisroute en levert die uit het bordbeeld van RavenGun (shared/Bordbeeld.cs):
//   - config_gun.get_gun_input_way = MOUSE; Game_Device_data_gun_core.device_is_can_work = waar;
//   - get_gun_pos_by_mouse: het richtpunt als fractie 0..1, oorsprong linksonder (zoals change_mouse_positon_to_gun_pos);
//   - is_gun_fire: de trekker van die speler (bij enkel vuren de flank, anders vasthouden, zoals de muisroute);
//   - munt 1/2 -> input_obj_insert_coin.add_coin(1); start N -> input_obj_pass_movie.pass_movie + input_obj_start_game.start_game(N);
//     herladen N (Special) -> input_obj_big_power.big_power_work(N); granaat N (Change weapon) -> input_obj_change_bullet.change_weapon(N);
//     test -> het testmenu (MANAGE_STATUS.HOUTAI); daarin test = kiezen (queding_work), service = volgende (add_key_work);
//   - het testmenu leest de knoppen van het bord via get_num_by_hardware: die antwoorden komen ook uit het bordbeeld;
//   - het toetsenbord van de game staat uit (de toetsenbordobjecten, Input.GetKey*/GetAxis), behalve de pijlen links/rechts: dat
//     is het stuur (menu omhoog/omlaag van het bordbeeld, zoals het WartranTroopers-profiel van deze game: Wheel Left/Right);
//   - venster en resolutie uit teknoparrot.ini (test_screen.Start);
//   - de dongle (SenseLock Sense4, dog_hardware_sense_4; de oude DllBindFunc-route in dog_check) zegt ja.
// Gebouwd tegen Assembly-CSharp.dll van de game (refs.txt), zodat de enums van de game de echte typen zijn; de rest gaat via
// reflectie, zoals bij de andere plugins.
using System;
using System.Reflection;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using RavenGun.Bepinex.Gedeeld;
using UnityEngine;

namespace RavenGun.Bepinex.AfterDark2
{
    [BepInPlugin("nl.studiodamon.ravengun.afterdark2", "RavenGun.AfterDark2", "1.0.0")]
    [BepInDependency("com.tp.nhad2", BepInDependency.DependencyFlags.SoftDependency)]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource L;
        internal static Bordbeeld B;
        const int Spelers = 2;

        static int gezet, gevraagd;
        static bool verbergCursor;

        void Awake()
        {
            L = Logger;
            B = new Bordbeeld(L);
            TpPlugin.ZetUit("com.tp.nhad2", L);
            Harmony h = new Harmony("nl.studiodamon.ravengun.afterdark2");
            Type kern = AccessTools.TypeByName("Game_Device_data_gun_core");
            Haak(h, AccessTools.TypeByName("config_gun"), "get_gun_input_way", null, "Muisroute", null);
            Haak(h, kern, "device_is_can_work", new[] { typeof(int) }, "Waar", null);
            Haak(h, kern, "get_gun_pos_by_mouse", null, "Richtpunt", null);
            Haak(h, kern, "is_gun_fire", null, "Trekker", null);
            Haak(h, AccessTools.TypeByName("Game_Device_data_gun_hardware"), "is_gun_fire", null, "Trekker", null);
            Haak(h, AccessTools.TypeByName("input_manage"), "init_input_obj_list", null, "Overslaan", null);
            Haak(h, AccessTools.TypeByName("show_info_manage_for_objs"), "init_show_info_obj_list", null, "Overslaan", null);
            Haak(h, AccessTools.TypeByName("test_screen"), "Start", null, "Venster", null);
            Haak(h, AccessTools.TypeByName("zzp_houtai_manage_check_control"), "check_for_zzp_houtai_control", null, "TestmenuKnoppen", null);
            Haak(h, AccessTools.TypeByName("zzp_houtai_read_hardware"), "get_num_by_hardware", null, "BordWaarde", null);
            Haak(h, typeof(Input), "GetAxis", new[] { typeof(string) }, "Nul", null);
            Haak(h, typeof(Input), "GetAxisRaw", new[] { typeof(string) }, "Nul", null);
            Haak(h, typeof(Input), "GetKey", new[] { typeof(KeyCode) }, "Onwaar", null);
            Haak(h, typeof(Input), "GetKeyUp", new[] { typeof(KeyCode) }, "Onwaar", null);
            Haak(h, typeof(Input), "GetKeyDown", new[] { typeof(KeyCode) }, "Stuur", null);
            // De dongle: een SenseLock Sense4 (Sense4.dll, S4Enum/S4Open/S4VerifyPin) via dog_hardware_sense_4; zonder antwoord zet
            // game_run_manage_dog het eigen scherm "Please insert the dongle and restart the game" neer en blijft staan (TV 2026-10-17).
            // De oudere controles (dog_check: DllBindFunc) staan er ook nog in; die zeggen hier ook ja.
            // Gelezen in Assembly-CSharp.dll (2026-10-18): de game kijkt enkel naar de teruggegeven code (S4VerifyPin == 0); er komt
            // geen door de dongle berekende waarde in de game terecht. Wij zeggen dus alleen "ja" op die ene controle; de dongle zelf
            // (S4Execute, sleutels, dongle-gegevens) wordt niet nagebootst.
            Haak(h, AccessTools.TypeByName("dog_hardware_sense_4"), "is_dog_ok_hareware", null, "DongleSense4", null);
            Haak(h, AccessTools.TypeByName("dog_check_new"), "is_dog_ok_hareware", null, "DongleSense4", null);
            Haak(h, AccessTools.TypeByName("dog_check_new"), "is_dog_ok_check_real", null, "DongleSense4", null);
            Haak(h, AccessTools.TypeByName("dog_check_new"), "is_dog_ok_in_start", null, "DongleBijStart", null);
            Haak(h, AccessTools.TypeByName("dog_check_new"), "is_dog_ok_in_update", null, "Waar", null);
            // De oude DllBindFunc-route hoeft niet eens meer te starten (BIND_InitDongle e.d.): overslaan.
            foreach (string m in new[] { "init_dog_work", "init_dog_work_real", "init_dog_when_run" })
                Haak(h, AccessTools.TypeByName("dog_check"), m, null, "Overslaan", null);
            foreach (string m in new[] { "dog_is_ok", "dog_is_ok_real", "is_dog_ok_when_run", "is_dog_ok_when_run_core",
                                         "is_dog_ok_when_game_run_update_work", "DongleVerify", "is_dog_ok_hareware" })
                Haak(h, AccessTools.TypeByName("dog_check"), m, null, "Waar", null);
            L.LogMessage("RavenGun.AfterDark2: " + gezet + " van " + gevraagd + " haken gezet");
            // Ook in output_log.txt van Unity, voor als het schijflogboek van BepInEx ontbreekt.
            Debug.Log("RavenGun.AfterDark2: geladen, " + gezet + " van " + gevraagd + " haken gezet");
            verbergCursor = TpIni.Lees().Waar("General/HideCursor", true);
        }

        static void Haak(Harmony h, Type t, string naam, Type[] args, string prefix, string postfix)
        {
            gevraagd++;
            MethodInfo m = t == null ? null : (args == null ? AccessTools.Method(t, naam) : AccessTools.Method(t, naam, args));
            if (m == null) { L.LogWarning("niet gevonden: " + (t == null ? "?" : t.FullName) + "." + naam); return; }
            try
            {
                h.Patch(m, prefix: prefix == null ? null : new HarmonyMethod(typeof(Plugin), prefix),
                        postfix: postfix == null ? null : new HarmonyMethod(typeof(Plugin), postfix));
                gezet++;
            }
            catch (Exception e) { L.LogWarning("haak mislukt: " + t.FullName + "." + naam + ": " + e.Message); }
        }

        static bool Overslaan() { return false; }

        static bool DongleBijStart(ref bool __result)
        {
            AccessTools.Field(AccessTools.TypeByName("dog_check_new"), "myis_has_dog_in_update").SetValue(null, true);
            __result = true;
            return false;
        }
        static bool dongleGemeld;
        // De dongle-controle (S4Enum/S4Open/S4VerifyPin == 0) zegt ja; een keer in het logboek, als bewijs dat de game niet bij het
        // dongle-scherm blijft staan.
        static bool DongleSense4(ref bool __result)
        {
            __result = true;
            if (!dongleGemeld)
            {
                dongleGemeld = true;
                L.LogMessage("dongle: de Sense4-controle (S4VerifyPin) is overgeslagen en zegt ja");
                Debug.Log("RavenGun.AfterDark2: dongle: Sense4-controle zegt ja");
            }
            return false;
        }
        static bool Waar(ref bool __result) { __result = true; return false; }
        static bool Onwaar(ref bool __result) { __result = false; return false; }
        static bool Nul(ref float __result) { __result = 0f; return false; }

        // Gebouwd tegen Assembly-CSharp.dll van de game (refs.txt): de enums als resultaat en argument zijn de echte typen.
        static bool Muisroute(ref config_gun.GUN_INPUT_WAY __result)
        {
            __result = config_gun.GUN_INPUT_WAY.MOUSE;
            return false;
        }

        static int Speler(object apparaat)
        {
            MethodInfo m = AccessTools.Method(apparaat.GetType(), "get_player_num");
            return m == null ? 0 : (int)m.Invoke(apparaat, null) - 1;
        }

        static bool Richtpunt(object __instance, ref Vector3 __result)
        {
            int p = Speler(__instance);
            B.Ververs();
            if (p < 0 || p >= Spelers || !B.Verbonden) { __result = new Vector3(-1f, -1f, 0f); return false; }
            Vector2 f = B.Fractie(p);
            __result = new Vector3(f.x, f.y, 0f);
            return false;
        }

        static bool Trekker(object __instance, ref bool __result)
        {
            int p = Speler(__instance);
            if (p < 0 || p >= Spelers) { __result = false; return false; }
            MethodInfo enkel = AccessTools.Method(__instance.GetType(), "is_single_fire");
            bool flank = enkel != null && (bool)enkel.Invoke(__instance, null);
            __result = flank ? B.Neer(Indeling.Trekker[p]) : B.Ingedrukt(Indeling.Trekker[p]);
            return false;
        }

        // Pijl links/rechts = het stuur (Wheel Left/Right van het profiel: menu omhoog/omlaag in het bordbeeld); de rest van het
        // toetsenbord bestaat voor de game niet.
        static bool Stuur(KeyCode key, ref bool __result)
        {
            __result = key == KeyCode.LeftArrow ? B.Neer(Indeling.VolumeOp) : key == KeyCode.RightArrow && B.Neer(Indeling.VolumeNeer);
            return false;
        }

        static bool Venster()
        {
            TpIni ini = TpIni.Lees();
            bool vol = !ini.Waar("General/Windowed", false);
            int w = ini.Waar("General/CustomResolution", false) ? ini.Getal("General/ResolutionWidth", 1920) : Screen.currentResolution.width;
            int hg = ini.Waar("General/CustomResolution", false) ? ini.Getal("General/ResolutionHeight", 1080) : Screen.currentResolution.height;
            Screen.SetResolution(w, hg, vol);
            L.LogMessage("venster: " + w + "x" + hg + (vol ? " schermvullend" : " in een venster"));
            return false;
        }

        static bool TestmenuKnoppen(object __instance)
        {
            if (B.Neer(Indeling.Service)) Roep(__instance, "add_key_work");
            if (B.Neer(Indeling.Test)) Roep(__instance, "queding_work");
            return false;
        }

        // HARDWARD_READ_TYPE (zzp_houtai_read_hardware.cs): 0 munt, 1 reset, 2 test, 3 service, 4/5 noodstop, 6/7 stuur links/rechts,
        // 8/9 wijsvinger 1P/2P, 10/11 duim 1P/2P, 12/13 wapen wisselen 1P/2P, 14 volume.
        static bool BordWaarde(zzp_houtai_read_hardware.HARDWARD_READ_TYPE HARDWARD_TYPE1, ref int __result)
        {
            int t = (int)HARDWARD_TYPE1;
            bool aan;
            switch (t)
            {
                case 0: aan = B.Ingedrukt(Indeling.Munt1) || B.Ingedrukt(Indeling.Munt2); break;
                case 1: case 3: aan = B.Ingedrukt(Indeling.Service); break;
                case 2: aan = B.Ingedrukt(Indeling.Test); break;
                case 6: aan = B.Ingedrukt(Indeling.VolumeOp); break;
                case 7: aan = B.Ingedrukt(Indeling.VolumeNeer); break;
                case 8: case 9: aan = B.Ingedrukt(Indeling.Trekker[t - 8]); break;
                case 10: case 11: aan = B.Ingedrukt(Indeling.Herladen[t - 10]); break;
                case 12: case 13: aan = B.Ingedrukt(Indeling.Granaat[t - 12]); break;
                default: aan = false; break;
            }
            __result = aan ? 1 : 0;
            return false;
        }

        static void Roep(object o, string naam)
        {
            MethodInfo m = o == null ? null : AccessTools.Method(o.GetType(), naam);
            if (m != null) m.Invoke(o, null);
        }

        static void Statisch(string type, string naam, params object[] args)
        {
            MethodInfo m = AccessTools.Method(AccessTools.TypeByName(type), naam);
            if (m == null) { L.LogWarning("niet gevonden: " + type + "." + naam); return; }
            try { m.Invoke(null, args); }
            catch (Exception e) { L.LogWarning(type + "." + naam + ": " + (e.InnerException ?? e).Message); }
        }

        void Update()
        {
            if (verbergCursor && Cursor.visible) Cursor.visible = false;
            B.Ververs();
            if (!B.Verbonden) return;
            if (game_run_manage_normal.static_get_curr_manage_status_name() == game_run_manage_normal.MANAGE_STATUS.HOUTAI) return;
            for (int p = 0; p < Spelers; p++)
            {
                if (B.Neer(Indeling.Granaat[p])) Statisch("input_obj_change_bullet", "change_weapon", p + 1);
                if (B.Neer(Indeling.Herladen[p])) Statisch("input_obj_big_power", "big_power_work", p + 1);
            }
            if (B.Neer(Indeling.Munt1)) Statisch("input_obj_insert_coin", "add_coin", 1);
            if (B.Neer(Indeling.Munt2)) Statisch("input_obj_insert_coin", "add_coin", 1);
            if (B.Neer(Indeling.Test)) game_run_manage_normal.static_set_curr_manage_status(game_run_manage_normal.MANAGE_STATUS.HOUTAI);
            for (int p = 0; p < Spelers; p++)
            {
                if (!B.Neer(Indeling.Start[p])) continue;
                Statisch("input_obj_pass_movie", "pass_movie");
                Statisch("input_obj_start_game", "start_game", p + 1);
            }
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Gedeelde code voor de BepInEx-plugins van de directe route (plugins/bepinex/<Game>/, scripts/build-bepinex-plugin.sh neemt deze map
// altijd mee). Drie dingen:
//
//   Bordbeeld  het invoerbeeld van RavenGun IN het spelproces: de Windows-mapping "TeknoParrot_JvsState" (64 bytes) die onze eigen
//              brug rg-jvsbridge.exe in de prefix aanmaakt en vult uit /dev/shm/ravengun_JvsState van de daemon (tools/rg-direct,
//              third_party/rg-jvsbridge). De naam is die van het bestaande formaat; er draait niets van TeknoParrot voor. De indeling is
//              WartranTroopers uit packaging/tp-shm-profiles.json: een 32-bits bitveld op offset 8, een byte per as op 12..19 (links/boven
//              = 0). tests/test_bepinex_bordbeeld.py vergelijkt de tabellen hieronder met dat bestand.
//   TpIni      teknoparrot.ini naast de exe (de launcher schrijft hem uit [Config] van game.ini): Windowed, HideCursor, resolutie. Zo
//              blijven de opties van de app werken zonder de kern die ze vroeger las.
//   TpPlugin   een TeknoParrot-plugin die in BepInEx/plugins van de spelmap staat (de view kopieert BepInEx/ lokaal) uitzetten: zijn
//              Harmony-patches eraf en zijn Update stil. Zonder de gesloten kern krijgt zo'n plugin toch geen antwoord meer.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using BepInEx;
using BepInEx.Bootstrap;
using BepInEx.Logging;
using HarmonyLib;
using UnityEngine;

namespace RavenGun.Bepinex.Gedeeld
{
    public static class Indeling
    {
        // packaging/tp-shm-profiles.json, profiel WartranTroopers. Niet wijzigen zonder dat bestand (de test vergelijkt ze).
        public const int Grootte = 64;
        public const int BitsOffset = 8;
        public const int AsOffset = 12;
        public const int Test = 0;
        public const int Service = 1;
        public const int Munt1 = 2;
        public const int Munt2 = 3;
        public const int VolumeOp = 4;
        public const int VolumeNeer = 5;
        public static readonly int[] Start = { 6, 11, 16, 21 };
        public static readonly int[] Trekker = { 7, 12, 17, 22 };
        public static readonly int[] Herladen = { 8, 13, 18, 23 };
        public static readonly int[] Granaat = { 9, 14, 19, 24 };
        public static readonly int[] Aux1 = { 10, 15, 20, 25 };
        // De kern las een as <= 1 of >= 254 als buiten beeld (offscreen_margin van de IO-brug).
        public const int BuitenOnder = 1;
        public const int BuitenBoven = 254;
    }

    public sealed class Bordbeeld
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr OpenFileMappingW(uint toegang, bool erven, string naam);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr MapViewOfFile(IntPtr map, uint toegang, uint hoog, uint laag, UIntPtr bytes);

        const uint FILE_MAP_READ = 4;
        public const string MappingNaam = "TeknoParrot_JvsState";

        readonly ManualLogSource log;
        IntPtr beeld = IntPtr.Zero;
        int volgendePoging;
        int frame = -1;
        uint bits, vorigeBits;
        readonly byte[] as8 = new byte[8];

        public Bordbeeld(ManualLogSource log) { this.log = log; }

        public bool Verbonden { get { return beeld != IntPtr.Zero; } }
        public uint Bits { get { return bits; } }

        // Een keer per frame lezen (de eerste aanroep in een frame); alle vragen in dat frame zien hetzelfde beeld, zodat een flank
        // (Neer) in een frame overal gelijk is, hoe vaak de game een methode ook aanroept.
        public void Ververs()
        {
            int f = Time.frameCount;
            if (f == frame) return;
            frame = f;
            if (beeld == IntPtr.Zero && !Open()) { vorigeBits = bits = 0; return; }
            vorigeBits = bits;
            bits = unchecked((uint)Marshal.ReadInt32(beeld, Indeling.BitsOffset));
            for (int i = 0; i < 8; i++) as8[i] = Marshal.ReadByte(beeld, Indeling.AsOffset + i);
        }

        bool Open()
        {
            int nu = Environment.TickCount;
            if (nu - volgendePoging < 0) return false;
            volgendePoging = nu + 1000;   // de brug start voor de game; een keer per seconde opnieuw is ruim genoeg
            IntPtr map = OpenFileMappingW(FILE_MAP_READ, false, MappingNaam);
            if (map == IntPtr.Zero) return false;
            beeld = MapViewOfFile(map, FILE_MAP_READ, 0, 0, new UIntPtr((uint)Indeling.Grootte));
            if (beeld != IntPtr.Zero && log != null) log.LogMessage("bordbeeld: " + MappingNaam + " geopend");
            return beeld != IntPtr.Zero;
        }

        public bool Ingedrukt(int bit) { Ververs(); return ((bits >> bit) & 1u) != 0; }
        public bool Neer(int bit) { Ververs(); return ((bits >> bit) & 1u) != 0 && ((vorigeBits >> bit) & 1u) == 0; }
        public bool Op(int bit) { Ververs(); return ((bits >> bit) & 1u) == 0 && ((vorigeBits >> bit) & 1u) != 0; }

        public byte AsByte(int speler, bool y) { Ververs(); return as8[speler * 2 + (y ? 1 : 0)]; }

        public bool BuitenBeeld(int speler)
        {
            byte x = AsByte(speler, false), y = AsByte(speler, true);
            return x <= Indeling.BuitenOnder || x >= Indeling.BuitenBoven || y <= Indeling.BuitenOnder || y >= Indeling.BuitenBoven;
        }

        // Fractie 0..1 met de oorsprong LINKSONDER, zoals Unity (Input.mousePosition, UI-ankers): de y van het bordbeeld (boven = 0)
        // wordt omgekeerd.
        public Vector2 Fractie(int speler)
        {
            return new Vector2(AsByte(speler, false) / 255f, 1f - AsByte(speler, true) / 255f);
        }
    }

    // teknoparrot.ini naast de exe: "[Sectie]" en "Naam=waarde". Ontbreekt hij, dan gelden de standaardwaarden van de aanroeper.
    public sealed class TpIni
    {
        readonly Dictionary<string, string> w = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        public static TpIni Lees()
        {
            TpIni t = new TpIni();
            try
            {
                string pad = Path.Combine(Paths.GameRootPath, "teknoparrot.ini");
                if (!File.Exists(pad)) return t;
                string sectie = "";
                foreach (string ruw in File.ReadAllLines(pad))
                {
                    string r = ruw.Trim();
                    if (r.Length == 0 || r[0] == ';' || r[0] == '#') continue;
                    if (r[0] == '[' && r.EndsWith("]")) { sectie = r.Substring(1, r.Length - 2).Trim(); continue; }
                    int i = r.IndexOf('=');
                    if (i > 0) t.w[sectie + "/" + r.Substring(0, i).Trim()] = r.Substring(i + 1).Trim();
                }
            }
            catch (Exception) { }
            return t;
        }

        public string Get(string sleutel, string standaard)
        {
            string v;
            return w.TryGetValue(sleutel, out v) ? v : standaard;
        }

        public bool Waar(string sleutel, bool standaard)
        {
            string v = Get(sleutel, null);
            if (v == null) return standaard;
            return v == "1" || v.Equals("true", StringComparison.OrdinalIgnoreCase);
        }

        public int Getal(string sleutel, int standaard)
        {
            int n;
            return int.TryParse(Get(sleutel, null), out n) ? n : standaard;
        }
    }

    public static class TpPlugin
    {
        // Laadt na de TeknoParrot-plugin (BepInDependency, zacht) en zet hem dan uit. Bestaat hij niet, dan gebeurt er niets.
        public static void ZetUit(string guid, ManualLogSource log)
        {
            PluginInfo info;
            if (!Chainloader.PluginInfos.TryGetValue(guid, out info)) return;
            Harmony.UnpatchID(guid);
            if (info.Instance != null) info.Instance.enabled = false;
            log.LogMessage("TeknoParrot-plugin " + guid + " uitgezet (patches eraf, Update stil); RavenGun bedient de invoer");
        }
    }
}

// RavenGun-plugin voor Unity-arcadegames (Adrenaline Amusements): draadprotocol versie 1.
//
// Dit is het protocol dat de daemon (ravengund, src/ravengun/games/aagames.*) al spreekt. Het staat beschreven in
// docs/architecture.md ("Unity-route") en is door de daemon-ontwikkelaars zelf vastgesteld door de verbinding van buitenaf
// te observeren; deze plugin is een eigen implementatie van die beschrijving (clean-room, zie docs/decisions.md).
//
// Alles little-endian, zonder padding, vier spelers per array (speler 1..4).
//
// Daemon naar plugin (invoer), een pakket van vaste lengte per bericht, zonder lengte of kop. Velden op alfabetische
// volgorde van de veldnaam:
//   Axis_X float[4], Axis_Y float[4]   schermpixels van het gamevenster, Y-oorsprong onderaan (Unity-conventie)
//   EnableInputsHack u8, HideCrosshairs u8
//   Reload u8[4] (alleen Tomb Raider), Trigger u8[4]         1 = ingedrukt
//   Tomb Raider: 42 bytes, Rabbids Hollywood: 38 bytes.
//
// Plugin naar daemon (uitvoer): int32 lengte (= payload + 1), u8 koptype (2 = uitvoer), 60 bytes payload:
//   Tomb Raider: Ammo i32[4], Credits i32[4], Damaged u8[4], IsPlaying u8[4], Life f32[4], Recoil u8[4]
//   Rabbids:     Ammo i32[4], Credits i32[4], Damaged u8[4], Life f32[4], Recoil u8[4], StartLamp u8[4]
//   Recoil en Damaged zijn eenmalige vlaggen: elk bericht met Recoil[p] != 0 is een schot van speler p.
using System;

namespace RavenGun.Unity
{
    public enum Variant { TombRaider, RabbidsHollywood }

    public sealed class InputState
    {
        public const int Players = 4;
        public readonly float[] X = new float[Players];
        public readonly float[] Y = new float[Players];
        public readonly bool[] Trigger = new bool[Players];
        public readonly bool[] Reload = new bool[Players];
        public bool Enable;          // EnableInputsHack
        public bool HideCrosshairs;  // vraag van de daemon om het vizier te verbergen

        public void CopyFrom(InputState o)
        {
            Array.Copy(o.X, X, Players);
            Array.Copy(o.Y, Y, Players);
            Array.Copy(o.Trigger, Trigger, Players);
            Array.Copy(o.Reload, Reload, Players);
            Enable = o.Enable;
            HideCrosshairs = o.HideCrosshairs;
        }

        public void Clear()
        {
            for (int i = 0; i < Players; i++) { X[i] = 0; Y[i] = 0; Trigger[i] = false; Reload[i] = false; }
            Enable = false;
            HideCrosshairs = false;
        }
    }

    public sealed class OutputState
    {
        public const int Players = 4;
        public readonly int[] Ammo = new int[Players];
        public readonly int[] Credits = new int[Players];
        public readonly byte[] Damaged = new byte[Players];
        public readonly byte[] Playing = new byte[Players];   // Tomb Raider: IsPlaying; Rabbids: StartLamp
        public readonly float[] Life = new float[Players];
        public readonly byte[] Recoil = new byte[Players];

        public void CopyFrom(OutputState o)
        {
            Array.Copy(o.Ammo, Ammo, Players);
            Array.Copy(o.Credits, Credits, Players);
            Array.Copy(o.Damaged, Damaged, Players);
            Array.Copy(o.Playing, Playing, Players);
            Array.Copy(o.Life, Life, Players);
            Array.Copy(o.Recoil, Recoil, Players);
        }

        // Gelijk zonder de eenmalige vlaggen: voor "is er iets veranderd".
        public bool SameStatus(OutputState o)
        {
            for (int i = 0; i < Players; i++)
                if (Ammo[i] != o.Ammo[i] || Credits[i] != o.Credits[i] || Playing[i] != o.Playing[i] || Life[i] != o.Life[i])
                    return false;
            return true;
        }
    }

    public static class Protocol
    {
        public const int OutputPayload = 60;
        public const byte HeaderOutputs = 2;

        public static int InputSize(Variant v) { return v == Variant.TombRaider ? 42 : 38; }

        static float F32(byte[] b, int o) { return BitConverter.ToSingle(LeBytes(b, o, 4), 0); }

        // BitConverter volgt de byte-volgorde van de machine; het protocol is little-endian. Op x86-64 is dat gelijk,
        // maar we draaien niet af op die aanname.
        static byte[] LeBytes(byte[] b, int o, int n)
        {
            byte[] r = new byte[n];
            Array.Copy(b, o, r, 0, n);
            if (!BitConverter.IsLittleEndian) Array.Reverse(r);
            return r;
        }

        static void PutI32(byte[] b, int o, int v)
        {
            b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 16); b[o + 3] = (byte)(v >> 24);
        }

        static void PutF32(byte[] b, int o, float v)
        {
            byte[] r = BitConverter.GetBytes(v);
            if (!BitConverter.IsLittleEndian) Array.Reverse(r);
            Array.Copy(r, 0, b, o, 4);
        }

        // Leest een invoerpakket uit buf[offset..offset+InputSize). Getallen die geen eindige float zijn worden 0.
        public static void ParseInput(Variant v, byte[] buf, int offset, InputState into)
        {
            int p = offset;
            for (int i = 0; i < 4; i++) { into.X[i] = Finite(F32(buf, p)); p += 4; }
            for (int i = 0; i < 4; i++) { into.Y[i] = Finite(F32(buf, p)); p += 4; }
            into.Enable = buf[p++] == 1;
            into.HideCrosshairs = buf[p++] == 1;
            if (v == Variant.TombRaider) for (int i = 0; i < 4; i++) into.Reload[i] = buf[p++] == 1;
            else for (int i = 0; i < 4; i++) into.Reload[i] = false;
            for (int i = 0; i < 4; i++) into.Trigger[i] = buf[p++] == 1;
        }

        static float Finite(float f)
        {
            return (float.IsNaN(f) || float.IsInfinity(f)) ? 0f : f;
        }

        // Bouwt een uitvoerbericht (5 bytes kop + 60 bytes).
        public static byte[] BuildOutput(Variant v, OutputState s)
        {
            byte[] m = new byte[5 + OutputPayload];
            PutI32(m, 0, OutputPayload + 1);
            m[4] = HeaderOutputs;
            int p = 5;
            for (int i = 0; i < 4; i++) { PutI32(m, p, s.Ammo[i]); p += 4; }
            for (int i = 0; i < 4; i++) { PutI32(m, p, s.Credits[i]); p += 4; }
            for (int i = 0; i < 4; i++) m[p++] = s.Damaged[i];
            if (v == Variant.TombRaider)
            {
                for (int i = 0; i < 4; i++) m[p++] = s.Playing[i];
                for (int i = 0; i < 4; i++) { PutF32(m, p, s.Life[i]); p += 4; }
                for (int i = 0; i < 4; i++) m[p++] = s.Recoil[i];
            }
            else
            {
                for (int i = 0; i < 4; i++) { PutF32(m, p, s.Life[i]); p += 4; }
                for (int i = 0; i < 4; i++) m[p++] = s.Recoil[i];
                for (int i = 0; i < 4; i++) m[p++] = s.Playing[i];
            }
            return m;
        }
    }
}

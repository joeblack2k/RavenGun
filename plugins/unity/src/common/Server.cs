// TCP-server van de plugin: luistert alleen op 127.0.0.1, accepteert een klant tegelijk (een nieuwe verbinding vervangt de
// oude, zodat een herstarte daemon altijd binnenkomt), ontvangt invoerpakketten van vaste lengte en verstuurt
// uitvoerberichten. Alle netwerkwerk gebeurt op eigen threads; de game-thread raakt alleen de gedeelde toestand aan
// onder een korte lock en blokkeert nooit op het netwerk. Elke fout sluit de klant en laat de game doorlopen.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace RavenGun.Unity
{
    public sealed class Server
    {
        readonly Variant variant;
        readonly int port;
        readonly Action<string> log;
        readonly Action<string> logVerbose;
        readonly object gate = new object();
        readonly InputState latest = new InputState();
        // Een korte druk (korter dan een gameframe) mag niet verloren gaan: elke 1 in een pakket blijft tot de volgende Snapshot staan.
        readonly bool[] latchTrigger = new bool[4];
        readonly bool[] latchReload = new bool[4];
        readonly Queue<byte[]> outbox = new Queue<byte[]>();
        readonly Stopwatch clock = Stopwatch.StartNew();
        Thread thread;
        TcpListener listener;
        volatile bool stop;
        volatile bool connected;
        long lastPacketMs = -1;
        long packets;
        long connects;
        long dropsOutbox;
        string lastError = "";
        Func<byte[]> hello;       // levert het bericht dat bij elke nieuwe verbinding meteen gaat
        const int MaxOutbox = 512;

        public Server(Variant v, int port, Action<string> log, Action<string> logVerbose)
        {
            variant = v; this.port = port; this.log = log; this.logVerbose = logVerbose;
        }

        public bool Connected { get { return connected; } }
        public long Packets { get { lock (gate) return packets; } }
        public long Connects { get { lock (gate) return connects; } }
        public string LastError { get { lock (gate) return lastError; } }

        public void Start(Func<byte[]> helloMessage)
        {
            hello = helloMessage;
            thread = new Thread(Run);
            thread.IsBackground = true;
            thread.Name = "RavenGun-net";
            thread.Start();
        }

        public void Stop()
        {
            stop = true;
            try { if (thread != null) thread.Join(500); } catch (Exception) { }
        }

        // Laatste invoer en de leeftijd ervan in ms (-1: nog nooit een pakket).
        public long Snapshot(InputState into)
        {
            lock (gate)
            {
                into.CopyFrom(latest);
                for (int i = 0; i < 4; i++)
                {
                    if (latchTrigger[i]) into.Trigger[i] = true;
                    if (latchReload[i]) into.Reload[i] = true;
                    latchTrigger[i] = false;
                    latchReload[i] = false;
                }
                return lastPacketMs < 0 ? -1 : clock.ElapsedMilliseconds - lastPacketMs;
            }
        }

        // Zet een uitvoerbericht in de wachtrij; zonder klant of bij een volle wachtrij gaat het oudste verloren.
        public void Send(byte[] msg)
        {
            lock (gate)
            {
                if (!connected) return;
                if (outbox.Count >= MaxOutbox) { outbox.Dequeue(); dropsOutbox++; }
                outbox.Enqueue(msg);
            }
        }

        void SetError(string e) { lock (gate) lastError = e; }

        void Run()
        {
            while (!stop)
            {
                if (listener == null)
                {
                    try
                    {
                        listener = new TcpListener(IPAddress.Loopback, port);
                        listener.Start(2);
                        log("TCP-server luistert op 127.0.0.1:" + port);
                        SetError("");
                    }
                    catch (Exception e)
                    {
                        SetError("luisteren op poort " + port + " mislukt: " + e.Message);
                        log("Luisteren op 127.0.0.1:" + port + " mislukt (" + e.Message + "); opnieuw proberen over 2 s. De game loopt door.");
                        try { if (listener != null) listener.Stop(); } catch (Exception) { }
                        listener = null;
                        SleepWhile(2000);
                        continue;
                    }
                }
                try
                {
                    if (!listener.Pending()) { Thread.Sleep(20); continue; }
                    TcpClient c = listener.AcceptTcpClient();
                    Serve(c);
                }
                catch (Exception e)
                {
                    SetError("serverfout: " + e.Message);
                    log("Serverfout: " + e.Message + "; de listener wordt opnieuw opgezet.");
                    try { listener.Stop(); } catch (Exception) { }
                    listener = null;
                    SleepWhile(500);
                }
            }
            try { if (listener != null) listener.Stop(); } catch (Exception) { }
        }

        void SleepWhile(int ms)
        {
            int t = 0;
            while (!stop && t < ms) { Thread.Sleep(20); t += 20; }
        }

        // Bedient een klant tot die weggaat, de server stopt of er een nieuwe verbinding aankomt (die vervangt deze).
        // Een enkele thread leest en schrijft: dat houdt de volgorde eenvoudig en de threads weinig.
        void Serve(TcpClient c)
        {
            lock (gate) { connects++; outbox.Clear(); latest.Clear(); lastPacketMs = -1; Array.Clear(latchTrigger, 0, 4); Array.Clear(latchReload, 0, 4); }
            try { c.NoDelay = true; c.SendTimeout = 250; } catch (Exception) { }
            NetworkStream ns = null;
            int need = Protocol.InputSize(variant);
            byte[] acc = new byte[need * 8];
            int have = 0;
            InputState tmp = new InputState();
            try
            {
                ns = c.GetStream();
                connected = true;
                log("Daemon verbonden.");
                byte[] hi = hello != null ? hello() : null;
                if (hi != null) ns.Write(hi, 0, hi.Length);
                while (!stop)
                {
                    bool worked = false;
                    // Lezen
                    if (c.Available > 0)
                    {
                        int room = acc.Length - have;
                        if (room <= 0) { have = 0; room = acc.Length; }   // uit de pas: begin opnieuw
                        int n = ns.Read(acc, have, room);
                        if (n <= 0) break;
                        have += n;
                        int off = 0;
                        bool got = false;
                        while (have - off >= need)
                        {
                            Protocol.ParseInput(variant, acc, off, tmp);   // het laatste volledige pakket is de stand; drukken van eerdere tellen via de latch
                            off += need;
                            got = true;
                            lock (gate)
                            {
                                packets++;
                                for (int i = 0; i < 4; i++)
                                {
                                    if (tmp.Trigger[i]) latchTrigger[i] = true;
                                    if (tmp.Reload[i]) latchReload[i] = true;
                                }
                            }
                        }
                        if (off > 0) { Buffer.BlockCopy(acc, off, acc, 0, have - off); have -= off; }
                        if (got)
                        {
                            lock (gate) { latest.CopyFrom(tmp); lastPacketMs = clock.ElapsedMilliseconds; }
                        }
                        worked = true;
                    }
                    // Schrijven
                    byte[] m = null;
                    lock (gate) { if (outbox.Count > 0) m = outbox.Dequeue(); }
                    if (m != null) { ns.Write(m, 0, m.Length); worked = true; }
                    // Weggevallen? (Poll meldt lezen mogelijk zonder data bij een gesloten verbinding.)
                    if (!worked)
                    {
                        if (c.Client.Poll(0, SelectMode.SelectRead) && c.Available == 0) break;
                        if (listenerHasPending()) break;
                        Thread.Sleep(2);
                    }
                }
            }
            catch (Exception e)
            {
                SetError("verbinding verbroken: " + e.Message);
                logVerbose("Verbinding met de daemon verbroken: " + e.Message);
            }
            finally
            {
                connected = false;
                lock (gate) { outbox.Clear(); latest.Clear(); lastPacketMs = -1; }
                try { if (ns != null) ns.Close(); } catch (Exception) { }
                try { c.Close(); } catch (Exception) { }
                log("Daemon losgekoppeld.");
            }
        }

        // Een nieuwe verbinding in de wachtrij vervangt de huidige klant.
        bool listenerHasPending()
        {
            try { return listener != null && listener.Pending(); } catch (Exception) { return false; }
        }
    }
}

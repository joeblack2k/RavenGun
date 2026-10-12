// rg-jvsbridge: RavenGuns eigen vervanger voor TeknoParrots pipehelper.exe.
//
// Doel: de laatste TeknoParrot-binary uit de draai-route halen. Deze Windows-brug (draait in de Wine-prefix
// naast de game) legt de JVS-koppeling tussen de game en de Linux-kant van RavenGun (de daemon /
// JvsPipeServer in scripts/launch-direct.py).
//
// CLEAN-ROOM: geschreven vanuit de GEDRAGSSPEC (docs/research/eigen-runtime-inventaris.md en de meting van
// pipehelper via strings/objdump/disassemblage), NIET vanuit TeknoParrots pipehelper.c (GPL-3). Alleen het
// waarneembare gedrag en de opdrachtregel zijn overgenomen, geen broncode-expressie.
//
// Twee modi, exact zoals launch-direct.py ze aanroept:
//   pipe-modus:  rg-jvsbridge <pijpnaam> <host> <poort> [<shmnaam> <shmgrootte> <shmbronpad>]
//   shm-modus:   rg-jvsbridge shm <shmnaam> <shmgrootte> <shmbronpad>
//
// De stderr-regels beginnen met "pipehelper: " en bevatten "created" resp. "shm mirror active", omdat
// launch-direct.py daarop wacht (run()): zo hoeft de Linux-kant niet te wijzigen.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPE_PREFIX "\\\\.\\pipe\\"
#define CHUNK_READ  0x1000
#define CHUNK_WRITE 0x100

static void logln(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "pipehelper: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

// --- gedeelde shm-weergaven (de verbind-vlag op byte 0 wordt vóór elke host->pipe-chunk gespiegeld) -----
static volatile unsigned char *g_wine = NULL;   // de Windows named mapping (wat de game ziet)
static volatile unsigned char *g_host = NULL;    // de /dev/shm-bron (wat de Linux-kant schrijft)

// --- shm-spiegel: drie-weg per byte, bron (host) wint ---------------------------------------------------
typedef struct { const char *name; long size; const char *path; } ShmArgs;

static DWORD WINAPI shm_mirror(LPVOID arg) {
    ShmArgs *a = (ShmArgs *)arg;
    if (a->size <= 0) { logln("invalid shm size"); ExitProcess(1); }

    HANDLE winMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)a->size, a->name);
    DWORD existed = GetLastError();
    if (!winMap) { logln("CreateFileMapping(%s) failed: %lu", a->name, GetLastError()); return 1; }
    unsigned char *wine = (unsigned char *)MapViewOfFile(winMap, FILE_MAP_ALL_ACCESS, 0, 0, a->size);
    if (!wine) { logln("MapViewOfFile(%s) failed: %lu", a->name, GetLastError()); return 1; }
    g_wine = wine;
    logln("shm mirror active (%s <-> %s, %ld bytes) %s", a->name, a->path, a->size,
          existed == ERROR_ALREADY_EXISTS ? "[OPENED PRE-EXISTING MAPPING]" : "[created fresh]");

    HANDLE hf = CreateFileA(a->path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
    if (hf == INVALID_HANDLE_VALUE) { logln("cannot open host shm file"); return 1; }
    HANDLE hostMap = CreateFileMappingA(hf, NULL, PAGE_READWRITE, 0, (DWORD)a->size, NULL);
    if (!hostMap) { logln("CreateFileMapping(host file) failed: %lu", GetLastError()); return 1; }
    unsigned char *host = (unsigned char *)MapViewOfFile(hostMap, FILE_MAP_ALL_ACCESS, 0, 0, a->size);
    if (!host) { logln("MapViewOfFile(host file) failed: %lu", GetLastError()); return 1; }
    g_host = host;

    unsigned char *snap = (unsigned char *)malloc(a->size);
    if (!snap) return 1;
    // startkopie: host wint
    memcpy(wine, host, a->size);
    memcpy(snap, host, a->size);

    for (;;) {
        for (long i = 0; i < a->size; i++) {
            if (host[i] != snap[i]) { wine[i] = host[i]; snap[i] = host[i]; }      // bron wint
            else if (wine[i] != snap[i]) { host[i] = wine[i]; snap[i] = wine[i]; } // anders game -> bron
        }
        Sleep(1);
    }
    // onbereikbaar
}

// --- pipe <-> TCP-brug ----------------------------------------------------------------------------------
static SOCKET g_sock = INVALID_SOCKET;
static HANDLE g_pipe = INVALID_HANDLE_VALUE;

// pipe -> host: lees de pijp (overlapped), stuur naar de socket.
static DWORD WINAPI pipe_to_host(LPVOID unused) {
    (void)unused;
    char buf[CHUNK_READ];
    int first = 1;
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (;;) {
        OVERLAPPED ov; memset(&ov, 0, sizeof(ov)); ov.hEvent = ev; ResetEvent(ev);
        DWORD n = 0;
        BOOL ok = ReadFile(g_pipe, buf, sizeof(buf), &n, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (!GetOverlappedResult(g_pipe, &ov, &n, TRUE)) break;
        } else if (!ok) break;
        if (n == 0) break;
        for (DWORD off = 0; off < n; ) {
            int s = send(g_sock, buf + off, (int)(n - off), 0);
            if (s <= 0) { CloseHandle(ev); return 0; }
            off += (DWORD)s;
        }
        if (first) { logln("first pipe-to-host chunk forwarded (%lu bytes)", n); first = 0; }
    }
    CloseHandle(ev);
    return 0;
}

// host -> pipe: lees de socket, spiegel byte 0, schrijf in stukken van <=0x100 naar de pijp (overlapped).
static DWORD WINAPI host_to_pipe(LPVOID unused) {
    (void)unused;
    char buf[CHUNK_READ];
    int first = 1;
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (;;) {
        int n = recv(g_sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        if (g_wine && g_host) g_wine[0] = g_host[0];   // verbind-vlag synchroniseren vóór het antwoord
        for (int off = 0; off < n; ) {
            DWORD want = (DWORD)(n - off); if (want > CHUNK_WRITE) want = CHUNK_WRITE;
            OVERLAPPED ov; memset(&ov, 0, sizeof(ov)); ov.hEvent = ev; ResetEvent(ev);
            DWORD wrote = 0;
            BOOL ok = WriteFile(g_pipe, buf + off, want, &wrote, &ov);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                if (!GetOverlappedResult(g_pipe, &ov, &wrote, TRUE)) { logln("host-to-pipe write failed: %lu", GetLastError()); CloseHandle(ev); return 0; }
            } else if (!ok) { logln("host-to-pipe write failed: %lu", GetLastError()); CloseHandle(ev); return 0; }
            off += (int)wrote;
        }
        if (first) { logln("first host-to-pipe chunk forwarded (%d bytes)", n); first = 0; }
    }
    CloseHandle(ev);
    return 0;
}

static int pipe_loop(const char *pipename, const char *host, const char *portstr) {
    char full[256];
    snprintf(full, sizeof(full), "%s%s", PIPE_PREFIX, pipename);
    unsigned short port = (unsigned short)atoi(portstr);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { logln("WSAStartup failed"); return 1; }

    for (;;) {
        g_pipe = CreateNamedPipeA(full, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                  PIPE_TYPE_BYTE | PIPE_WAIT, 1, 0x10000, 0x10000, 0, NULL);
        if (g_pipe == INVALID_HANDLE_VALUE) { logln("CreateNamedPipe(%s) failed: %lu", full, GetLastError()); Sleep(500); continue; }
        logln("pipe %s created, waiting for game...", pipename);

        // overlapped ConnectNamedPipe
        HANDLE cev = CreateEventA(NULL, TRUE, FALSE, NULL);
        OVERLAPPED cov; memset(&cov, 0, sizeof(cov)); cov.hEvent = cev;
        BOOL conn = ConnectNamedPipe(g_pipe, &cov);
        DWORD cerr = GetLastError();
        if (!conn && cerr == ERROR_IO_PENDING) { WaitForSingleObject(cev, INFINITE); }
        else if (!conn && cerr != ERROR_PIPE_CONNECTED) { logln("ConnectNamedPipe failed: %lu", cerr); CloseHandle(cev); CloseHandle(g_pipe); continue; }
        CloseHandle(cev);
        logln("game connected to pipe %s", pipename);

        // pas NU de TCP-verbinding opzetten (pipehelper is de client)
        g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET; sa.sin_port = htons(port);
        inet_pton(AF_INET, host, &sa.sin_addr);
        if (connect(g_sock, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
            logln("cannot reach host %s:%u", host, port);
            closesocket(g_sock); g_sock = INVALID_SOCKET;
            DisconnectNamedPipe(g_pipe); CloseHandle(g_pipe); g_pipe = INVALID_HANDLE_VALUE;
            Sleep(500); continue;
        }
        int one = 1; setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
        logln("TCP attached to host %s:%u, bridging", host, port);

        HANDLE th[2];
        th[0] = CreateThread(NULL, 0, pipe_to_host, NULL, 0, NULL);
        th[1] = CreateThread(NULL, 0, host_to_pipe, NULL, 0, NULL);
        WaitForMultipleObjects(2, th, FALSE, INFINITE);   // één richting eindigt

        // opruimen en de cyclus herstarten (nieuwe pipe-instantie, nieuwe TCP)
        closesocket(g_sock); g_sock = INVALID_SOCKET;
        CancelIoEx(g_pipe, NULL);
        DisconnectNamedPipe(g_pipe);
        logln("bridge cycle ended, recycling pipe");
        WaitForMultipleObjects(2, th, TRUE, 2000);
        CloseHandle(th[0]); CloseHandle(th[1]);
        CloseHandle(g_pipe); g_pipe = INVALID_HANDLE_VALUE;
    }
}

static void usage(void) {
    fprintf(stderr,
        "rg-jvsbridge (RavenGuns pipehelper-vervanger)\n"
        "  pipe:  rg-jvsbridge <pipenaam> <host> <poort> [<shmnaam> <shmgrootte> <shmbronpad>]\n"
        "  shm:   rg-jvsbridge shm <shmnaam> <shmgrootte> <shmbronpad>\n");
    fflush(stderr);
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "shm") == 0) {
        if (argc < 5) { usage(); return 1; }
        ShmArgs a = { argv[2], atol(argv[3]), argv[4] };
        shm_mirror(&a);   // keert nooit terug
        return 0;
    }
    if (argc < 4) { usage(); return 1; }
    if (argc >= 7) {   // shm-spiegelthread vóór de pipe starten
        static ShmArgs a; a.name = argv[4]; a.size = atol(argv[5]); a.path = argv[6];
        CreateThread(NULL, 0, shm_mirror, &a, 0, NULL);
    }
    return pipe_loop(argv[1], argv[2], argv[3]);
}

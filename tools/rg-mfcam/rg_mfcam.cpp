// SPDX-License-Identifier: GPL-3.0-or-later
// rg-mfcam.dll: een Media Foundation-camera voor een Windows-spel onder Wine, gevoed door de daemon van RavenGun.
//
// WAAROM
//   Mario Kart Arcade GP DX maakt een spelersfoto met de camera van de kast. Het spel zoekt die camera via Media Foundation:
//   MF.dll!MFEnumDeviceSources met MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE = VIDCAP, daarna MFCreateSourceReaderFromMediaSource
//   (MFReadWrite.dll) met een IMFSourceReaderCallback (gemeten: de importtabel van MK_AGP3_FINAL.exe). Wine (proton-11.0-2) kent
//   geen videocamera's in Media Foundation: MFEnumDeviceSources geeft bij VIDCAP `*count = 0` en S_OK (gemeten: de code van
//   mf.dll, i386), en MFCreateDeviceSource is een stub. Het spel ziet dus nooit een camera, wat er ook aan /dev/video hangt.
//
// WAT
//   Bij het laden (de OpenParrot-lader laadt deze DLL via TP_PRELOAD_DLL in het spelproces, na de kern en voordat het spel verder
//   gaat; docs/dropins.md) zet deze DLL de invoer MFEnumDeviceSources in de importtabel van het spel om naar een eigen versie. Die
//   geeft voor VIDCAP een camera ("RavenGun camera") als het camerabestand van de daemon er is (src/ravengun/vision/
//   camera_feed_format.h, standaard Z:\dev\shm\ravengun_Camera), en roept anders de oorspronkelijke functie aan (dan blijft alles
//   zoals het was). De camera is een eigen IMFMediaSource met een stroom: YUY2 in de afmetingen van het bestand, en daarnaast NV12
//   en RGB32 (hier omgezet), 30 beelden per seconde, live. Elk verzoek om een beeld (RequestSample) krijgt het nieuwste beeld uit het
//   bestand. De daemon blijft de enige eigenaar van het apparaat.
//
// LOGBOEK
//   rg-mfcam.log naast deze DLL (in de view van de game): het laden, de omzetting van de importtabel, elke vraag van het spel
//   (MFEnumDeviceSources, het formaat dat het kiest, start/stop) en het aantal geleverde beelden. Zonder dat logboek is de DLL niet
//   geladen.
//
// TWEEDE TAAK: pHResult van d3dx10
//   De lader neemt maar een preload-DLL per spel, dus deze DLL repareert ook iets van Wine dat Mario Kart laat vallen. Het spel
//   laadt zijn effecten met D3DX10CreateEffectFromFileA(..., pump = NULL, &effect, &errors, &hr) en kijkt daarna ALLEEN naar `hr`
//   (0x838AE9 in MK_AGP3_FINAL.exe: `hr < 0` geeft NULL, en 0x76C8C0 leest dan zonder controle door die NULL). Windows zet `*pHResult`
//   ook zonder pump; Wine (proton-11.0-2) laat hem ongemoeid (gemeten met een proefprogramma: de waarde bleef 0xDEADBEEF). Dan
//   beslist wat er toevallig op de stapel stond, en het spel viel na ~3 minuten attract bij "AttractGameDemo Unload" (TireTrace_Nml.cfx).
//   De omzetting hier: de vier d3dx10-functies met een pHResult roepen het origineel aan en zetten daarna `*pHResult` op de uitkomst
//   als er geen pump is (wat Windows ook doet).
//
// DERDE TAAK: JVS over COM3 naar de pipe (zonder de gesloten TeknoParrot-kern)
//   Mario Kart opent zijn JVS-bord als `COM3` (CreateFileA op 0x8ac1f2, daarna SetCommState/SetCommTimeouts/SetupComm/PurgeComm en
//   GetCommModemStatus als sense-lijn). Onder Wine is er geen COM3; de eigen brug (third_party/rg-jvsbridge) maakt \\.\pipe\TeknoParrot_JVS en
//   de daemon is het bord erachter. Vroeger zette de gesloten kern COM3 om naar die pipe; hier doet deze DLL dat: CreateFileA op de
//   ingestelde poort geeft een handvat op de pipe, en de comm-aanroepen op dat handvat slagen zonder iets te doen. GetCommModemStatus geeft
//   de sense-lijn (0x10, na de adrestoekenning 0x30) uit byte 0 van het gedeelde geheugen TeknoParrot_JvsState. Alleen aan als
//   RG_JVS_COMPORT gezet is (dropin.ini, Env). De beslislogica staat in jvs_com.h en is getoetst (tests/test_jvs_com.cpp).
//
// VIERDE TAAK: een eigen ShareK-object voor CoCreateInstance (zonder de gesloten TeknoParrot-kern)
//   Mario Kart start bij het opstarten de ShareK-client (ShareKManager.cpp, content-distributie tussen kasten): CoCreateInstance van de
//   .NET-COM-klasse {ccc44b2a-709a-33a8-a1b0-5dbe25b19c2c} (ipccl.dll, op de echte kast met RegAsm geregistreerd) op 0x47e834. Die
//   registratie bestaat in onze prefix niet: 0x80040154, een _com_error, en de catch-afhandelaar op 0x47e8f8 roept dan toch een methode
//   aan op de lege interface (toegangsfout op 0x47e95e na ~15 s). Met RG_SHAREK_FAKE=1 zet deze DLL ole32!CoCreateInstance in de
//   importtabel van het spel om; voor die ene CLSID komt een eigen in-proces object terug dat zegt "ShareK draait maar wacht op het
//   netwerk" (status 2: de tick van het spel doet dan niets). Elke andere klasse, ook de IAuth van AMAuthd, gaat ongewijzigd naar de echte
//   CoCreateInstance. Met de TeknoParrot-kern in het proces blijft dit uit (die beantwoordt het zelf). Logica en uitleg: sharek_fake.h.
//
// VIJFDE TAAK: de stalldump (waar wacht het spel?)
//   Met RG_STALL_DUMP=30,60,90 (seconden na het laden van deze DLL) zet een eigen draad op die tijdstippen elke andere draad van het proces
//   even stil, leest EIP/ESP/EBP en zoekt de stapel af naar terugkeeradressen in de exe (alleen waarden waar een x86-aanroep aan voorafgaat,
//   stall_dump.h). Alles in rg-mfcam.log, adressen als exe+RVA en als het VA dat objdump voor de exe toont. Voor een zwart beeld waar het
//   spel niets meer logt; zonder gdb, en zonder dat het spel iets merkt (de draden draaien na de dump gewoon door).
//
// ZESDE TAAK: schijfletter-herleiding voor wat patch 0004 van de kern mist
//   De eigen kern herleidt `F:/x` naar `.\TeknoParrot\F\x`, maar alleen voor CreateFile en CreateDirectory. Mario Kart hernoemt zijn
//   `F:/system/<naam>.bin.tmp` met de CRT (`rename`) naar `<naam>.bin`, zoekt met FindFirstFileA/W en controleert met GetFileAttributesW en
//   `_waccess_s`: die gaan er niet doorheen, dus elke start begint met lege instellingen (gemeten: alleen *.bin.tmp in de view). Met
//   RG_DRIVE_FIXUP=F zet deze DLL die ingangen in de importtabel van het spel om naar dezelfde regel (drive_fix.h). Alleen de genoemde letters;
//   G: blijft met rust (het spel zet ShareK dan uit, gewenst). Uit met de TeknoParrot-kern in het proces (die doet het op zijn eigen manier).
//
// ZEVENDE TAAK: de Banapass-kaartlezer (bngrw.dll) volgen of nabootsen
//   Het spel importeert bngrw.dll uit de spelmap (de echte bibliotheek, die COM4/COM2 opent) en stuurt twee lezers aan in de opstartcontroles,
//   vlak voor de IO-controle die COM3 opent. De gesloten TeknoParrot-kern vervangt die functies; onze kern niet. Met RG_BNGRW=trace zet deze DLL
//   de elf importen van bngrw.dll om naar logger-omhulsels rond de echte functies (argumenten, uitkomst en elke callback van de bibliotheek); met
//   RG_BNGRW=fake geeft hij zelf antwoord: twee lezers zonder kaart (bngrw_fake.h). Uit met de TeknoParrot-kern.
//
// ACHTSTE TAAK: de berichtenlus en de vensters van het spel volgen, en de F:/-tekenreeksen zoals TeknoParrot aanpassen
//   RG_MSGTRACE=1 zet PeekMessageW, DispatchMessageW, ShowWindow, SetWindowPos en CreateWindowExW van het spel om naar logger-omhulsels: de eerste vijf
//   PeekMessageW-aanroepen (binnen en terug, met de duur), de vensters van de draad bij de eerste aanroep (zichtbaar, voorgrond, afmetingen), de eerste 40
//   berichten, trage aanroepen (>= 200 ms) en elke 5 s een samenvatting van de lus. Een PeekMessageW die nooit terugkeert staat als "binnen" zonder "terug".
//   De TP-kern past de tekenreeksen van het spel zelf aan (`F:/playdata/` wordt `./Fplaydata/`, zelfde lengte; het spel logt dat zelf); met RG_DRIVE_FIXUP doen
//   we dat nu ook (drive_fix.h, patch_strings), voor de genoemde letters, in de niet-uitvoerbare secties van de exe. RG_DRIVE_STRINGS=0 laat dat weg.
//
// NEGENDE TAAK: venstermodus zoals TeknoParrot (Windowed=1)
//   Het spel is DXUT (D3D10) en zet zelf volledig scherm als standaard: `mov [ebx+0x30],1` op 0x4057AD (constructor 0x405740 van het
//   instellingenobject), wat "-fullscreen" voor DXUT (0x770656) en DXUTCreateDevice(bWindowed = 0) (0x846808) geeft. Onder onze kern doet DXVK dan een
//   exclusieve modewissel ("Setting display mode: 1368x768@60", twee keer, alle proefruns van 2026-10-18 12:03-12:07) en keert de eerste PeekMessageW
//   (0x83D7D4, DXUTMainLoop) niet meer terug: zwart beeld. Onder TeknoParrot (profiel Windowed=1) staat die regel er niet en presenteert de swapchain
//   1360x768 in een venster (run 10:46). Met RG_WINDOWED=1 zet deze DLL die imm32 op 0 (patroon in window_mode.h, precies een treffer vereist), zodat het
//   spel zelf "-windowed" kiest. Uit met de TeknoParrot-kern. Logboek: "venstermodus aan: ...".
//
// TIENDE TAAK: het STR-bord (stuur en krachtterugkoppeling) op COM1 nabootsen (zonder de gesloten TeknoParrot-kern)
//   Na het venster stopt het spel op E22-12 "STR PCB 通信エラー" (tv 2026-10-18 12:35). Het STR-bord hangt niet aan JVS maar aan \\.\COM1, 19200 8N1
//   (overlapped, ReadFileEx/WriteFileEx); het meldt zijn status in drie bytes ("C01" verbonden, "E00" geen fout) en het spel geeft E22-12 als na 4800
//   ticks geen "C01" kwam (0x602E39). Met RG_STR_COMPORT=COM1 geeft CreateFileA voor die poort het clienteinde van een eigen pipe; een draad in deze DLL
//   is het bord (str_board.h): status "C01", "E00", dan "C01", en per opdracht van 10 bytes een "C01". De comm-aanroepen slagen op dat handvat (zoals bij
//   JVS). Uit met de TeknoParrot-kern. Logboek: "STR-bord: ...".
//
// OMGEVING
//   RG_STR_COMPORT=COM1  het STR-bord nabootsen (TIENDE TAAK)
//   RG_WINDOWED=1        het spel venstergewijs laten starten (NEGENDE TAAK)
//   RG_MSGTRACE=1       de berichtenlus en vensters volgen
//   RG_PEEK_DELAY_MS=N   (proef) de eerste PeekMessageW van het spel N ms uitstellen, bv. 25000 (TP doet er ~25 s over voor zijn eerste lus)
//   RG_PEEK_SKIP_FIRST=1 (proef) de eerste PeekMessageW(PM_NOREMOVE) van het spel (0x83D7D4, maakt alleen de berichtenwachtrij) niet uitvoeren
//   RG_DRIVE_STRINGS=0   bij RG_DRIVE_FIXUP de tekenreeksen niet aanpassen (alleen de API-omleiding)
//   RG_BNGRW=trace|fake  de kaartlezer volgen of nabootsen; leeg = uit
//   RG_DRIVE_FIXUP=F  de schijfletters (zonder C en Z) waarvoor de aanvullende herleiding aan staat; leeg = uit
//   RG_STALL_DUMP=30,60  de stalldump aan (seconden); leeg = uit
//   RG_SHAREK_FAKE=1  het eigen ShareK-object aan (dropin.ini, Env); leeg of 0 = uit
//   RG_MFCAM_SOURCE  Windows-pad van het camerabestand (standaard Z:\dev\shm\ravengun_Camera)
//   RG_MFCAM_FPS     beelden per seconde naar het spel (standaard 30, 1..60)
//   RG_MFCAM_OFF=1   niets doen (alleen een regel in het logboek)
//   RG_D3DX10_HRFIX=0  de pHResult-reparatie niet aanzetten
//   RG_JVS_COMPORT    de COM-poort die het spel voor JVS opent (bv. COM3); leeg = JVS-omleiding uit
//   RG_JVS_PIPE       de pipe van de brug (standaard \\.\pipe\TeknoParrot_JVS)
//
// Clean-room: alleen de openbare Media Foundation-interfaces en wat we zelf aan het spel en aan Wine gemeten hebben.
#define WIN32_LEAN_AND_MEAN
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <initguid.h>
#include <cguid.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "../../src/ravengun/vision/camera_feed_format.h"
#include "jvs_com.h"
#include "sharek_fake.h"
#include "stall_dump.h"
#include "drive_fix.h"
#include "bngrw_fake.h"
#include "window_mode.h"
#include "str_board.h"

// --- GUID's die niet in elke MinGW-importbibliotheek staan (eigen namen, dezelfde waarden als in mfidl.h / mfapi.h) -------------
DEFINE_GUID(RG_DEVSOURCE_SOURCE_TYPE, 0xc60ac5fe, 0x252a, 0x478f, 0xa0, 0xef, 0xbc, 0x8f, 0xa5, 0xf7, 0xca, 0xd3);
DEFINE_GUID(RG_DEVSOURCE_VIDCAP_GUID, 0x8ac3587a, 0x4ae7, 0x42d8, 0x99, 0xe0, 0x0a, 0x60, 0x13, 0xee, 0xf9, 0x0f);
DEFINE_GUID(RG_DEVSOURCE_FRIENDLY_NAME, 0x60d0e559, 0x52f8, 0x4fa2, 0xbb, 0xce, 0xac, 0xdb, 0x34, 0xa8, 0xec, 0x01);
DEFINE_GUID(RG_DEVSOURCE_VIDCAP_SYMLINK, 0x58f0aad8, 0x22bf, 0x4f8a, 0xbb, 0x3d, 0xd2, 0xc4, 0x97, 0x8c, 0x6e, 0x2f);
DEFINE_GUID(RG_SAMPLE_TOKEN, 0x8294da66, 0xf328, 0x4805, 0xb5, 0x51, 0x00, 0xde, 0xb4, 0xc5, 0x7a, 0x61);
DEFINE_GUID(RG_MT_VIDEO, 0x73646976, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(RG_FMT_YUY2, 0x32595559, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(RG_FMT_NV12, 0x3231564e, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(RG_FMT_RGB32, 0x00000016, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

namespace {

// --- logboek -------------------------------------------------------------------------------------------------------------------------
CRITICAL_SECTION g_log_cs;
wchar_t g_log_path[MAX_PATH];
HMODULE g_self = nullptr;

void rg_log(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = std::snprintf(line, sizeof line, "[%10lu] ", static_cast<unsigned long>(GetTickCount()));
    if (n < 0) n = 0;
    int m = std::vsnprintf(line + n, sizeof line - static_cast<size_t>(n) - 2, fmt, ap);
    va_end(ap);
    size_t len = static_cast<size_t>(n) + (m < 0 ? 0 : std::min<size_t>(static_cast<size_t>(m), sizeof line - static_cast<size_t>(n) - 3));
    line[len++] = '\r';
    line[len++] = '\n';
    EnterCriticalSection(&g_log_cs);
    HANDLE h = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, line, static_cast<DWORD>(len), &w, nullptr);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_log_cs);
}

const char* fmt_name(const GUID& g) {
    if (g == RG_FMT_YUY2) return "YUY2";
    if (g == RG_FMT_NV12) return "NV12";
    if (g == RG_FMT_RGB32) return "RGB32";
    return "?";
}

// --- instellingen ---------------------------------------------------------------------------------------------------------------------
wchar_t g_source[MAX_PATH] = RG_CAMFEED_PATH_WINE;
int g_fps = 30;

// --- het camerabestand lezen ----------------------------------------------------------------------------------------------------------
// Leest kop en beeld met de seqlock van camera_feed_format.h. true = `head` en `frame` zijn een heel beeld.
bool read_feed(rg_camfeed_header& head, std::vector<BYTE>& frame) {
    HANDLE h = CreateFileW(g_source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    for (int poging = 0; poging < 4 && !ok; ++poging) {
        DWORD got = 0;
        rg_camfeed_header hd;
        SetFilePointer(h, 0, nullptr, FILE_BEGIN);
        if (!ReadFile(h, &hd, sizeof hd, &got, nullptr) || got != sizeof hd) break;
        if (hd.seq & 1u) { Sleep(1); continue; }
        const uint32_t n = rg_camfeed_frame_bytes(&hd);
        if (!n || hd.frames == 0) break;
        frame.resize(n);
        if (!ReadFile(h, frame.data(), n, &got, nullptr) || got != n) break;
        uint32_t seq2 = 0;
        SetFilePointer(h, 8, nullptr, FILE_BEGIN);
        if (!ReadFile(h, &seq2, 4, &got, nullptr) || got != 4) break;
        if (seq2 != hd.seq) continue;
        head = hd;
        ok = true;
    }
    CloseHandle(h);
    return ok;
}

// Alleen de kop (voor MFEnumDeviceSources): is er een camera, en welke afmetingen.
bool feed_present(uint32_t& w, uint32_t& h) {
    rg_camfeed_header hd;
    std::vector<BYTE> f;
    if (!read_feed(hd, f)) return false;
    w = hd.width;
    h = hd.height;
    return true;
}

// --- omzetten -------------------------------------------------------------------------------------------------------------------------
inline BYTE clamp8(int v) { return static_cast<BYTE>(v < 0 ? 0 : v > 255 ? 255 : v); }

void yuy2_to_nv12(const BYTE* src, uint32_t stride, uint32_t w, uint32_t h, BYTE* dst) {
    BYTE* y = dst;
    BYTE* uv = dst + static_cast<size_t>(w) * h;
    for (uint32_t r = 0; r < h; ++r) {
        const BYTE* s = src + static_cast<size_t>(r) * stride;
        for (uint32_t c = 0; c < w; c += 2) {
            y[static_cast<size_t>(r) * w + c] = s[c * 2];
            y[static_cast<size_t>(r) * w + c + 1] = s[c * 2 + 2];
            if ((r & 1u) == 0) {
                BYTE* o = uv + static_cast<size_t>(r / 2) * w + c;
                o[0] = s[c * 2 + 1];
                o[1] = s[c * 2 + 3];
            }
        }
    }
}

void yuy2_to_rgb32(const BYTE* src, uint32_t stride, uint32_t w, uint32_t h, BYTE* dst) {
    // BT.601, beperkt bereik; RGB32 is in Media Foundation van boven naar onder met een positieve stride (B, G, R, X).
    for (uint32_t r = 0; r < h; ++r) {
        const BYTE* s = src + static_cast<size_t>(r) * stride;
        BYTE* d = dst + static_cast<size_t>(r) * w * 4;
        for (uint32_t c = 0; c < w; c += 2) {
            const int y0 = s[c * 2] - 16, u = s[c * 2 + 1] - 128, y1 = s[c * 2 + 2] - 16, v = s[c * 2 + 3] - 128;
            for (int k = 0; k < 2; ++k) {
                const int yy = 298 * (k ? y1 : y0);
                BYTE* p = d + (c + static_cast<uint32_t>(k)) * 4;
                p[0] = clamp8((yy + 516 * u + 128) >> 8);
                p[1] = clamp8((yy - 100 * u - 208 * v + 128) >> 8);
                p[2] = clamp8((yy + 409 * v + 128) >> 8);
                p[3] = 0xFF;
            }
        }
    }
}

uint32_t out_bytes(const GUID& fmt, uint32_t w, uint32_t h) {
    if (fmt == RG_FMT_NV12) return w * h * 3 / 2;
    if (fmt == RG_FMT_RGB32) return w * h * 4;
    return w * h * 2;
}

HRESULT make_type(const GUID& fmt, uint32_t w, uint32_t h, IMFMediaType** out) {
    IMFMediaType* t = nullptr;
    HRESULT hr = MFCreateMediaType(&t);
    if (FAILED(hr)) return hr;
    t->SetGUID(MF_MT_MAJOR_TYPE, RG_MT_VIDEO);
    t->SetGUID(MF_MT_SUBTYPE, fmt);
    t->SetUINT64(MF_MT_FRAME_SIZE, (static_cast<UINT64>(w) << 32) | h);
    t->SetUINT64(MF_MT_FRAME_RATE, (static_cast<UINT64>(g_fps) << 32) | 1u);
    t->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, (1ull << 32) | 1u);
    t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    t->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    t->SetUINT32(MF_MT_SAMPLE_SIZE, out_bytes(fmt, w, h));
    t->SetUINT32(MF_MT_DEFAULT_STRIDE, fmt == RG_FMT_NV12 ? w : fmt == RG_FMT_RGB32 ? w * 4 : w * 2);
    *out = t;
    return S_OK;
}

LONGLONG now_100ns() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<LONGLONG>(static_cast<double>(c.QuadPart) * 1e7 / static_cast<double>(freq.QuadPart));
}

class CamSource;

// --- de stroom ------------------------------------------------------------------------------------------------------------------------
class CamStream final : public IMFMediaStream {
public:
    CamStream(CamSource* src, IMFStreamDescriptor* sd) : src_(src), sd_(sd) {
        sd_->AddRef();
        InitializeCriticalSection(&cs_);
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        MFCreateEventQueue(&queue_);
    }
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator || riid == IID_IMFMediaStream) {
            *ppv = static_cast<IMFMediaStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    // IMFMediaEventGenerator
    STDMETHODIMP GetEvent(DWORD f, IMFMediaEvent** e) override { return queue_ ? queue_->GetEvent(f, e) : MF_E_SHUTDOWN; }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* st) override { return queue_ ? queue_->BeginGetEvent(cb, st) : MF_E_SHUTDOWN; }
    STDMETHODIMP EndGetEvent(IMFAsyncResult* r, IMFMediaEvent** e) override { return queue_ ? queue_->EndGetEvent(r, e) : MF_E_SHUTDOWN; }
    STDMETHODIMP QueueEvent(MediaEventType t, REFGUID g, HRESULT hr, const PROPVARIANT* v) override {
        return queue_ ? queue_->QueueEventParamVar(t, g, hr, v) : MF_E_SHUTDOWN;
    }
    // IMFMediaStream
    STDMETHODIMP GetMediaSource(IMFMediaSource** out) override;
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** out) override {
        if (!out) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        *out = sd_;
        sd_->AddRef();
        return S_OK;
    }
    STDMETHODIMP RequestSample(IUnknown* token) override {
        EnterCriticalSection(&cs_);
        HRESULT hr = S_OK;
        if (shutdown_) hr = MF_E_SHUTDOWN;
        else if (!started_) hr = MF_E_MEDIA_SOURCE_WRONGSTATE;
        else {
            if (token) token->AddRef();
            tokens_.push_back(token);
        }
        LeaveCriticalSection(&cs_);
        if (SUCCEEDED(hr)) SetEvent(wake_);
        return hr;
    }

    // Voor de bron.
    void start(const GUID& fmt, uint32_t w, uint32_t h, LONGLONG t0) {
        EnterCriticalSection(&cs_);
        fmt_ = fmt; w_ = w; h_ = h; t0_ = t0;
        started_ = true;
        if (!worker_) worker_ = CreateThread(nullptr, 0, &CamStream::worker_main, this, 0, nullptr);
        LeaveCriticalSection(&cs_);
        PROPVARIANT v;
        PropVariantInit(&v);
        v.vt = VT_I8;
        v.hVal.QuadPart = 0;
        QueueEvent(MEStreamStarted, GUID_NULL, S_OK, &v);
    }
    void stop() {
        EnterCriticalSection(&cs_);
        started_ = false;
        for (IUnknown* t : tokens_) if (t) t->Release();
        tokens_.clear();
        LeaveCriticalSection(&cs_);
        QueueEvent(MEStreamStopped, GUID_NULL, S_OK, nullptr);
    }
    void shutdown() {
        EnterCriticalSection(&cs_);
        shutdown_ = true;
        started_ = false;
        for (IUnknown* t : tokens_) if (t) t->Release();
        tokens_.clear();
        HANDLE w = worker_;
        worker_ = nullptr;
        LeaveCriticalSection(&cs_);
        SetEvent(wake_);
        if (w) { WaitForSingleObject(w, 2000); CloseHandle(w); }
        if (queue_) { queue_->Shutdown(); queue_->Release(); queue_ = nullptr; }
        rg_log("stroom dicht: %lu beelden geleverd, %lu keer zonder nieuw beeld", static_cast<unsigned long>(delivered_),
               static_cast<unsigned long>(stale_));
    }
    void detach_source() { src_ = nullptr; }

private:
    ~CamStream() {
        if (sd_) sd_->Release();
        if (queue_) { queue_->Shutdown(); queue_->Release(); }
        if (wake_) CloseHandle(wake_);
        DeleteCriticalSection(&cs_);
    }
    static DWORD WINAPI worker_main(void* p) {
        static_cast<CamStream*>(p)->worker();
        return 0;
    }
    void worker() {
        const DWORD period = static_cast<DWORD>(1000 / (g_fps > 0 ? g_fps : 30));
        DWORD next = GetTickCount();
        std::vector<BYTE> raw;
        rg_camfeed_header head{};
        bool have = false;
        for (;;) {
            WaitForSingleObject(wake_, period);
            EnterCriticalSection(&cs_);
            const bool stop = shutdown_;
            const bool go = started_ && !tokens_.empty();
            LeaveCriticalSection(&cs_);
            if (stop) break;
            if (!go) continue;
            const DWORD t = GetTickCount();
            if (static_cast<LONG>(t - next) < 0) { Sleep(next - t); }
            next = GetTickCount() + period;
            rg_camfeed_header hd;
            if (read_feed(hd, raw)) { head = hd; have = true; }
            else ++stale_;
            deliver(have ? &head : nullptr, raw);
        }
    }
    void deliver(const rg_camfeed_header* head, const std::vector<BYTE>& raw) {
        EnterCriticalSection(&cs_);
        if (!started_ || tokens_.empty()) { LeaveCriticalSection(&cs_); return; }
        IUnknown* token = tokens_.front();
        tokens_.pop_front();
        const GUID fmt = fmt_;
        const uint32_t w = w_, h = h_;
        const LONGLONG t0 = t0_;
        LeaveCriticalSection(&cs_);
        IMFSample* s = nullptr;
        IMFMediaBuffer* b = nullptr;
        const DWORD n = out_bytes(fmt, w, h);
        HRESULT hr = MFCreateMemoryBuffer(n, &b);
        if (SUCCEEDED(hr)) {
            BYTE* p = nullptr;
            DWORD maxl = 0;
            hr = b->Lock(&p, &maxl, nullptr);
            if (SUCCEEDED(hr)) {
                if (head && head->width == w && head->height == h && !raw.empty()) {
                    if (fmt == RG_FMT_NV12) yuy2_to_nv12(raw.data(), head->stride, w, h, p);
                    else if (fmt == RG_FMT_RGB32) yuy2_to_rgb32(raw.data(), head->stride, w, h, p);
                    else for (uint32_t r = 0; r < h; ++r) std::memcpy(p + static_cast<size_t>(r) * w * 2, raw.data() + static_cast<size_t>(r) * head->stride, w * 2);
                } else {
                    // Geen beeld (of een andere vorm dan afgesproken): grijs, zodat het spel een geldig beeld krijgt.
                    if (fmt == RG_FMT_NV12) std::memset(p, 0x80, n);
                    else if (fmt == RG_FMT_RGB32) std::memset(p, 0x80, n);
                    else for (DWORD i = 0; i + 1 < n; i += 2) { p[i] = 0x80; p[i + 1] = 0x80; }
                }
                b->Unlock();
                b->SetCurrentLength(n);
            }
        }
        if (SUCCEEDED(hr)) hr = MFCreateSample(&s);
        if (SUCCEEDED(hr)) hr = s->AddBuffer(b);
        if (SUCCEEDED(hr)) {
            const LONGLONG ts = now_100ns() - t0;
            s->SetSampleTime(ts < 0 ? 0 : ts);
            s->SetSampleDuration(10000000LL / (g_fps > 0 ? g_fps : 30));
            if (token) s->SetUnknown(RG_SAMPLE_TOKEN, token);
            hr = queue_ ? queue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, s) : MF_E_SHUTDOWN;
        }
        if (SUCCEEDED(hr)) {
            ++delivered_;
            if (delivered_ == 1 || delivered_ % 900 == 0)
                rg_log("beeld %lu geleverd (%s %ux%u%s)", static_cast<unsigned long>(delivered_), fmt_name(fmt), w, h, head ? "" : ", grijs: nog geen beeld van de daemon");
        } else {
            rg_log("FOUT: beeld leveren mislukt (hr=0x%08lx)", static_cast<unsigned long>(hr));
        }
        if (s) s->Release();
        if (b) b->Release();
        if (token) token->Release();
    }

    LONG ref_ = 1;
    CamSource* src_;
    IMFStreamDescriptor* sd_;
    IMFMediaEventQueue* queue_ = nullptr;
    CRITICAL_SECTION cs_;
    HANDLE wake_ = nullptr, worker_ = nullptr;
    std::deque<IUnknown*> tokens_;
    bool started_ = false, shutdown_ = false;
    GUID fmt_ = RG_FMT_YUY2;
    uint32_t w_ = 0, h_ = 0;
    LONGLONG t0_ = 0;
    unsigned long delivered_ = 0, stale_ = 0;
};

// --- de bron --------------------------------------------------------------------------------------------------------------------------
class CamSource final : public IMFMediaSource {
public:
    CamSource(uint32_t w, uint32_t h) : w_(w), h_(h) { InitializeCriticalSection(&cs_); }
    HRESULT init() {
        HRESULT hr = MFCreateEventQueue(&queue_);
        IMFMediaType* types[3] = {};
        const GUID fmts[3] = {RG_FMT_YUY2, RG_FMT_NV12, RG_FMT_RGB32};
        for (int i = 0; i < 3 && SUCCEEDED(hr); ++i) hr = make_type(fmts[i], w_, h_, &types[i]);
        IMFStreamDescriptor* sd = nullptr;
        if (SUCCEEDED(hr)) hr = MFCreateStreamDescriptor(0, 3, types, &sd);
        IMFMediaTypeHandler* th = nullptr;
        if (SUCCEEDED(hr)) hr = sd->GetMediaTypeHandler(&th);
        if (SUCCEEDED(hr)) hr = th->SetCurrentMediaType(types[0]);
        if (SUCCEEDED(hr)) hr = MFCreatePresentationDescriptor(1, &sd, &pd_);
        if (SUCCEEDED(hr)) hr = pd_->SelectStream(0);
        if (SUCCEEDED(hr)) stream_ = new CamStream(this, sd);
        if (th) th->Release();
        if (sd) sd->Release();
        for (auto* t : types) if (t) t->Release();
        return hr;
    }
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator || riid == IID_IMFMediaSource) {
            *ppv = static_cast<IMFMediaSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    // IMFMediaEventGenerator
    STDMETHODIMP GetEvent(DWORD f, IMFMediaEvent** e) override { return queue_ ? queue_->GetEvent(f, e) : MF_E_SHUTDOWN; }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* st) override { return queue_ ? queue_->BeginGetEvent(cb, st) : MF_E_SHUTDOWN; }
    STDMETHODIMP EndGetEvent(IMFAsyncResult* r, IMFMediaEvent** e) override { return queue_ ? queue_->EndGetEvent(r, e) : MF_E_SHUTDOWN; }
    STDMETHODIMP QueueEvent(MediaEventType t, REFGUID g, HRESULT hr, const PROPVARIANT* v) override {
        return queue_ ? queue_->QueueEventParamVar(t, g, hr, v) : MF_E_SHUTDOWN;
    }
    // IMFMediaSource
    STDMETHODIMP GetCharacteristics(DWORD* c) override {
        if (!c) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        *c = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** out) override {
        if (!out) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        return pd_->Clone(out);
    }
    STDMETHODIMP Start(IMFPresentationDescriptor* pd, const GUID* tf, const PROPVARIANT* pos) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        if (!pd) return E_INVALIDARG;
        if (tf && *tf != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;
        (void)pos;   // live: altijd vanaf nu
        // Het formaat dat het spel (of de source reader) koos: de huidige vorm van de stroom in deze presentatiebeschrijving.
        GUID fmt = RG_FMT_YUY2;
        uint32_t w = w_, h = h_;
        BOOL selected = FALSE;
        IMFStreamDescriptor* sd = nullptr;
        if (SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &selected, &sd)) && sd) {
            IMFMediaTypeHandler* th = nullptr;
            IMFMediaType* mt = nullptr;
            if (SUCCEEDED(sd->GetMediaTypeHandler(&th)) && th && SUCCEEDED(th->GetCurrentMediaType(&mt)) && mt) {
                GUID sub;
                UINT64 fs = 0;
                if (SUCCEEDED(mt->GetGUID(MF_MT_SUBTYPE, &sub)) && (sub == RG_FMT_YUY2 || sub == RG_FMT_NV12 || sub == RG_FMT_RGB32)) fmt = sub;
                if (SUCCEEDED(mt->GetUINT64(MF_MT_FRAME_SIZE, &fs))) { w = static_cast<uint32_t>(fs >> 32); h = static_cast<uint32_t>(fs); }
            }
            if (mt) mt->Release();
            if (th) th->Release();
            sd->Release();
        }
        if (w != w_ || h != h_) { rg_log("let op: gevraagd %ux%u, het beeld is %ux%u; grijze beelden", w, h, w_, h_); }
        rg_log("Start: %s %ux%u, stroom %s", fmt_name(fmt), w, h, selected ? "gekozen" : "NIET gekozen");
        EnterCriticalSection(&cs_);
        const bool first = !stream_announced_;
        stream_announced_ = true;
        if (!t0_) t0_ = now_100ns();
        const LONGLONG t0 = t0_;
        LeaveCriticalSection(&cs_);
        if (selected) {
            QueueEventUnk(first ? MENewStream : MEUpdatedStream, static_cast<IMFMediaStream*>(stream_));
            stream_->start(fmt, w, h, t0);
        }
        PROPVARIANT v;
        PropVariantInit(&v);
        v.vt = VT_I8;
        v.hVal.QuadPart = 0;
        return QueueEvent(MESourceStarted, GUID_NULL, S_OK, &v);
    }
    STDMETHODIMP Stop() override {
        if (shutdown_) return MF_E_SHUTDOWN;
        rg_log("Stop");
        stream_->stop();
        return QueueEvent(MESourceStopped, GUID_NULL, S_OK, nullptr);
    }
    STDMETHODIMP Pause() override { return shutdown_ ? MF_E_SHUTDOWN : MF_E_INVALID_STATE_TRANSITION; }   // live: geen pauze
    STDMETHODIMP Shutdown() override {
        if (shutdown_) return MF_E_SHUTDOWN;
        shutdown_ = true;
        rg_log("Shutdown");
        if (stream_) { stream_->shutdown(); stream_->detach_source(); stream_->Release(); stream_ = nullptr; }
        if (queue_) { queue_->Shutdown(); queue_->Release(); queue_ = nullptr; }
        if (pd_) { pd_->Release(); pd_ = nullptr; }
        return S_OK;
    }

private:
    ~CamSource() {
        if (!shutdown_) Shutdown();
        DeleteCriticalSection(&cs_);
    }
    HRESULT QueueEventUnk(MediaEventType t, IUnknown* u) { return queue_ ? queue_->QueueEventParamUnk(t, GUID_NULL, S_OK, u) : MF_E_SHUTDOWN; }

    LONG ref_ = 1;
    uint32_t w_, h_;
    CRITICAL_SECTION cs_;
    IMFMediaEventQueue* queue_ = nullptr;
    IMFPresentationDescriptor* pd_ = nullptr;
    CamStream* stream_ = nullptr;
    bool shutdown_ = false, stream_announced_ = false;
    LONGLONG t0_ = 0;
    friend class CamStream;
};

STDMETHODIMP CamStream::GetMediaSource(IMFMediaSource** out) {
    if (!out) return E_POINTER;
    if (shutdown_ || !src_) return MF_E_SHUTDOWN;
    *out = src_;
    src_->AddRef();
    return S_OK;
}

// --- de activator (wat MFEnumDeviceSources teruggeeft) ---------------------------------------------------------------------------------
// IMFActivate is een IMFAttributes; die delen gaan naar een gewone attribuutopslag van mfplat.
#define RG_FWD(sig, call) STDMETHODIMP sig override { return attrs_->call; }
class CamActivate final : public IMFActivate {
public:
    CamActivate(uint32_t w, uint32_t h) : w_(w), h_(h) {}
    HRESULT init() {
        HRESULT hr = MFCreateAttributes(&attrs_, 4);
        if (FAILED(hr)) return hr;
        attrs_->SetGUID(RG_DEVSOURCE_SOURCE_TYPE, RG_DEVSOURCE_VIDCAP_GUID);
        attrs_->SetString(RG_DEVSOURCE_FRIENDLY_NAME, L"RavenGun camera");
        attrs_->SetString(RG_DEVSOURCE_VIDCAP_SYMLINK, L"\\\\?\\ravengun#camera#0#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global");
        return S_OK;
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IMFAttributes || riid == IID_IMFActivate) {
            *ppv = static_cast<IMFActivate*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    RG_FWD(GetItem(REFGUID k, PROPVARIANT* v), GetItem(k, v))
    RG_FWD(GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t), GetItemType(k, t))
    RG_FWD(CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r), CompareItem(k, v, r))
    RG_FWD(Compare(IMFAttributes* a, MF_ATTRIBUTES_MATCH_TYPE m, BOOL* r), Compare(a, m, r))
    RG_FWD(GetUINT32(REFGUID k, UINT32* v), GetUINT32(k, v))
    RG_FWD(GetUINT64(REFGUID k, UINT64* v), GetUINT64(k, v))
    RG_FWD(GetDouble(REFGUID k, double* v), GetDouble(k, v))
    RG_FWD(GetGUID(REFGUID k, GUID* v), GetGUID(k, v))
    RG_FWD(GetStringLength(REFGUID k, UINT32* v), GetStringLength(k, v))
    RG_FWD(GetString(REFGUID k, LPWSTR v, UINT32 n, UINT32* l), GetString(k, v, n, l))
    RG_FWD(GetAllocatedString(REFGUID k, LPWSTR* v, UINT32* l), GetAllocatedString(k, v, l))
    RG_FWD(GetBlobSize(REFGUID k, UINT32* v), GetBlobSize(k, v))
    RG_FWD(GetBlob(REFGUID k, UINT8* b, UINT32 n, UINT32* l), GetBlob(k, b, n, l))
    RG_FWD(GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* l), GetAllocatedBlob(k, b, l))
    RG_FWD(GetUnknown(REFGUID k, REFIID r, LPVOID* p), GetUnknown(k, r, p))
    RG_FWD(SetItem(REFGUID k, REFPROPVARIANT v), SetItem(k, v))
    RG_FWD(DeleteItem(REFGUID k), DeleteItem(k))
    RG_FWD(DeleteAllItems(), DeleteAllItems())
    RG_FWD(SetUINT32(REFGUID k, UINT32 v), SetUINT32(k, v))
    RG_FWD(SetUINT64(REFGUID k, UINT64 v), SetUINT64(k, v))
    RG_FWD(SetDouble(REFGUID k, double v), SetDouble(k, v))
    RG_FWD(SetGUID(REFGUID k, REFGUID v), SetGUID(k, v))
    RG_FWD(SetString(REFGUID k, LPCWSTR v), SetString(k, v))
    RG_FWD(SetBlob(REFGUID k, const UINT8* b, UINT32 n), SetBlob(k, b, n))
    RG_FWD(SetUnknown(REFGUID k, IUnknown* u), SetUnknown(k, u))
    RG_FWD(LockStore(), LockStore())
    RG_FWD(UnlockStore(), UnlockStore())
    RG_FWD(GetCount(UINT32* n), GetCount(n))
    RG_FWD(GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v), GetItemByIndex(i, k, v))
    RG_FWD(CopyAllItems(IMFAttributes* d), CopyAllItems(d))
    // IMFActivate
    STDMETHODIMP ActivateObject(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (!source_) {
            CamSource* s = new CamSource(w_, h_);
            const HRESULT hr = s->init();
            if (FAILED(hr)) {
                rg_log("FOUT: camera aanmaken mislukt (hr=0x%08lx)", static_cast<unsigned long>(hr));
                s->Release();
                return hr;
            }
            source_ = s;
            rg_log("ActivateObject: camera %ux%u aangemaakt (YUY2, NV12, RGB32 @ %d fps)", w_, h_, g_fps);
        }
        return source_->QueryInterface(riid, ppv);
    }
    STDMETHODIMP ShutdownObject() override {
        if (source_) { source_->Shutdown(); source_->Release(); source_ = nullptr; }
        return S_OK;
    }
    STDMETHODIMP DetachObject() override {
        if (source_) { source_->Release(); source_ = nullptr; }
        return S_OK;
    }

private:
    ~CamActivate() {
        if (source_) source_->Release();
        if (attrs_) attrs_->Release();
    }
    LONG ref_ = 1;
    uint32_t w_, h_;
    IMFAttributes* attrs_ = nullptr;
    CamSource* source_ = nullptr;
};
#undef RG_FWD

// --- MFEnumDeviceSources ---------------------------------------------------------------------------------------------------------------
using EnumFn = HRESULT(WINAPI*)(IMFAttributes*, IMFActivate***, UINT32*);
EnumFn g_orig_enum = nullptr;
LONG g_enum_calls = 0;

HRESULT WINAPI rg_MFEnumDeviceSources(IMFAttributes* attrs, IMFActivate*** sources, UINT32* count) {
    const LONG n = InterlockedIncrement(&g_enum_calls);
    GUID type = GUID_NULL;
    const bool vidcap = attrs && SUCCEEDED(attrs->GetGUID(RG_DEVSOURCE_SOURCE_TYPE, &type)) && type == RG_DEVSOURCE_VIDCAP_GUID;
    uint32_t w = 0, h = 0;
    if (vidcap && sources && count && feed_present(w, h)) {
        CamActivate* a = new CamActivate(w, h);
        HRESULT hr = a->init();
        IMFActivate** arr = static_cast<IMFActivate**>(CoTaskMemAlloc(sizeof(IMFActivate*)));
        if (FAILED(hr) || !arr) {
            a->Release();
            if (arr) CoTaskMemFree(arr);
            rg_log("MFEnumDeviceSources #%ld: FOUT bij het aanmaken (hr=0x%08lx); door naar het origineel", static_cast<long>(n),
                   static_cast<unsigned long>(hr));
        } else {
            arr[0] = a;
            *sources = arr;
            *count = 1;
            if (n <= 3 || n % 100 == 0) rg_log("MFEnumDeviceSources #%ld (VIDCAP): 1 camera, RavenGun camera %ux%u", static_cast<long>(n), w, h);
            return S_OK;
        }
    }
    const HRESULT hr = g_orig_enum ? g_orig_enum(attrs, sources, count) : E_NOTIMPL;
    if (n <= 3 || n % 100 == 0)
        rg_log("MFEnumDeviceSources #%ld (%s): %s; origineel: hr=0x%08lx, %u apparaten", static_cast<long>(n), vidcap ? "VIDCAP" : "ander type",
               vidcap ? "geen camerabestand van de daemon" : "niet onze zaak", static_cast<unsigned long>(hr),
               (SUCCEEDED(hr) && count) ? *count : 0u);
    return hr;
}

// --- de importtabel van het spel -------------------------------------------------------------------------------------------------------
// Zet in de importtabel van `mod` de invoer `dll!func` om naar `repl`; geeft het oude adres (of nullptr als hij er niet is).
void* patch_iat(HMODULE mod, const char* dll, const char* func, void* repl) {
    auto* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (lstrcmpiA(reinterpret_cast<const char*>(base + imp->Name), dll) != 0) continue;
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        auto* names = imp->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk) : nullptr;
        for (size_t i = 0; iat[i].u1.Function; ++i) {
            bool hit = false;
            if (names && !(names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
                hit = std::strcmp(reinterpret_cast<const char*>(ibn->Name), func) == 0;
            } else if (!names) {
                HMODULE m = GetModuleHandleA(dll);
                hit = m && reinterpret_cast<void*>(iat[i].u1.Function) == reinterpret_cast<void*>(GetProcAddress(m, func));
            }
            if (!hit) continue;
            void* old = reinterpret_cast<void*>(iat[i].u1.Function);
            DWORD prot = 0;
            if (!VirtualProtect(&iat[i].u1.Function, sizeof(void*), PAGE_READWRITE, &prot)) return nullptr;
            iat[i].u1.Function = reinterpret_cast<decltype(iat[i].u1.Function)>(repl);
            VirtualProtect(&iat[i].u1.Function, sizeof(void*), prot, &prot);
            return old;
        }
    }
    return nullptr;
}

// --- d3dx10: pHResult zoals Windows (zie TWEEDE TAAK bovenaan) ---------------------------------------------------------------------------
// Alleen de argumenten die we aanraken hebben een type; de rest gaat ongezien door. Een schildwacht in *pHResult laat in het logboek zien
// of het origineel hem zelf zette (dan is de reparatie overbodig) of ongemoeid liet.
constexpr HRESULT kHrSentinel = static_cast<HRESULT>(0x5247A11Bu);
using FxFileAFn = HRESULT(WINAPI*)(const char*, const void*, void*, const char*, UINT, UINT, void*, void*, void*, void**, void**, HRESULT*);
using FxFileWFn = HRESULT(WINAPI*)(const wchar_t*, const void*, void*, const char*, UINT, UINT, void*, void*, void*, void**, void**, HRESULT*);
using SrvFileAFn = HRESULT(WINAPI*)(void*, const char*, void*, void*, void**, HRESULT*);
using SrvMemFn = HRESULT(WINAPI*)(void*, const void*, SIZE_T, void*, void*, void**, HRESULT*);
FxFileAFn g_fx_a = nullptr;
FxFileWFn g_fx_w = nullptr;
SrvFileAFn g_srv_a = nullptr;
SrvMemFn g_srv_mem = nullptr;
LONG g_hr_fixed = 0, g_hr_set_by_orig = 0;

void hr_fix(const char* fn, HRESULT hr, void* pump, HRESULT* phr) {
    if (!phr || pump) return;
    if (*phr == kHrSentinel) {
        *phr = hr;
        const LONG n = InterlockedIncrement(&g_hr_fixed);
        if (n <= 3 || n % 500 == 0) rg_log("d3dx10: %s liet pHResult ongemoeid; nu 0x%08lx (%ld keer)", fn, static_cast<unsigned long>(hr), static_cast<long>(n));
    } else if (InterlockedIncrement(&g_hr_set_by_orig) <= 3) {
        rg_log("d3dx10: %s zette pHResult zelf (0x%08lx)", fn, static_cast<unsigned long>(*phr));
    }
}

HRESULT WINAPI rg_FxFileA(const char* f, const void* d, void* i, const char* p, UINT h, UINT x, void* dev, void* pool, void* pump, void** fx, void** err, HRESULT* phr) {
    if (phr && !pump) *phr = kHrSentinel;
    const HRESULT hr = g_fx_a(f, d, i, p, h, x, dev, pool, pump, fx, err, phr);
    hr_fix("D3DX10CreateEffectFromFileA", hr, pump, phr);
    return hr;
}
HRESULT WINAPI rg_FxFileW(const wchar_t* f, const void* d, void* i, const char* p, UINT h, UINT x, void* dev, void* pool, void* pump, void** fx, void** err, HRESULT* phr) {
    if (phr && !pump) *phr = kHrSentinel;
    const HRESULT hr = g_fx_w(f, d, i, p, h, x, dev, pool, pump, fx, err, phr);
    hr_fix("D3DX10CreateEffectFromFileW", hr, pump, phr);
    return hr;
}
HRESULT WINAPI rg_SrvFileA(void* dev, const char* f, void* info, void* pump, void** srv, HRESULT* phr) {
    if (phr && !pump) *phr = kHrSentinel;
    const HRESULT hr = g_srv_a(dev, f, info, pump, srv, phr);
    hr_fix("D3DX10CreateShaderResourceViewFromFileA", hr, pump, phr);
    return hr;
}
HRESULT WINAPI rg_SrvMem(void* dev, const void* data, SIZE_T size, void* info, void* pump, void** srv, HRESULT* phr) {
    if (phr && !pump) *phr = kHrSentinel;
    const HRESULT hr = g_srv_mem(dev, data, size, info, pump, srv, phr);
    hr_fix("D3DX10CreateShaderResourceViewFromMemory", hr, pump, phr);
    return hr;
}

template <typename Fn>
void hook_d3dx10(const char* func, Fn repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), "d3dx10_43.dll", func, reinterpret_cast<void*>(repl));
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("importtabel: d3dx10_43.dll!%s %s", func, old ? "omgezet (pHResult zoals Windows)" : "niet in het spel");
}

void setup_d3dx10_hrfix() {
    wchar_t buf[8];
    if (GetEnvironmentVariableW(L"RG_D3DX10_HRFIX", buf, 8) > 0 && buf[0] == L'0') {
        rg_log("RG_D3DX10_HRFIX=0: pHResult van d3dx10 niet gerepareerd");
        return;
    }
    hook_d3dx10("D3DX10CreateEffectFromFileA", &rg_FxFileA, g_fx_a);
    hook_d3dx10("D3DX10CreateEffectFromFileW", &rg_FxFileW, g_fx_w);
    hook_d3dx10("D3DX10CreateShaderResourceViewFromFileA", &rg_SrvFileA, g_srv_a);
    hook_d3dx10("D3DX10CreateShaderResourceViewFromMemory", &rg_SrvMem, g_srv_mem);
}

// --- JVS over COM3 naar de pipe (zie DERDE TAAK bovenaan) ---------------------------------------------------------------------------------
char g_jvs_port[32];
char g_jvs_pipe[128] = "\\\\.\\pipe\\TeknoParrot_JVS";
volatile HANDLE g_jvs_handle = INVALID_HANDLE_VALUE;
HANDLE g_jvs_map = nullptr;
const volatile uint8_t* g_jvs_state = nullptr;

using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using SetCommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using GetCommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using SetCommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);
using GetCommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);
using SetupCommFn = BOOL(WINAPI*)(HANDLE, DWORD, DWORD);
using PurgeCommFn = BOOL(WINAPI*)(HANDLE, DWORD);
using GetCommModemStatusFn = BOOL(WINAPI*)(HANDLE, LPDWORD);
CreateFileAFn g_create_file_a = nullptr;
SetCommStateFn g_set_comm_state = nullptr;
GetCommStateFn g_get_comm_state = nullptr;
SetCommTimeoutsFn g_set_comm_timeouts = nullptr;
GetCommTimeoutsFn g_get_comm_timeouts = nullptr;
SetupCommFn g_setup_comm = nullptr;
PurgeCommFn g_purge_comm = nullptr;
GetCommModemStatusFn g_get_modem_status = nullptr;

bool is_jvs(HANDLE h) { return h != INVALID_HANDLE_VALUE && h == g_jvs_handle; }

// Het nagebootste STR-bord (TIENDE TAAK); de functies staan verderop.
char g_str_port[32];
volatile HANDLE g_str_handle = INVALID_HANDLE_VALUE;
bool is_str(HANDLE h) { return h != INVALID_HANDLE_VALUE && h == g_str_handle; }
bool is_fake_com(HANDLE h) { return is_jvs(h) || is_str(h); }
HANDLE str_open(LPCSTR name, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD flags);

HANDLE WINAPI rg_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (g_str_port[0] && rg_jvscom::port_matches(name, g_str_port)) return str_open(name, access, sa, flags);
    if (!g_jvs_port[0] || !rg_jvscom::port_matches(name, g_jvs_port)) return g_create_file_a(name, access, share, sa, disp, flags, tmpl);
    // De brug maakt de pipe pas als het spel hem nodig heeft of net daarvoor: even wachten tot hij er is (maximaal ~5 s).
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 50; ++i) {
        h = g_create_file_a(g_jvs_pipe, access, share, sa, OPEN_EXISTING, flags, nullptr);
        if (h != INVALID_HANDLE_VALUE) break;
        const DWORD e = GetLastError();
        if (e == ERROR_PIPE_BUSY) WaitNamedPipeA(g_jvs_pipe, 100);
        else Sleep(100);
    }
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        rg_log("JVS: CreateFileA(%s) -> pipe %s NIET te openen (fout %lu)", name, g_jvs_pipe, static_cast<unsigned long>(e));
        SetLastError(e);
        return INVALID_HANDLE_VALUE;
    }
    g_jvs_handle = h;
    rg_log("JVS: CreateFileA(%s) -> pipe %s, handvat %p", name, g_jvs_pipe, h);
    return h;
}
BOOL WINAPI rg_SetCommState(HANDLE h, LPDCB d) { return is_fake_com(h) ? TRUE : g_set_comm_state(h, d); }
BOOL WINAPI rg_GetCommState(HANDLE h, LPDCB d) { return is_fake_com(h) ? TRUE : g_get_comm_state(h, d); }
BOOL WINAPI rg_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_fake_com(h) ? TRUE : g_set_comm_timeouts(h, t); }
BOOL WINAPI rg_GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_fake_com(h) ? TRUE : g_get_comm_timeouts(h, t); }
BOOL WINAPI rg_SetupComm(HANDLE h, DWORD i, DWORD o) { return is_fake_com(h) ? TRUE : g_setup_comm(h, i, o); }
BOOL WINAPI rg_PurgeComm(HANDLE h, DWORD f) { return is_fake_com(h) ? TRUE : g_purge_comm(h, f); }
BOOL WINAPI rg_GetCommModemStatus(HANDLE h, LPDWORD status) {
    if (is_str(h)) {
        if (status) *status = 0x30;   // CTS en DSR: het bord is er
        return TRUE;
    }
    if (!is_jvs(h)) return g_get_modem_status(h, status);
    if (!g_jvs_state) {
        g_jvs_map = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (g_jvs_map) g_jvs_state = static_cast<const volatile uint8_t*>(MapViewOfFile(g_jvs_map, FILE_MAP_READ, 0, 0, 64));
    }
    const bool addressed = g_jvs_state && g_jvs_state[0] != 0;
    if (status) *status = rg_jvscom::modem_status(addressed);
    return TRUE;
}

template <typename Fn>
void hook_kernel32(const char* func, Fn repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), "KERNEL32.dll", func, reinterpret_cast<void*>(repl));
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("importtabel: KERNEL32.dll!%s %s", func, old ? "omgezet (COM-omleiding)" : "niet in het spel");
}

// CreateFileA en de comm-aanroepen een keer omzetten, voor de JVS-omleiding en het STR-bord samen (twee keer omzetten zou de omhulsels op
// zichzelf laten wijzen).
void hook_comm_once() {
    static bool done = false;
    if (done) return;
    done = true;
    hook_kernel32("CreateFileA", &rg_CreateFileA, g_create_file_a);
    hook_kernel32("SetCommState", &rg_SetCommState, g_set_comm_state);
    hook_kernel32("GetCommState", &rg_GetCommState, g_get_comm_state);
    hook_kernel32("SetCommTimeouts", &rg_SetCommTimeouts, g_set_comm_timeouts);
    hook_kernel32("GetCommTimeouts", &rg_GetCommTimeouts, g_get_comm_timeouts);
    hook_kernel32("SetupComm", &rg_SetupComm, g_setup_comm);
    hook_kernel32("PurgeComm", &rg_PurgeComm, g_purge_comm);
    hook_kernel32("GetCommModemStatus", &rg_GetCommModemStatus, g_get_modem_status);
    if (!g_create_file_a) rg_log("COM: CreateFileA staat niet in de importtabel van het spel; de omleiding kan niets vangen");
}

void setup_jvs_com() {
    char port[32];
    if (GetEnvironmentVariableA("RG_JVS_COMPORT", port, sizeof port) == 0 || !port[0]) return;   // uit
    // Met de gesloten TeknoParrot-kern in het proces (de game kiest Core = teknoparrot voor online spelen) haakt die kern COM3 zelf; twee
    // omleidingen over elkaar helpen niet, dus dan blijft deze uit.
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("JVS-omleiding uit: de TeknoParrot-kern is geladen en doet COM3 zelf");
        return;
    }
    std::snprintf(g_jvs_port, sizeof g_jvs_port, "%s", port);
    char pipe[128];
    if (GetEnvironmentVariableA("RG_JVS_PIPE", pipe, sizeof pipe) > 0 && pipe[0]) std::snprintf(g_jvs_pipe, sizeof g_jvs_pipe, "%s", pipe);
    rg_log("JVS-omleiding aan: %s -> %s", g_jvs_port, g_jvs_pipe);
    hook_comm_once();
}

// --- het nagebootste STR-bord op COM1 (zie TIENDE TAAK bovenaan; logica in str_board.h) ------------------------------------------------------
// Het spel krijgt het clienteinde van een eigen pipe (overlapped, zoals het de COM-poort opent); een draad in deze DLL is het bord aan het
// servereinde. Sluit het spel het handvat (opnieuw verbinden), dan eindigt die draad en maakt de volgende CreateFileA een nieuw paar.
LONG g_str_serial = 0;

bool str_write(HANDLE pipe, HANDLE ev, const char* data) {
    OVERLAPPED ov = {};
    ov.hEvent = ev;
    ResetEvent(ev);
    DWORD done = 0;
    if (!WriteFile(pipe, data, static_cast<DWORD>(rg_strpcb::kReplyLen), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return false;
    return GetOverlappedResult(pipe, &ov, &done, TRUE) && done == rg_strpcb::kReplyLen;
}

struct StrPair {
    HANDLE server, client;
};

DWORD WINAPI str_board_thread(LPVOID arg) {
    auto* pair = static_cast<StrPair*>(arg);
    HANDLE pipe = pair->server;
    HANDLE rd_ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE wr_ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    rg_strpcb::Board board;
    uint8_t buf[256];
    OVERLAPPED ov = {};
    bool pending = false, ok = true;
    unsigned logged = 0;
    DWORD last_summary = GetTickCount();
    while (ok) {
        if (!pending) {
            ov = OVERLAPPED{};
            ov.hEvent = rd_ev;
            ResetEvent(rd_ev);
            if (!ReadFile(pipe, buf, sizeof buf, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) break;   // gesloten
            pending = true;
        }
        const DWORD w = WaitForSingleObject(rd_ev, 20);
        if (w == WAIT_OBJECT_0) {
            pending = false;
            DWORD got = 0;
            if (!GetOverlappedResult(pipe, &ov, &got, FALSE)) break;   // het spel heeft het handvat gesloten
            if (logged < 5) {
                char hex[16 * 3 + 1] = {};
                for (DWORD i = 0; i < got && i < 16; ++i) std::snprintf(hex + 3 * i, 4, "%02x ", buf[i]);
                rg_log("STR-bord: opdracht van het spel (%lu bytes): %s", static_cast<unsigned long>(got), hex);
                ++logged;
            }
            const unsigned due = board.on_bytes(got);
            for (unsigned i = 0; i < due && ok; ++i) ok = str_write(pipe, wr_ev, rg_strpcb::Board::reply());
        } else if (w == WAIT_TIMEOUT) {
            if (const char* st = board.idle_status()) {
                if (board.status_sent <= 3) rg_log("STR-bord: status %s gestuurd", st);
                ok = str_write(pipe, wr_ev, st);
            }
        } else {
            break;
        }
        if (GetTickCount() - last_summary >= 10000) {
            last_summary = GetTickCount();
            rg_log("STR-bord: %u statusmeldingen, %lu opdrachtbytes, %lu antwoorden", board.status_sent,
                   static_cast<unsigned long>(board.rx_bytes), static_cast<unsigned long>(board.replies));
        }
    }
    rg_log("STR-bord: verbinding dicht (%u statusmeldingen, %lu opdrachtbytes, %lu antwoorden)", board.status_sent,
           static_cast<unsigned long>(board.rx_bytes), static_cast<unsigned long>(board.replies));
    if (pending) {
        CancelIoEx(pipe, &ov);
        DWORD got = 0;
        GetOverlappedResult(pipe, &ov, &got, TRUE);   // de geannuleerde lezing afwachten voor buf en ov van de stapel gaan
    }
    CloseHandle(pipe);
    CloseHandle(rd_ev);
    CloseHandle(wr_ev);
    // Het spel heeft het clienteinde gesloten: dat handvatnummer kan straks iets anders zijn (bv. het JVS-handvat), dus niet meer als STR herkennen.
    InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_str_handle), INVALID_HANDLE_VALUE, pair->client);
    delete pair;
    return 0;
}

HANDLE str_open(LPCSTR name, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD flags) {
    char pipe_name[96];
    std::snprintf(pipe_name, sizeof pipe_name, "\\\\.\\pipe\\rg_str_%lu_%ld", static_cast<unsigned long>(GetCurrentProcessId()),
                  static_cast<long>(InterlockedIncrement(&g_str_serial)));
    HANDLE server = CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0x2000, 0x2000, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE) {
        rg_log("STR-bord: CreateNamedPipe mislukt (fout %lu)", static_cast<unsigned long>(GetLastError()));
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE client = g_create_file_a(pipe_name, access, 0, sa, OPEN_EXISTING, flags, nullptr);
    if (client == INVALID_HANDLE_VALUE) {
        rg_log("STR-bord: clienteinde niet te openen (fout %lu)", static_cast<unsigned long>(GetLastError()));
        CloseHandle(server);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ConnectNamedPipe(server, &ov) && GetLastError() == ERROR_IO_PENDING) WaitForSingleObject(ov.hEvent, 1000);   // de client is er al
    CloseHandle(ov.hEvent);
    g_str_handle = client;
    HANDLE t = CreateThread(nullptr, 0, &str_board_thread, new StrPair{server, client}, 0, nullptr);
    if (t) CloseHandle(t);
    rg_log("STR-bord: CreateFileA(%s, vlaggen 0x%lx) -> eigen bord (pipe %s), handvat %p", name, static_cast<unsigned long>(flags), pipe_name, client);
    return client;
}

void setup_str_board() {
    char port[32];
    if (GetEnvironmentVariableA("RG_STR_COMPORT", port, sizeof port) == 0 || !port[0]) return;   // uit
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("STR-bord uit: de TeknoParrot-kern is geladen (die beantwoordt het stuurbord zelf)");
        return;
    }
    std::snprintf(g_str_port, sizeof g_str_port, "%s", port);
    rg_log("STR-bord aan: %s (19200 8N1) wordt een eigen bord dat C01/E00 meldt", g_str_port);
    hook_comm_once();
}

// --- een eigen ShareK-object (zie VIERDE TAAK bovenaan; logica en adressen in sharek_fake.h) -------------------------------------------------
struct SharekObject {
    const void* const* vtbl;
    volatile LONG ref;
};
static_assert(sizeof(GUID) == sizeof(rg_sharek::Guid), "GUID en rg_sharek::Guid moeten dezelfde indeling hebben");
const rg_sharek::Guid& as_guid(const GUID& g) { return *reinterpret_cast<const rg_sharek::Guid*>(&g); }

void guid_text(const GUID& g, char* out, size_t n) {
    std::snprintf(out, n, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0],
                  g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

void* g_sharek_vtbl[rg_sharek::kVtableSize];
SharekObject g_sharek = {nullptr, 1};
volatile LONG g_sharek_seen[rg_sharek::kVtableSize];
volatile LONG g_qi_bekend = 0;
volatile LONG g_qi_onbekend = 0;

// Het eerste gebruik van elk slot in het logboek: zo staat er na een tv-run welke methodes het spel werkelijk aanriep.
void sk_note_van(int slot, void* beller) {
    if (slot >= 0 && slot < rg_sharek::kVtableSize && InterlockedExchange(&g_sharek_seen[slot], 1) == 0)
        rg_log("ShareK-object: slot %d aangeroepen (beller %p)", slot, beller);
}
void sk_note(int slot) { sk_note_van(slot, nullptr); }

HRESULT STDMETHODCALLTYPE sk_query_interface(SharekObject* self, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    char tekst[48];
    guid_text(riid, tekst, sizeof tekst);
    const bool bekend = rg_sharek::knows_iid(as_guid(riid));
    static const bool alles = [] {
        char any[8];
        return GetEnvironmentVariableA("RG_SHAREK_ANY_IID", any, sizeof any) != 0 && any[0] == '1';
    }();
    if (!bekend && !alles) {
        /* Onbekende IID's zijn zeldzaam; die loggen we altijd. Bekende IID's maar een paar keer,
         * anders schrijft elke aanroep een regel (het spel vraagt dit vaak). */
        if (InterlockedIncrement(&g_qi_onbekend) <= 8)
            rg_log("ShareK-object: QueryInterface(%s) -> E_NOINTERFACE (onbekend)", tekst);
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    if (InterlockedIncrement(&g_qi_bekend) <= 8)
        rg_log("ShareK-object: QueryInterface(%s) -> hetzelfde object%s", tekst, bekend ? "" : " (onbekend, RG_SHAREK_ANY_IID)");
    *ppv = self;
    InterlockedIncrement(&self->ref);
    return S_OK;
}
ULONG STDMETHODCALLTYPE sk_add_ref(SharekObject* self) { return static_cast<ULONG>(InterlockedIncrement(&self->ref)); }
// Het object is statisch en blijft bestaan; Release telt alleen af (nooit onder 1: het spel maakt na een herstart gewoon een nieuwe).
ULONG STDMETHODCALLTYPE sk_release(SharekObject* self) {
    const LONG r = InterlockedDecrement(&self->ref);
    if (r < 1) {
        InterlockedExchange(&self->ref, 1);
        return 0;
    }
    return static_cast<ULONG>(r);
}
// IDispatch (slot 3..6): het spel gebruikt alleen de vtable-methodes; late binding bieden we niet aan.
HRESULT STDMETHODCALLTYPE sk_get_type_info_count(SharekObject*, UINT* n) {
    if (n) *n = 0;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE sk_get_type_info(SharekObject*, UINT, LCID, void**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE sk_get_ids_of_names(SharekObject*, REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE sk_invoke(SharekObject*, DISPID, REFIID, LCID, WORD, void*, void*, void*, UINT*) { return E_NOTIMPL; }

template <int N>
HRESULT STDMETHODCALLTYPE sk_nop8(SharekObject*, void*) {
    sk_note_van(N, __builtin_return_address(0));
    return S_OK;
}
template <int N>
HRESULT STDMETHODCALLTYPE sk_nop20(SharekObject*, void*, void*, void*, void*) {
    sk_note_van(N, __builtin_return_address(0));
    return S_OK;
}
template <int N, rg_sharek::Kind K>
HRESULT STDMETHODCALLTYPE sk_word(SharekObject*, int16_t* out) {
    sk_note(N);
    int16_t v = 0;
    if (out && rg_sharek::out_word(K, v)) *out = v;
    return S_OK;
}

template <int N>
void sk_install(void** vt) {
    constexpr const rg_sharek::Slot* s = rg_sharek::find_slot(N);
    if constexpr (s == nullptr) {
        vt[N] = reinterpret_cast<void*>(&sk_nop8<N>);   // ongebruikt slot: nooit aangeroepen door het spel
    } else if constexpr (s->arg_bytes == 20) {
        vt[N] = reinterpret_cast<void*>(&sk_nop20<N>);
    } else if constexpr (s->kind == rg_sharek::Kind::Nop) {
        vt[N] = reinterpret_cast<void*>(&sk_nop8<N>);
    } else {
        vt[N] = reinterpret_cast<void*>(&sk_word<N, s->kind>);
    }
}
template <int N>
struct SkFill {
    static void run(void** vt) {
        SkFill<N - 1>::run(vt);
        sk_install<N>(vt);
    }
};
template <>
struct SkFill<2> {
    static void run(void**) {}
};

void sk_build() {
    g_sharek_vtbl[0] = reinterpret_cast<void*>(&sk_query_interface);
    g_sharek_vtbl[1] = reinterpret_cast<void*>(&sk_add_ref);
    g_sharek_vtbl[2] = reinterpret_cast<void*>(&sk_release);
    SkFill<rg_sharek::kVtableSize - 1>::run(g_sharek_vtbl);
    g_sharek_vtbl[3] = reinterpret_cast<void*>(&sk_get_type_info_count);
    g_sharek_vtbl[4] = reinterpret_cast<void*>(&sk_get_type_info);
    g_sharek_vtbl[5] = reinterpret_cast<void*>(&sk_get_ids_of_names);
    g_sharek_vtbl[6] = reinterpret_cast<void*>(&sk_invoke);
    g_sharek.vtbl = reinterpret_cast<const void* const*>(g_sharek_vtbl);
}

// --- AMAuthd IAuth: een leeg object als proef ------------------------------------------------
// De game vraagt CoCreateInstance({4603BB03-058D-43D9-B96F-639BE908C1ED}, IID {045A5150-...}) en
// krijgt CLASS_E_CLASSNOTREG (0x80040154): de Namco ALL.Net-authenticatie (AMAuthd) is er niet. Als
// proef geven we een leeg object terug waarvan elke methode S_OK zegt; de aanroeper zet zijn eigen
// uitvoerparameters al op nul. Geen echte authenticatie, alleen genoeg om te meten.
volatile LONG g_am_seen[48];
struct AmAuthObject { const void* const* vtbl; volatile LONG ref; };
AmAuthObject g_am_auth;
void* g_am_auth_vtbl[48];

bool am_auth_clsid(const void* g) {
    /* 4603BB03-058D-43D9-B96F-639BE908C1ED (AMAuthd IAuth), als bytes zodat elk GUID-type past. */
    static const unsigned char kClsid[16] = {0x03, 0xbb, 0x03, 0x46, 0x8d, 0x05, 0xd9, 0x43,
                                             0xb9, 0x6f, 0x63, 0x9b, 0xe9, 0x08, 0xc1, 0xed};
    return g && std::memcmp(g, kClsid, 16) == 0;
}
HRESULT STDMETHODCALLTYPE am_query(void* zelf, const void* riid, void** ppv) {
    if (!ppv) return E_POINTER;
    (void)riid;
    *ppv = zelf;
    InterlockedIncrement(&g_am_auth.ref);
    rg_log("AMAuth: QueryInterface -> hetzelfde object");
    return S_OK;
}
ULONG STDMETHODCALLTYPE am_addref(void*) { return static_cast<ULONG>(InterlockedIncrement(&g_am_auth.ref)); }
ULONG STDMETHODCALLTYPE am_release(void*) {
    const LONG r = InterlockedDecrement(&g_am_auth.ref);
    if (r < 1) { InterlockedExchange(&g_am_auth.ref, 1); return 0; }
    return static_cast<ULONG>(r);
}
template <int N>
HRESULT STDMETHODCALLTYPE am_nop(void*) {
    if (InterlockedExchange(&g_am_seen[N], 1) == 0)
        rg_log("AMAuth-object: slot %d aangeroepen (beller %p)", N, __builtin_return_address(0));
    return S_OK;
}
template <int N>
struct AmFill { static void run(void** vt) { AmFill<N - 1>::run(vt); vt[N] = reinterpret_cast<void*>(&am_nop<N>); } };
template <> struct AmFill<2> { static void run(void**) {} };

void setup_am_auth() {
    static bool gedaan = false;
    if (gedaan) return;
    gedaan = true;
    g_am_auth_vtbl[0] = reinterpret_cast<void*>(&am_query);
    g_am_auth_vtbl[1] = reinterpret_cast<void*>(&am_addref);
    g_am_auth_vtbl[2] = reinterpret_cast<void*>(&am_release);
    AmFill<47>::run(g_am_auth_vtbl);
    g_am_auth.vtbl = g_am_auth_vtbl;
    g_am_auth.ref = 1;
}

using CoCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
CoCreateInstanceFn g_co_create_instance = nullptr;
volatile LONG g_co_seen = 0;

HRESULT WINAPI rg_CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD ctx, REFIID riid, LPVOID* ppv) {
    char c[48], i[48];
    char am_proef[8];
    const bool am_aan = GetEnvironmentVariableA("RG_AMAUTH_PROBE", am_proef, sizeof am_proef) != 0 && am_proef[0] == '1';
    if (am_aan && am_auth_clsid(&clsid)) {
        guid_text(riid, i, sizeof i);
        setup_am_auth();
        HRESULT hr = am_query(&g_am_auth, &riid, ppv);
        rg_log("AMAuth: CoCreateInstance(%s) -> eigen leeg object, hr 0x%08lx", i, static_cast<unsigned long>(hr));
        return hr;
    }
    if (rg_sharek::wants_clsid(as_guid(clsid))) {
        guid_text(clsid, c, sizeof c);
        guid_text(riid, i, sizeof i);
        if (outer) {
            rg_log("ShareK: CoCreateInstance(%s) met aggregatie geweigerd", c);
            return CLASS_E_NOAGGREGATION;
        }
        HRESULT hr = sk_query_interface(&g_sharek, riid, ppv);
        rg_log("ShareK: CoCreateInstance(%s, ctx 0x%lx, %s) -> eigen object, hr 0x%08lx", c, static_cast<unsigned long>(ctx), i, static_cast<unsigned long>(hr));
        return hr;
    }
    HRESULT hr = g_co_create_instance(clsid, outer, ctx, riid, ppv);
    // Welke andere klassen vraagt het spel? (De IAuth van AMAuthd, {4603BB03-...}, hoort hier met zijn echte uitkomst te staan.)
    if (InterlockedIncrement(&g_co_seen) <= 16) {
        guid_text(clsid, c, sizeof c);
        guid_text(riid, i, sizeof i);
        rg_log("CoCreateInstance(%s, ctx 0x%lx, %s) -> hr 0x%08lx (echte aanroep)", c, static_cast<unsigned long>(ctx), i, static_cast<unsigned long>(hr));
    }
    return hr;
}

// Het meetinstrument van rg-compat (docs/rg-compat.md, RG_COMPAT_SAMPLE=<ms>): een draad die de
// andere draden bemonstert en de heetste plekken als module+RVA wegschrijft. Mario Kart heeft maar
// een preload (deze DLL), dus laden we rg-compat.dll hier zelf. Zonder RG_COMPAT_SAMPLE gebeurt er
// niets en is het bestand ook niet nodig.
// De geheugen-motor (memory/src, docs/memory-engine.md) ook hier: Mario Kart heeft maar een preload
// (deze DLL), dus laden we ravengun-patchmem32.dll zelf als RAVENGUN_PATCHMEM_FILE gezet is. De
// motor doet dan zijn eigen synchrone ronde op de hoofdmodule en leest zijn patchlijst.
void setup_geheugen_motor() {
    char v[MAX_PATH];
    if (GetEnvironmentVariableA("RAVENGUN_PATCHMEM_FILE", v, sizeof v) == 0) return;
    HMODULE m = LoadLibraryA("ravengun-patchmem32.dll");
    rg_log("geheugen-motor: ravengun-patchmem32.dll %s (lijst %s)", m ? "geladen" : "NIET geladen", v);
}

void setup_meetdraad() {
    char v[8];
    if (GetEnvironmentVariableA("RG_COMPAT_SAMPLE", v, sizeof v) == 0) return;
    HMODULE m = LoadLibraryA("rg-compat.dll");
    rg_log("meetdraad: rg-compat.dll %s (RG_COMPAT_SAMPLE=%s)", m ? "geladen" : "NIET geladen", v);
}

DWORD WINAPI laad_hulp_dlls(LPVOID) {
    setup_geheugen_motor();
    setup_meetdraad();
    return 0;
}

void setup_sharek_fake() {
    char v[8];
    if (GetEnvironmentVariableA("RG_SHAREK_FAKE", v, sizeof v) == 0 || v[0] != '1') return;   // uit
    // De gesloten TeknoParrot-kern beantwoordt dit zelf (Core = teknoparrot, online spelen): daar blijft alles zoals het was.
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("ShareK-object uit: de TeknoParrot-kern is geladen");
        return;
    }
    sk_build();
    void* old = patch_iat(GetModuleHandleW(nullptr), "ole32.dll", "CoCreateInstance", reinterpret_cast<void*>(&rg_CoCreateInstance));
    if (old) {
        g_co_create_instance = reinterpret_cast<CoCreateInstanceFn>(old);
        rg_log("importtabel: ole32.dll!CoCreateInstance omgezet (eigen ShareK-object, oud adres %p)", old);
    } else {
        rg_log("importtabel: ole32.dll!CoCreateInstance NIET gevonden in het spel; geen eigen ShareK-object");
    }
}

// --- aanvullende schijfletter-herleiding (zie ZESDE TAAK bovenaan; logica in drive_fix.h) ------------------------------------------------------
char g_drive_letters[28];
volatile LONG g_drive_logged = 0;

std::string to_utf8(const wchar_t* w) {
    char b[600];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b - 1, nullptr, nullptr);
    return n > 0 ? std::string(b) : std::string("?");
}
void drive_note(const char* what, const std::string& from, const std::string& to, long result) {
    if (InterlockedIncrement(&g_drive_logged) <= 40) rg_log("schijfletter: %s(%s) -> %s = %ld", what, from.c_str(), to.c_str(), result);
}

using RenameFn = int(__cdecl*)(const char*, const char*);
using WRenameFn = int(__cdecl*)(const wchar_t*, const wchar_t*);
using RemoveFn = int(__cdecl*)(const char*);
using WRemoveFn = int(__cdecl*)(const wchar_t*);
using WAccessSFn = int(__cdecl*)(const wchar_t*, int);
using FindFirstAFn = HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
using FindFirstWFn = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
using GetAttrWFn = DWORD(WINAPI*)(LPCWSTR);
RenameFn g_rename = nullptr;
WRenameFn g_wrename = nullptr;
RemoveFn g_remove = nullptr;
WRemoveFn g_wremove = nullptr;
WAccessSFn g_waccess_s = nullptr;
FindFirstAFn g_find_first_a = nullptr;
FindFirstWFn g_find_first_w = nullptr;
GetAttrWFn g_get_attr_w = nullptr;

// Geeft het herleide pad (in `buf`) of het oorspronkelijke.
const char* fix_a(const char* in, char* buf, size_t n) { return rg_drivefix::redirect<char>(in, g_drive_letters, buf, n) ? buf : in; }
const wchar_t* fix_w(const wchar_t* in, wchar_t* buf, size_t n) { return rg_drivefix::redirect<wchar_t>(in, g_drive_letters, buf, n) ? buf : in; }

int __cdecl rg_rename(const char* a, const char* b) {
    char x[520], y[520];
    const char *pa = fix_a(a, x, sizeof x), *pb = fix_a(b, y, sizeof y);
    const int r = g_rename(pa, pb);
    if (pa != a || pb != b) drive_note("rename", std::string(a ? a : "") + " => " + (b ? b : ""), std::string(pa ? pa : "") + " => " + (pb ? pb : ""), r);
    return r;
}
int __cdecl rg_wrename(const wchar_t* a, const wchar_t* b) {
    wchar_t x[520], y[520];
    const wchar_t *pa = fix_w(a, x, 520), *pb = fix_w(b, y, 520);
    const int r = g_wrename(pa, pb);
    if (pa != a || pb != b) drive_note("_wrename", to_utf8(a) + " => " + to_utf8(b), to_utf8(pa) + " => " + to_utf8(pb), r);
    return r;
}
int __cdecl rg_remove(const char* a) {
    char x[520];
    const char* pa = fix_a(a, x, sizeof x);
    const int r = g_remove(pa);
    if (pa != a) drive_note("remove", a, pa, r);
    return r;
}
int __cdecl rg_wremove(const wchar_t* a) {
    wchar_t x[520];
    const wchar_t* pa = fix_w(a, x, 520);
    const int r = g_wremove(pa);
    if (pa != a) drive_note("_wremove", to_utf8(a), to_utf8(pa), r);
    return r;
}
int __cdecl rg_waccess_s(const wchar_t* a, int mode) {
    wchar_t x[520];
    const wchar_t* pa = fix_w(a, x, 520);
    const int r = g_waccess_s(pa, mode);
    if (pa != a) drive_note("_waccess_s", to_utf8(a), to_utf8(pa), r);
    return r;
}
HANDLE WINAPI rg_FindFirstFileA(LPCSTR a, LPWIN32_FIND_DATAA d) {
    char x[520];
    const char* pa = fix_a(a, x, sizeof x);
    HANDLE h = g_find_first_a(pa, d);
    if (pa != a) drive_note("FindFirstFileA", a, pa, h == INVALID_HANDLE_VALUE ? -1L : 0L);
    return h;
}
HANDLE WINAPI rg_FindFirstFileW(LPCWSTR a, LPWIN32_FIND_DATAW d) {
    wchar_t x[520];
    const wchar_t* pa = fix_w(a, x, 520);
    HANDLE h = g_find_first_w(pa, d);
    if (pa != a) drive_note("FindFirstFileW", to_utf8(a), to_utf8(pa), h == INVALID_HANDLE_VALUE ? -1L : 0L);
    return h;
}
DWORD WINAPI rg_GetFileAttributesW(LPCWSTR a) {
    wchar_t x[520];
    const wchar_t* pa = fix_w(a, x, 520);
    const DWORD r = g_get_attr_w(pa);
    if (pa != a) drive_note("GetFileAttributesW", to_utf8(a), to_utf8(pa), static_cast<long>(r));
    return r;
}

template <typename Fn>
void hook_import(const char* dll, const char* func, void* repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), dll, func, repl);
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("importtabel: %s!%s %s", dll, func, old ? "omgezet" : "niet in het spel");
}

void patch_drive_strings();

void setup_drive_fix() {
    char v[28];
    if (GetEnvironmentVariableA("RG_DRIVE_FIXUP", v, sizeof v) == 0 || !v[0]) return;   // uit
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("schijfletter-herleiding uit: de TeknoParrot-kern is geladen");
        return;
    }
    std::snprintf(g_drive_letters, sizeof g_drive_letters, "%s", v);
    rg_log("schijfletter-herleiding aan voor: %s", g_drive_letters);
    char ds[8];
    if (!(GetEnvironmentVariableA("RG_DRIVE_STRINGS", ds, sizeof ds) > 0 && ds[0] == '0')) patch_drive_strings();
    hook_import("MSVCR100.dll", "rename", reinterpret_cast<void*>(&rg_rename), g_rename);
    hook_import("MSVCR100.dll", "_wrename", reinterpret_cast<void*>(&rg_wrename), g_wrename);
    hook_import("MSVCR100.dll", "remove", reinterpret_cast<void*>(&rg_remove), g_remove);
    hook_import("MSVCR100.dll", "_wremove", reinterpret_cast<void*>(&rg_wremove), g_wremove);
    hook_import("MSVCR100.dll", "_waccess_s", reinterpret_cast<void*>(&rg_waccess_s), g_waccess_s);
    hook_import("KERNEL32.dll", "FindFirstFileA", reinterpret_cast<void*>(&rg_FindFirstFileA), g_find_first_a);
    hook_import("KERNEL32.dll", "FindFirstFileW", reinterpret_cast<void*>(&rg_FindFirstFileW), g_find_first_w);
    hook_import("KERNEL32.dll", "GetFileAttributesW", reinterpret_cast<void*>(&rg_GetFileAttributesW), g_get_attr_w);
}

// --- de Banapass-kaartlezer (zie ZEVENDE TAAK bovenaan; logica in bngrw_fake.h) ----------------------------------------------------------------
bool g_bng_fake = false;
rg_bngrw::Fake g_bng;
CRITICAL_SECTION g_bng_cs;
volatile LONG g_bng_logged = 0;

#define BNG_LOG(...) do { if (InterlockedIncrement(&g_bng_logged) <= 400) rg_log(__VA_ARGS__); } while (0)

// De callbacks van de bibliotheek zijn cdecl: (lezer, resultaat, user) of, bij ReqWaitTouch, (lezer, resultaat, kaart, user).
using BngCb3 = void(__cdecl*)(int, int, void*);
using BngCb4 = void(__cdecl*)(int, int, void*, void*);
struct BngThunk {
    void* cb;
    void* user;
    int reader;
    const char* what;
};
void __cdecl bng_thunk3(int a, int b, void* t_) {
    auto* t = static_cast<BngThunk*>(t_);
    BNG_LOG("bngrw: callback %s(lezer %d): a=%d b=0x%x", t->what, t->reader, a, static_cast<unsigned>(b));
    reinterpret_cast<BngCb3>(t->cb)(a, b, t->user);
}
void __cdecl bng_thunk4(int a, int b, void* c, void* t_) {
    auto* t = static_cast<BngThunk*>(t_);
    BNG_LOG("bngrw: callback %s(lezer %d): a=%d b=0x%x kaart=%p", t->what, t->reader, a, static_cast<unsigned>(b), c);
    reinterpret_cast<BngCb4>(t->cb)(a, b, c, t->user);
}
BngThunk* bng_thunk(void* cb, void* user, int reader, const char* what) {
    return new BngThunk{cb, user, reader, what};   // klein, bewust niet vrijgegeven: de bibliotheek kan de callback later nog aanroepen
}

using BngInitFn = int(__stdcall*)();
using BngFinFn = int(__stdcall*)();
using BngAttachFn = int(__stdcall*)(int, const char*, int, int, void*, void*);
using BngIsExecFn = int(__stdcall*)(int);
using BngLedPowerFn = int(__stdcall*)(int, int, int);
using BngDevResetFn = int(__stdcall*)(int, void*, void*);
using BngReq4Fn = int(__stdcall*)(int, int, void*, void*);   // ReqLed, ReqBeep
using BngWaitTouchFn = int(__stdcall*)(int, int, int, void*, void*);
using BngCancelFn = int(__stdcall*)(int);
using BngSendUrlFn = int(__stdcall*)(int, int, int, void*, void*, const char*, void*, void*);
BngInitFn g_bng_init = nullptr;
BngFinFn g_bng_fin = nullptr;
BngAttachFn g_bng_attach = nullptr;
BngIsExecFn g_bng_is_exec = nullptr;
BngLedPowerFn g_bng_led_power = nullptr;
BngDevResetFn g_bng_dev_reset = nullptr;
BngReq4Fn g_bng_req_led = nullptr;
BngReq4Fn g_bng_req_beep = nullptr;
BngWaitTouchFn g_bng_wait_touch = nullptr;
BngCancelFn g_bng_cancel = nullptr;
BngSendUrlFn g_bng_send_url = nullptr;

// Nep: roep de callback meteen met resultaat 0 aan.
void bng_done(void* cb, void* user, int reader) {
    if (cb) reinterpret_cast<BngCb3>(cb)(reader, rg_bngrw::kResultOk, user);
}

int __stdcall rg_BngRwInit() {
    if (g_bng_fake) {
        BNG_LOG("bngrw: Init (nep)");
        return 0;
    }
    const int r = g_bng_init();
    BNG_LOG("bngrw: Init -> %d", r);
    return r;
}
int __stdcall rg_BngRwFin() {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        g_bng.fin();
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: Fin (nep)");
        return 0;
    }
    const int r = g_bng_fin();
    BNG_LOG("bngrw: Fin -> %d", r);
    return r;
}
int __stdcall rg_BngRwAttach(int reader, const char* port, int a3, int a4, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.attach(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: Attach(lezer %d, %s, %d, 0x%x) -> %d (nep)", reader, port ? port : "?", a3, a4, r);
        if (r == rg_bngrw::kAccepted) bng_done(cb, user, reader);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, "Attach");
    const int r = g_bng_attach(reader, port, a3, a4, cb ? reinterpret_cast<void*>(&bng_thunk3) : nullptr, t);
    BNG_LOG("bngrw: Attach(lezer %d, %s, %d, 0x%x) -> %d", reader, port ? port : "?", a3, a4, r);
    return r;
}
int __stdcall rg_BngRwIsCmdExec(int reader) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.is_cmd_exec(reader);
        LeaveCriticalSection(&g_bng_cs);
        return r;
    }
    const int r = g_bng_is_exec(reader);
    static LONG last[4] = {-2, -2, -2, -2};   // alleen veranderingen loggen: het spel vraagt dit elke tik
    if (reader >= 0 && reader < 4 && InterlockedExchange(&last[reader], r) != r) BNG_LOG("bngrw: IsCmdExec(lezer %d) -> %d", reader, r);
    return r;
}
int __stdcall rg_BngRwSetLedPower(int a, int b, int c) {
    if (g_bng_fake) {
        BNG_LOG("bngrw: SetLedPower(%d, %d, %d) (nep)", a, b, c);
        return 0;
    }
    const int r = g_bng_led_power(a, b, c);
    BNG_LOG("bngrw: SetLedPower(%d, %d, %d) -> %d", a, b, c, r);
    return r;
}
int __stdcall rg_BngRwDevReset(int reader, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.dev_reset(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: DevReset(lezer %d) -> %d (nep)", reader, r);
        if (r == rg_bngrw::kAccepted) bng_done(cb, user, reader);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, "DevReset");
    const int r = g_bng_dev_reset(reader, cb ? reinterpret_cast<void*>(&bng_thunk3) : nullptr, t);
    BNG_LOG("bngrw: DevReset(lezer %d) -> %d", reader, r);
    return r;
}
int bng_req4(const char* name, BngReq4Fn real, int reader, int v, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.simple_request(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: %s(lezer %d, %d) -> %d (nep)", name, reader, v, r);
        if (r == rg_bngrw::kAccepted) bng_done(cb, user, reader);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, name);
    const int r = real(reader, v, cb ? reinterpret_cast<void*>(&bng_thunk3) : nullptr, t);
    BNG_LOG("bngrw: %s(lezer %d, %d) -> %d", name, reader, v, r);
    return r;
}
int __stdcall rg_BngRwReqLed(int reader, int v, void* cb, void* user) { return bng_req4("ReqLed", g_bng_req_led, reader, v, cb, user); }
int __stdcall rg_BngRwReqBeep(int reader, int v, void* cb, void* user) { return bng_req4("ReqBeep", g_bng_req_beep, reader, v, cb, user); }
int __stdcall rg_BngRwReqWaitTouch(int reader, int timeout, int mode, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.wait_touch(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: ReqWaitTouch(lezer %d, %d, 0x%x) -> %d (nep, geen kaart)", reader, timeout, mode, r);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, "WaitTouch");
    const int r = g_bng_wait_touch(reader, timeout, mode, cb ? reinterpret_cast<void*>(&bng_thunk4) : nullptr, t);
    BNG_LOG("bngrw: ReqWaitTouch(lezer %d, %d, 0x%x) -> %d", reader, timeout, mode, r);
    return r;
}
int __stdcall rg_BngRwReqCancel(int reader) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const bool was = g_bng.cancel(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: ReqCancel(lezer %d) -> %d (nep)", reader, was ? 1 : 0);
        return rg_bngrw::kAccepted;
    }
    const int r = g_bng_cancel(reader);
    BNG_LOG("bngrw: ReqCancel(lezer %d) -> %d", reader, r);
    return r;
}
int __stdcall rg_BngRwReqSendUrlTo(int reader, int a2, int a3, void* a4, void* a5, const char* url, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.simple_request(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: ReqSendUrlTo(lezer %d, %s) -> %d (nep)", reader, url ? url : "?", r);
        if (r == rg_bngrw::kAccepted) bng_done(cb, user, reader);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, "SendUrlTo");
    const int r = g_bng_send_url(reader, a2, a3, a4, a5, url, cb ? reinterpret_cast<void*>(&bng_thunk3) : nullptr, t);
    BNG_LOG("bngrw: ReqSendUrlTo(lezer %d, %s) -> %d", reader, url ? url : "?", r);
    return r;
}

template <typename Fn>
void hook_bng(const char* name, void* repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), "bngrw.dll", name, repl);
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("importtabel: bngrw.dll!%s %s", name, old ? "omgezet (kaartlezer)" : "niet in het spel");
}

void setup_bngrw() {
    char v[16];
    if (GetEnvironmentVariableA("RG_BNGRW", v, sizeof v) == 0 || !v[0]) return;   // uit
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("kaartlezer-omhulsel uit: de TeknoParrot-kern is geladen");
        return;
    }
    g_bng_fake = std::strcmp(v, "fake") == 0;
    if (!g_bng_fake && std::strcmp(v, "trace") != 0) {
        rg_log("RG_BNGRW=%s onbekend (kies trace of fake); uit", v);
        return;
    }
    InitializeCriticalSection(&g_bng_cs);
    rg_log("kaartlezer: %s", g_bng_fake ? "eigen antwoord (twee lezers zonder kaart)" : "de echte bngrw.dll volgen");
    hook_bng("_BngRwInit@0", reinterpret_cast<void*>(&rg_BngRwInit), g_bng_init);
    hook_bng("_BngRwFin@0", reinterpret_cast<void*>(&rg_BngRwFin), g_bng_fin);
    hook_bng("_BngRwAttach@24", reinterpret_cast<void*>(&rg_BngRwAttach), g_bng_attach);
    hook_bng("_BngRwIsCmdExec@4", reinterpret_cast<void*>(&rg_BngRwIsCmdExec), g_bng_is_exec);
    hook_bng("_BngRwSetLedPower@12", reinterpret_cast<void*>(&rg_BngRwSetLedPower), g_bng_led_power);
    hook_bng("_BngRwDevReset@12", reinterpret_cast<void*>(&rg_BngRwDevReset), g_bng_dev_reset);
    hook_bng("_BngRwReqLed@16", reinterpret_cast<void*>(&rg_BngRwReqLed), g_bng_req_led);
    hook_bng("_BngRwReqBeep@16", reinterpret_cast<void*>(&rg_BngRwReqBeep), g_bng_req_beep);
    hook_bng("_BngRwReqWaitTouch@20", reinterpret_cast<void*>(&rg_BngRwReqWaitTouch), g_bng_wait_touch);
    hook_bng("_BngRwReqCancel@4", reinterpret_cast<void*>(&rg_BngRwReqCancel), g_bng_cancel);
    hook_bng("_BngRwReqSendUrlTo@32", reinterpret_cast<void*>(&rg_BngRwReqSendUrlTo), g_bng_send_url);
}

// --- berichtenlus en vensters volgen (zie ACHTSTE TAAK bovenaan) -----------------------------------------------------------------------------------
volatile LONG g_peek_n = 0, g_peek_msgs = 0, g_disp_n = 0;
volatile LONG g_peek_slow = 0;
DWORD g_peek_max_ms = 0;
DWORD g_peek_last_sum = 0;
DWORD g_peek_delay_ms = 0;       // RG_PEEK_DELAY_MS: de eerste PeekMessageW zoveel ms uitstellen (proef: een race met het venster?)
bool g_peek_skip_first = false;  // RG_PEEK_SKIP_FIRST=1: de eerste PeekMessageW (PM_NOREMOVE) niet uitvoeren (proef)

using PeekMessageWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using DispatchMessageWFn = LRESULT(WINAPI*)(const MSG*);
using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using CreateWindowExWFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
PeekMessageWFn g_peek_w = nullptr;
DispatchMessageWFn g_dispatch_w = nullptr;
ShowWindowFn g_show_window = nullptr;
SetWindowPosFn g_set_window_pos = nullptr;
CreateWindowExWFn g_create_window_ex_w = nullptr;

BOOL CALLBACK enum_thread_window(HWND h, LPARAM) {
    wchar_t cls[80] = {};
    GetClassNameW(h, cls, 79);
    RECT r = {};
    GetWindowRect(h, &r);
    rg_log("  venster %p klasse %s zichtbaar %d iconic %d rect %ld,%ld %ldx%ld", h, to_utf8(cls).c_str(), IsWindowVisible(h) ? 1 : 0, IsIconic(h) ? 1 : 0,
           static_cast<long>(r.left), static_cast<long>(r.top), static_cast<long>(r.right - r.left), static_cast<long>(r.bottom - r.top));
    return TRUE;
}

void window_state(const char* why) {
    rg_log("vensters van deze draad (%s): voorgrond %p, actief %p, focus %p", why, GetForegroundWindow(), GetActiveWindow(), GetFocus());
    EnumThreadWindows(GetCurrentThreadId(), enum_thread_window, 0);
}

BOOL WINAPI rg_PeekMessageW(LPMSG m, HWND h, UINT a, UINT b, UINT f) {
    const LONG n = InterlockedIncrement(&g_peek_n);
    if (n <= 5) {
        rg_log("PeekMessageW #%ld: binnen (hwnd %p, %u..%u, vlaggen 0x%x)", static_cast<long>(n), h, a, b, f);
        if (n == 1) window_state("eerste PeekMessageW");
    }
    if (n == 1 && g_peek_delay_ms) {
        rg_log("PeekMessageW #1: %lu ms uitgesteld (RG_PEEK_DELAY_MS)", static_cast<unsigned long>(g_peek_delay_ms));
        Sleep(g_peek_delay_ms);
    }
    if (n == 1 && g_peek_skip_first && f == 0) {   // de eerste PeekMessageW(PM_NOREMOVE) van het spel (0x83D7D4) maakt alleen de berichtenwachtrij; msg blijft leeg
        rg_log("PeekMessageW #1 overgeslagen (RG_PEEK_SKIP_FIRST): FALSE zonder de echte aanroep");
        if (m) std::memset(m, 0, sizeof *m);
        return FALSE;
    }
    const DWORD t0 = GetTickCount();
    const BOOL r = g_peek_w(m, h, a, b, f);
    const DWORD dt = GetTickCount() - t0;
    if (n <= 5) rg_log("PeekMessageW #%ld: terug met %d na %lu ms", static_cast<long>(n), r ? 1 : 0, static_cast<unsigned long>(dt));
    if (dt >= 200 && InterlockedIncrement(&g_peek_slow) <= 20) rg_log("PeekMessageW #%ld duurde %lu ms", static_cast<long>(n), static_cast<unsigned long>(dt));
    if (dt > g_peek_max_ms) g_peek_max_ms = dt;
    if (r) {
        const LONG k = InterlockedIncrement(&g_peek_msgs);
        if (k <= 40 && m) rg_log("bericht #%ld: hwnd %p msg 0x%04x wParam 0x%lx lParam 0x%lx", static_cast<long>(k), m->hwnd, m->message, static_cast<unsigned long>(m->wParam),
                                 static_cast<unsigned long>(m->lParam));
    }
    const DWORD now = GetTickCount();
    if (now - g_peek_last_sum >= 5000) {   // elke 5 s een samenvatting (alleen als de lus draait: bij een hang komt hij niet)
        g_peek_last_sum = now;
        rg_log("berichtenlus: %ld aanroepen, %ld berichten, langste aanroep %lu ms", static_cast<long>(g_peek_n), static_cast<long>(g_peek_msgs), static_cast<unsigned long>(g_peek_max_ms));
    }
    return r;
}
LRESULT WINAPI rg_DispatchMessageW(const MSG* m) {
    const LONG n = InterlockedIncrement(&g_disp_n);
    if (n <= 5 && m) rg_log("DispatchMessageW #%ld: hwnd %p msg 0x%04x", static_cast<long>(n), m->hwnd, m->message);
    return g_dispatch_w(m);
}
BOOL WINAPI rg_ShowWindow(HWND h, int cmd) {
    const BOOL r = g_show_window(h, cmd);
    rg_log("ShowWindow(%p, %d) -> %d", h, cmd, r ? 1 : 0);
    return r;
}
BOOL WINAPI rg_SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT fl) {
    const BOOL r = g_set_window_pos(h, after, x, y, cx, cy, fl);
    static volatile LONG k = 0;
    if (InterlockedIncrement(&k) <= 20) rg_log("SetWindowPos(%p, %p, %d,%d %dx%d, 0x%x) -> %d", h, after, x, y, cx, cy, fl, r ? 1 : 0);
    return r;
}
HWND WINAPI rg_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int hh, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p) {
    HWND h = g_create_window_ex_w(ex, cls, name, style, x, y, w, hh, parent, menu, inst, p);
    rg_log("CreateWindowExW(ex 0x%lx, %s, %s, stijl 0x%lx, %d,%d %dx%d) -> %p", static_cast<unsigned long>(ex), (reinterpret_cast<uintptr_t>(cls) > 0xFFFF) ? to_utf8(cls).c_str() : "(atoom)",
           name ? to_utf8(name).c_str() : "", static_cast<unsigned long>(style), x, y, w, hh, h);
    return h;
}

void setup_msg_trace() {
    char v[8];
    if (GetEnvironmentVariableA("RG_MSGTRACE", v, sizeof v) == 0 || v[0] != '1') return;   // uit
    char pv[16];
    if (GetEnvironmentVariableA("RG_PEEK_DELAY_MS", pv, sizeof pv) > 0) g_peek_delay_ms = static_cast<DWORD>(std::strtoul(pv, nullptr, 10));
    if (GetEnvironmentVariableA("RG_PEEK_SKIP_FIRST", pv, sizeof pv) > 0 && pv[0] == '1') g_peek_skip_first = true;
    rg_log("berichtenlus volgen aan (uitstel %lu ms, eerste overslaan %d)", static_cast<unsigned long>(g_peek_delay_ms), g_peek_skip_first ? 1 : 0);
    hook_import("USER32.dll", "PeekMessageW", reinterpret_cast<void*>(&rg_PeekMessageW), g_peek_w);
    hook_import("USER32.dll", "DispatchMessageW", reinterpret_cast<void*>(&rg_DispatchMessageW), g_dispatch_w);
    hook_import("USER32.dll", "ShowWindow", reinterpret_cast<void*>(&rg_ShowWindow), g_show_window);
    hook_import("USER32.dll", "SetWindowPos", reinterpret_cast<void*>(&rg_SetWindowPos), g_set_window_pos);
    hook_import("USER32.dll", "CreateWindowExW", reinterpret_cast<void*>(&rg_CreateWindowExW), g_create_window_ex_w);
}

// TeknoParrot-stijl: de F:/-tekenreeksen van het spel zelf aanpassen (drive_fix.h, patch_strings) in de gegevenssecties van de exe.
void patch_drive_strings() {
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    size_t total_n = 0, total_w = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;   // geen code
        BYTE* start = exe + sec[i].VirtualAddress;
        const DWORD size = sec[i].Misc.VirtualSize;
        DWORD old = 0;
        if (!size || !VirtualProtect(start, size, PAGE_READWRITE, &old)) continue;
        const size_t n = rg_drivefix::patch_strings(start, size, g_drive_letters, 1);
        const size_t w = rg_drivefix::patch_strings(start, size, g_drive_letters, 2);
        VirtualProtect(start, size, old, &old);
        char name[9] = {};
        std::memcpy(name, sec[i].Name, 8);
        rg_log("schijfletter: tekenreeksen in %s: %lu smal, %lu breed aangepast (<L>:/ -> ./<L>, zoals TeknoParrot)", name, static_cast<unsigned long>(n), static_cast<unsigned long>(w));
        total_n += n;
        total_w += w;
    }
    rg_log("schijfletter: %lu smalle en %lu brede tekenreeksen aangepast", static_cast<unsigned long>(total_n), static_cast<unsigned long>(total_w));
}

// --- venstermodus zoals TeknoParrot Windowed=1 (zie NEGENDE TAAK bovenaan) ----------------------------------------------------------------
void setup_windowed() {
    char v[8];
    if (GetEnvironmentVariableA("RG_WINDOWED", v, sizeof v) == 0 || v[0] != '1') return;   // uit
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("venstermodus uit: de TeknoParrot-kern is geladen (die doet Windowed=1 zelf)");
        return;
    }
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;   // alleen code
        BYTE* start = exe + sec[i].VirtualAddress;
        const DWORD size = sec[i].Misc.VirtualSize;
        size_t first = 0;
        const size_t hits = rg_winmode::find_pattern(start, size, first);
        if (hits == 0) continue;
        if (hits != 1) {
            rg_log("venstermodus: patroon %lu keer gevonden, niets aangepast (verwacht precies 1)", static_cast<unsigned long>(hits));
            return;
        }
        BYTE* imm = start + first + rg_winmode::kImmOffset;
        DWORD old = 0;
        if (!VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old)) {
            rg_log("venstermodus: VirtualProtect mislukt (fout %lu), niets aangepast", static_cast<unsigned long>(GetLastError()));
            return;
        }
        const DWORD zero = 0;
        std::memcpy(imm, &zero, 4);
        VirtualProtect(imm, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), imm, 4);
        const DWORD at = static_cast<DWORD>(nt->OptionalHeader.ImageBase + (imm - 3 - exe));
        rg_log("venstermodus aan: mov [ebx+0x30],1 -> 0 op VA 0x%lx (DXUT krijgt -windowed, geen modewissel; zoals TeknoParrot Windowed=1)",
               static_cast<unsigned long>(at));
        return;
    }
    rg_log("venstermodus: patroon niet gevonden in de exe, niets aangepast (andere versie van het spel?)");
}

// --- de stalldump (zie VIJFDE TAAK bovenaan) -------------------------------------------------------------------------------------------------
// Waar ligt `a`? In de exe: "exe+0xRVA (VA 0x...)", anders "module+0xoffset".
void where_text(DWORD a, char* out, size_t n) {
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const DWORD base = reinterpret_cast<DWORD>(exe), size = nt->OptionalHeader.SizeOfImage;
    if (a >= base && a < base + size) {
        std::snprintf(out, n, "exe+0x%lx (VA 0x%lx)", static_cast<unsigned long>(a - base), static_cast<unsigned long>(nt->OptionalHeader.ImageBase + (a - base)));
        return;
    }
    HMODULE m = nullptr;
    char name[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(a)), &m) &&
        GetModuleFileNameA(m, name, MAX_PATH)) {
        const char* b = std::strrchr(name, '\\');
        std::snprintf(out, n, "%s+0x%lx", b ? b + 1 : name, static_cast<unsigned long>(a - reinterpret_cast<DWORD>(m)));
    } else {
        std::snprintf(out, n, "0x%lx", static_cast<unsigned long>(a));
    }
}

bool read_self(DWORD addr, void* buf, SIZE_T n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(addr)), buf, n, &got) && got == n;
}

void stall_dump_once(unsigned secs) {
    rg_log("stalldump na %u s:", secs);
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const DWORD base = reinterpret_cast<DWORD>(exe), size = nt->OptionalHeader.SizeOfImage;
    const DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        rg_log("  geen threadlijst (fout %lu)", static_cast<unsigned long>(GetLastError()));
        return;
    }
    THREADENTRY32 te = {};
    te.dwSize = sizeof te;
    int threads = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        ++threads;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) {
            rg_log("  thread %lu: niet te openen (fout %lu)", static_cast<unsigned long>(te.th32ThreadID), static_cast<unsigned long>(GetLastError()));
            continue;
        }
        if (SuspendThread(h) == static_cast<DWORD>(-1)) {
            rg_log("  thread %lu: SuspendThread mislukt (fout %lu)", static_cast<unsigned long>(te.th32ThreadID), static_cast<unsigned long>(GetLastError()));
            CloseHandle(h);
            continue;
        }
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(h, &c)) {
            char w[160];
            where_text(c.Eip, w, sizeof w);
            rg_log("  thread %lu: EIP %s, ESP 0x%lx, EBP 0x%lx", static_cast<unsigned long>(te.th32ThreadID), w, static_cast<unsigned long>(c.Esp),
                   static_cast<unsigned long>(c.Ebp));
            // De stapel (ESP..+4 KiB): waarden in de exe waar een aanroep aan voorafgaat. Hoogstens 14 per draad.
            BYTE stack[4096];
            // In stukken van 32 bytes: dicht bij de top van de stapel zou een enkele grote lezing mislukken.
            SIZE_T got = 0;
            while (got + 32 <= sizeof stack && read_self(c.Esp + static_cast<DWORD>(got), stack + got, 32)) got += 32;
            if (got >= 4) {
                int shown = 0, shown_other = 0;
                for (SIZE_T o = 0; o + 4 <= got && (shown < 14 || shown_other < 16); o += 4) {
                    DWORD v;
                    std::memcpy(&v, stack + o, 4);
                    const bool in_exe = v >= base + 8 && v < base + size;
                    if (in_exe ? shown >= 14 : shown_other >= 16) continue;
                    if (!in_exe) {
                        // andere modules (user32, win32u, winex11-brug, dinput8, d3d11, ...): alleen echte code in een geladen image
                        if (v < 0x10000 || v >= 0x7ff00000) continue;
                        MEMORY_BASIC_INFORMATION mbi = {};
                        if (!VirtualQuery(reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(v)), &mbi, sizeof mbi) || mbi.Type != MEM_IMAGE) continue;
                        if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) continue;
                    }
                    BYTE before[8];
                    if (!read_self(v - 8, before, 8)) continue;
                    if (!rg_stall::preceded_by_call(before + 8)) continue;   // leest p[-6]..p[-1]: de 8 bytes vlak voor v
                    where_text(v, w, sizeof w);
                    rg_log("      [ESP+0x%lx] %s%s", static_cast<unsigned long>(o), in_exe ? "" : "(module) ", w);
                    if (in_exe) ++shown; else ++shown_other;
                }
            }
        } else {
            rg_log("  thread %lu: GetThreadContext mislukt (fout %lu)", static_cast<unsigned long>(te.th32ThreadID), static_cast<unsigned long>(GetLastError()));
        }
        ResumeThread(h);
        CloseHandle(h);
    }
    CloseHandle(snap);
    rg_log("  %d draden bekeken", threads);
}

DWORD WINAPI stall_thread(LPVOID) {
    char spec[128];
    if (GetEnvironmentVariableA("RG_STALL_DUMP", spec, sizeof spec) == 0) return 0;
    const DWORD t0 = GetTickCount();
    for (unsigned secs : rg_stall::parse_times(spec)) {
        for (;;) {
            const DWORD el = GetTickCount() - t0;
            if (el >= secs * 1000u) break;
            Sleep(static_cast<DWORD>(secs * 1000u - el > 500 ? 500 : secs * 1000u - el));
        }
        stall_dump_once(secs);
    }
    return 0;
}

void setup_stall_dump() {
    char spec[128];
    if (GetEnvironmentVariableA("RG_STALL_DUMP", spec, sizeof spec) == 0 || !spec[0]) return;
    HANDLE t = CreateThread(nullptr, 0, &stall_thread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    rg_log("stalldump aan: %s s na het laden", spec);
}

void setup() {
    InitializeCriticalSection(&g_log_cs);
    // Logboek naast deze DLL.
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(g_self, self, MAX_PATH);
    lstrcpynW(g_log_path, self, MAX_PATH);
    wchar_t* slash = wcsrchr(g_log_path, L'\\');
    if (slash) lstrcpynW(slash + 1, L"rg-mfcam.log", static_cast<int>(MAX_PATH - (slash + 1 - g_log_path)));
    else lstrcpynW(g_log_path, L"rg-mfcam.log", MAX_PATH);
    wchar_t buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"RG_MFCAM_SOURCE", buf, MAX_PATH) > 0) lstrcpynW(g_source, buf, MAX_PATH);
    if (GetEnvironmentVariableW(L"RG_MFCAM_FPS", buf, MAX_PATH) > 0) {
        const int f = _wtoi(buf);
        if (f >= 1 && f <= 60) g_fps = f;
    }
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char src8[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, g_source, -1, src8, MAX_PATH, nullptr, nullptr);
    rg_log("rg-mfcam geladen in %s (camerabestand %s, %d fps)", exe, src8, g_fps);
    setup_d3dx10_hrfix();   // los van de camera: ook met RG_MFCAM_OFF=1
    setup_jvs_com();        // idem: alleen met RG_JVS_COMPORT
    setup_str_board();      // idem: alleen met RG_STR_COMPORT
    // Deze twee laden allebei een DLL en mogen daarom NIET in DllMain (loader-lock): in een draad,
    // net als de stalldump hierboven.
    {
        HANDLE t = CreateThread(nullptr, 0, &laad_hulp_dlls, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    setup_sharek_fake();    // idem: alleen met RG_SHAREK_FAKE=1
    setup_stall_dump();     // idem: alleen met RG_STALL_DUMP
    setup_drive_fix();      // idem: alleen met RG_DRIVE_FIXUP
    setup_bngrw();          // idem: alleen met RG_BNGRW
    setup_msg_trace();      // idem: alleen met RG_MSGTRACE=1
    setup_windowed();       // idem: alleen met RG_WINDOWED=1
    if (GetEnvironmentVariableW(L"RG_MFCAM_OFF", buf, MAX_PATH) > 0 && buf[0] == L'1') {
        rg_log("RG_MFCAM_OFF=1: niets omgezet");
        return;
    }
    void* old = patch_iat(GetModuleHandleW(nullptr), "MF.dll", "MFEnumDeviceSources", reinterpret_cast<void*>(&rg_MFEnumDeviceSources));
    if (old) {
        g_orig_enum = reinterpret_cast<EnumFn>(old);
        rg_log("importtabel: MF.dll!MFEnumDeviceSources omgezet (oud adres %p)", old);
    } else {
        rg_log("importtabel: MF.dll!MFEnumDeviceSources NIET gevonden in het spel; geen camera");
    }
    uint32_t w = 0, h = 0;
    if (feed_present(w, h)) rg_log("camerabestand nu: %ux%u met beeld", w, h);
    else rg_log("camerabestand nu: er is (nog) geen beeld (de daemon zet het aan voor een game met [Camera] Use = game)");
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        setup();
    }
    return TRUE;
}

// Voor een proef zonder spel (een klein programma onder Wine dat deze DLL laadt): dezelfde functie als het spel na de omzetting krijgt.
extern "C" __declspec(dllexport) HRESULT WINAPI RgMfcamEnumDeviceSources(IMFAttributes* a, IMFActivate*** s, UINT32* c) {
    return rg_MFEnumDeviceSources(a, s, c);
}

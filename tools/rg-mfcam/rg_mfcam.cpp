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
// OMGEVING
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
#include <vector>

#include "../../src/ravengun/vision/camera_feed_format.h"
#include "jvs_com.h"

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

HANDLE WINAPI rg_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (!rg_jvscom::port_matches(name, g_jvs_port)) return g_create_file_a(name, access, share, sa, disp, flags, tmpl);
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
BOOL WINAPI rg_SetCommState(HANDLE h, LPDCB d) { return is_jvs(h) ? TRUE : g_set_comm_state(h, d); }
BOOL WINAPI rg_GetCommState(HANDLE h, LPDCB d) { return is_jvs(h) ? TRUE : g_get_comm_state(h, d); }
BOOL WINAPI rg_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_jvs(h) ? TRUE : g_set_comm_timeouts(h, t); }
BOOL WINAPI rg_GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_jvs(h) ? TRUE : g_get_comm_timeouts(h, t); }
BOOL WINAPI rg_SetupComm(HANDLE h, DWORD i, DWORD o) { return is_jvs(h) ? TRUE : g_setup_comm(h, i, o); }
BOOL WINAPI rg_PurgeComm(HANDLE h, DWORD f) { return is_jvs(h) ? TRUE : g_purge_comm(h, f); }
BOOL WINAPI rg_GetCommModemStatus(HANDLE h, LPDWORD status) {
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
    rg_log("importtabel: KERNEL32.dll!%s %s", func, old ? "omgezet (JVS-omleiding)" : "niet in het spel");
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
    hook_kernel32("CreateFileA", &rg_CreateFileA, g_create_file_a);
    hook_kernel32("SetCommState", &rg_SetCommState, g_set_comm_state);
    hook_kernel32("GetCommState", &rg_GetCommState, g_get_comm_state);
    hook_kernel32("SetCommTimeouts", &rg_SetCommTimeouts, g_set_comm_timeouts);
    hook_kernel32("GetCommTimeouts", &rg_GetCommTimeouts, g_get_comm_timeouts);
    hook_kernel32("SetupComm", &rg_SetupComm, g_setup_comm);
    hook_kernel32("PurgeComm", &rg_PurgeComm, g_purge_comm);
    hook_kernel32("GetCommModemStatus", &rg_GetCommModemStatus, g_get_modem_status);
    if (!g_create_file_a) rg_log("JVS: CreateFileA staat niet in de importtabel van het spel; de omleiding kan niets vangen");
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

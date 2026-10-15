/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_v4l2_fake.c -- virtuele webcams voor arcadegames die met camera's in de geweren richten (Raw Thrills: Halo: Fireteam Raven).
 * Wordt met LD_PRELOAD in het spelproces geladen (native route, packaging/native-games.json: "preload" + "fake_video").
 *
 * Het spel vindt zijn camera's met `ls -1 /dev/video*` en opent ze met open(); de lader zet daarom in de sandbox voor elke
 * virtuele camera een knooppunt /dev/videoN (een binding van /dev/null). Deze bibliotheek vangt open() van zo'n pad af en geeft
 * een eventfd terug (altijd leesbaar, dus select()/poll() van de kernel werken vanzelf), en beantwoordt daarop:
 *   ioctl  VIDIOC_QUERYCAP (capture + read/write), ENUM_FMT/G_FMT/S_FMT/TRY_FMT (YUYV, de gevraagde maat), G_PARM/S_PARM (60 fps),
 *          S_PRIORITY, STREAMON/STREAMOFF; besturingselementen bestaan niet (EINVAL, het spel meldt dat als niet-fataal);
 *          al het andere EINVAL (met RG_V4L2_LOG=1 op stderr, zodat te zien is wat het spel vraagt);
 *   read   een zwart YUYV-beeld van de ingestelde maat, in het tempo van 60 beelden per seconde;
 *   close  de nep-fd weg.
 * Alleen paden /dev/video0 .. /dev/video(RG_V4L2_FAKE-1) worden afgevangen; RG_V4L2_FAKE=0 of ontbrekend: niets.
 *
 * Wat de beelden moeten tonen om te richten, staat hier (nog) niet in: deze stap maakt alleen dat het spel zijn geweren vindt.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <elf.h>
#include <link.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MAX_CAMS 4

#define MAX_BUFS 16

typedef struct {
    int fd;      /* de eventfd, -1 = vrij */
    int index;   /* N van /dev/videoN */
    uint32_t w, h, pixfmt, bpl, size;
    uint64_t next_ns;
    /* streaming met USERPTR (het spel geeft zijn eigen buffers; Halo importeert geen mmap) */
    uint32_t nbufs, memory, seq;
    uint64_t qcounter;
    struct { unsigned long ptr; uint32_t len; int queued; uint64_t order; void *map; } buf[MAX_BUFS];
} cam;

static uint32_t stride(const cam *c) { return (c->size + 4095u) & ~4095u; }

static cam cams[MAX_CAMS] = {{.fd = -1}, {.fd = -1}, {.fd = -1}, {.fd = -1}};
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int n_fake = -1, log_on;

static int (*real_open)(const char *, int, ...);
static int (*real_open64)(const char *, int, ...);
static int (*real_ioctl)(int, unsigned long, ...);
static ssize_t (*real_read)(int, void *, size_t);
static int (*real_close)(int);

static void init(void) {
    if (n_fake >= 0) return;
    const char *v = getenv("RG_V4L2_FAKE");
    n_fake = v ? atoi(v) : 0;
    if (n_fake < 0) n_fake = 0;
    if (n_fake > MAX_CAMS) n_fake = MAX_CAMS;
    const char *l = getenv("RG_V4L2_LOG");
    log_on = l && *l == '1';
    *(void **)&real_open = dlsym(RTLD_NEXT, "open");
    *(void **)&real_open64 = dlsym(RTLD_NEXT, "open64");
    *(void **)&real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    *(void **)&real_read = dlsym(RTLD_NEXT, "read");
    *(void **)&real_close = dlsym(RTLD_NEXT, "close");
}

static FILE *(*real_popen)(const char *, const char *);
static void logf_(const char *fmt, ...);
/* Meting (RG_V4L2_LOG=1): welke opdrachten het spel met popen uitvoert (Halo zoekt zijn camera's met `ls -1 /dev/video*`). */
static FILE *rg_popen(const char *cmd, const char *mode) {
    if (!real_popen) *(void **)&real_popen = dlsym(RTLD_NEXT, "popen");
    FILE *f = real_popen(cmd, mode);
    logf_("popen(\"%s\", \"%s\") -> %p", cmd ? cmd : "(null)", mode ? mode : "(null)", (void *)f);
    return f;
}

static ssize_t (*real_getline)(char **, size_t *, FILE *);
static ssize_t rg_getline(char **line, size_t *n, FILE *f) {
    if (!real_getline) *(void **)&real_getline = dlsym(RTLD_NEXT, "getline");
    const ssize_t r = real_getline(line, n, f);
    logf_("getline(%p) -> %zd \"%.60s\"", (void *)f, r, r > 0 && line && *line ? *line : "");
    return r;
}

static void logf_(const char *fmt, ...) {
    if (!log_on) return;
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    fprintf(stderr, "rg_v4l2_fake: %s\n", b);
}

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int video_index(const char *path) {
    if (!path || strncmp(path, "/dev/video", 10) != 0) return -1;
    char *end;
    const long n = strtol(path + 10, &end, 10);
    if (end == path + 10 || *end || n < 0 || n >= n_fake) return -1;
    return (int)n;
}

static cam *find_cam(int fd) {
    if (fd < 0) return NULL;
    for (int i = 0; i < MAX_CAMS; i++)
        if (cams[i].fd == fd) return &cams[i];
    return NULL;
}

static void set_fmt(cam *c, uint32_t w, uint32_t h) {
    c->w = w ? w : 640, c->h = h ? h : 480, c->pixfmt = V4L2_PIX_FMT_YUYV;
    c->bpl = c->w * 2, c->size = c->bpl * c->h;
}

/* Een beeld in `p` (len bytes), YUYV. Proef (RG_V4L2_BRIGHT=lijst van cameranummers, bv. "0"): die camera's geven een wit beeld
   (Y = 235), de rest zwart (Y = 16). De automatische koppeling camera <-> speler van Halo kijkt of precies een camera meer dan 80
   helderheid ziet (cmp $0x50 bij 0x4547a1; gemeten: zwart gaf "(16, 16, 0, 0)"). Hier komt later wat het spel moet zien om te richten. */
static int is_bright(const cam *c) {
    const char *b = getenv("RG_V4L2_BRIGHT");
    if (!b) return 0;
    for (const char *q = b; *q; q++)
        if (*q >= '0' && *q <= '9' && *q - '0' == c->index) return 1;
    return 0;
}
static void fill_frame(const cam *c, uint8_t *p, size_t len) {
    const uint8_t y = is_bright(c) ? 235 : 16;
    for (size_t i = 0; i + 1 < len; i += 2) p[i] = y, p[i + 1] = 128;
}

/* Wacht tot het volgende beeld aan de beurt is (60 per seconde); roep aan ZONDER het slot. */
static void pace(uint64_t due) {
    const uint64_t now = now_ns();
    if (due <= now) return;
    const uint64_t d = due - now;
    struct timespec ts = {(time_t)(d / 1000000000ull), (long)(d % 1000000000ull)};
    nanosleep(&ts, NULL);
}

static uint64_t claim_slot(cam *c) {
    const uint64_t now = now_ns(), period = 1000000000ull / 60;
    uint64_t due = c->next_ns < now ? now : c->next_ns;
    c->next_ns = due + period;
    return due;
}

static int open_cam(int idx) {
    const int fd = eventfd(1, EFD_CLOEXEC);   /* teller 1: altijd leesbaar voor select()/poll() */
    if (fd < 0) return -1;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_CAMS; i++)
        if (cams[i].fd < 0) {
            cams[i].fd = fd, cams[i].index = idx, cams[i].next_ns = 0;
            set_fmt(&cams[i], 640, 480);
            break;
        }
    pthread_mutex_unlock(&lock);
    logf_("open /dev/video%d -> fd %d", idx, fd);
    return fd;
}

int open(const char *path, int flags, ...) {
    init();
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    const int idx = video_index(path);
    if (idx >= 0) return open_cam(idx);
    if (log_on && (strstr(path, "video") || strncmp(path, "/dev/", 5) == 0)) logf_("open %s (doorgegeven)", path);
    return real_open(path, flags, mode);
}

int open64(const char *path, int flags, ...) {
    init();
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    const int idx = video_index(path);
    if (idx >= 0) return open_cam(idx);
    if (log_on && (strstr(path, "video") || strncmp(path, "/dev/", 5) == 0)) logf_("open64 %s (doorgegeven)", path);
    return real_open64 ? real_open64(path, flags, mode) : real_open(path, flags, mode);
}

static int cam_ioctl(cam *c, unsigned long req, void *arg) {
    switch (req) {
    case VIDIOC_QUERYCAP: {
        struct v4l2_capability *cap = arg;
        memset(cap, 0, sizeof *cap);
        snprintf((char *)cap->driver, sizeof cap->driver, "uvcvideo");
        snprintf((char *)cap->card, sizeof cap->card, "RavenGun gun camera %d", c->index);
        snprintf((char *)cap->bus_info, sizeof cap->bus_info, "usb-ravengun-%d", c->index + 1);
        cap->version = 0x00050f00;
        cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
        cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
        return 0;
    }
    case VIDIOC_ENUM_FMT: {
        struct v4l2_fmtdesc *f = arg;
        if (f->index != 0 || f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) { errno = EINVAL; return -1; }
        const uint32_t i = f->index, t = f->type;
        memset(f, 0, sizeof *f);
        f->index = i, f->type = t, f->pixelformat = V4L2_PIX_FMT_YUYV;
        snprintf((char *)f->description, sizeof f->description, "YUYV 4:2:2");
        return 0;
    }
    case VIDIOC_G_FMT:
    case VIDIOC_S_FMT:
    case VIDIOC_TRY_FMT: {
        struct v4l2_format *f = arg;
        if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) { errno = EINVAL; return -1; }
        if (req != VIDIOC_G_FMT) {
            logf_("fd %d %s %ux%u %.4s", c->fd, req == VIDIOC_S_FMT ? "S_FMT" : "TRY_FMT", f->fmt.pix.width, f->fmt.pix.height,
                  (const char *)&f->fmt.pix.pixelformat);
            if (req == VIDIOC_S_FMT) set_fmt(c, f->fmt.pix.width, f->fmt.pix.height);
        }
        f->fmt.pix.width = req == VIDIOC_TRY_FMT && f->fmt.pix.width ? f->fmt.pix.width : c->w;
        f->fmt.pix.height = req == VIDIOC_TRY_FMT && f->fmt.pix.height ? f->fmt.pix.height : c->h;
        f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        f->fmt.pix.field = V4L2_FIELD_NONE;
        f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
        f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
        return 0;
    }
    case VIDIOC_G_PARM:
    case VIDIOC_S_PARM: {
        struct v4l2_streamparm *p = arg;
        if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) { errno = EINVAL; return -1; }
        p->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
        p->parm.capture.timeperframe.numerator = 1;
        p->parm.capture.timeperframe.denominator = 60;
        p->parm.capture.readbuffers = 2;
        return 0;
    }
    case VIDIOC_S_PRIORITY:
    case VIDIOC_G_PRIORITY:
    case VIDIOC_STREAMON:
        return 0;
    case VIDIOC_STREAMOFF:
        for (uint32_t i = 0; i < c->nbufs; i++) c->buf[i].queued = 0;
        return 0;
    case VIDIOC_REQBUFS: {
        struct v4l2_requestbuffers *r = arg;
        logf_("fd %d REQBUFS count %u memory %u", c->fd, r->count, r->memory);
        if (r->type != V4L2_BUF_TYPE_VIDEO_CAPTURE || (r->memory != V4L2_MEMORY_USERPTR && r->memory != V4L2_MEMORY_MMAP)) {
            errno = EINVAL;
            return -1;
        }
        if (r->count > MAX_BUFS) r->count = MAX_BUFS;
        c->nbufs = r->count, c->memory = r->memory;
        memset(c->buf, 0, sizeof c->buf);
        r->capabilities = V4L2_BUF_CAP_SUPPORTS_USERPTR | V4L2_BUF_CAP_SUPPORTS_MMAP;
        return 0;
    }
    case VIDIOC_QUERYBUF:
    case VIDIOC_QBUF: {
        struct v4l2_buffer *b = arg;
        if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE || b->index >= c->nbufs) { errno = EINVAL; return -1; }
        if (req == VIDIOC_QBUF) {
            if (c->memory == V4L2_MEMORY_USERPTR) {
                if (!b->m.userptr) { errno = EINVAL; return -1; }
                c->buf[b->index].ptr = b->m.userptr, c->buf[b->index].len = b->length;
            } else {
                if (!c->buf[b->index].map) { errno = EINVAL; return -1; }   /* eerst mmap */
                c->buf[b->index].ptr = (unsigned long)c->buf[b->index].map, c->buf[b->index].len = c->size;
            }
            c->buf[b->index].queued = 1, c->buf[b->index].order = ++c->qcounter;
        }
        b->memory = c->memory;
        if (c->memory == V4L2_MEMORY_MMAP) b->m.offset = b->index * stride(c), b->length = c->size;
        else b->length = req == VIDIOC_QBUF ? b->length : c->size;
        b->flags = c->buf[b->index].queued ? V4L2_BUF_FLAG_QUEUED : 0;
        return 0;
    }
    case VIDIOC_QUERYCTRL:
    case VIDIOC_G_CTRL:
    case VIDIOC_S_CTRL:
    case VIDIOC_CROPCAP:
    case VIDIOC_S_CROP:
    default:
        logf_("fd %d ioctl 0x%lx -> EINVAL", c->fd, req);
        errno = EINVAL;
        return -1;
    }
}

int ioctl(int fd, unsigned long req, ...) {
    init();
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    pthread_mutex_lock(&lock);
    cam *c = find_cam(fd);
    if (c && (uint32_t)req == (uint32_t)VIDIOC_DQBUF) {
        struct v4l2_buffer *b = arg;
        const uint64_t due = claim_slot(c);
        pthread_mutex_unlock(&lock);
        pace(due);
        pthread_mutex_lock(&lock);
        c = find_cam(fd);
        int best = -1;
        for (uint32_t i = 0; c && i < c->nbufs; i++)
            if (c->buf[i].queued && (best < 0 || c->buf[i].order < c->buf[best].order)) best = (int)i;
        if (!c || best < 0) {
            pthread_mutex_unlock(&lock);
            errno = EAGAIN;
            return -1;
        }
        const size_t len = c->buf[best].len < c->size ? c->buf[best].len : c->size;
        fill_frame(c, (uint8_t *)c->buf[best].ptr, len);
        c->buf[best].queued = 0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        b->index = (uint32_t)best, b->type = V4L2_BUF_TYPE_VIDEO_CAPTURE, b->memory = c->memory;
        b->bytesused = (uint32_t)len, b->field = V4L2_FIELD_NONE, b->sequence = c->seq++;
        b->flags = V4L2_BUF_FLAG_DONE | V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
        b->timestamp.tv_sec = ts.tv_sec, b->timestamp.tv_usec = ts.tv_nsec / 1000;
        if (c->memory == V4L2_MEMORY_MMAP) b->m.offset = (uint32_t)best * stride(c), b->length = c->size;
        else b->m.userptr = c->buf[best].ptr, b->length = c->buf[best].len;
        pthread_mutex_unlock(&lock);
        return 0;
    }
    if (c) {
        /* De kernel kijkt alleen naar de onderste 32 bits van het verzoek; Halo geeft het teken-uitgebreid door
           (0xffffffff80685600 voor VIDIOC_QUERYCAP, gemeten 2026-10-15). */
        const int r = cam_ioctl(c, (unsigned long)(uint32_t)req, arg);
        pthread_mutex_unlock(&lock);
        return r;
    }
    pthread_mutex_unlock(&lock);
    return real_ioctl(fd, req, arg);
}

ssize_t read(int fd, void *buf, size_t n) {
    init();
    pthread_mutex_lock(&lock);
    cam *c = find_cam(fd);
    if (!c) {
        pthread_mutex_unlock(&lock);
        return real_read(fd, buf, n);
    }
    const uint32_t size = c->size;
    const uint64_t due = claim_slot(c);
    const cam snapshot = *c;
    pthread_mutex_unlock(&lock);
    pace(due);
    const size_t len = n < size ? n : size;
    fill_frame(&snapshot, buf, len);
    return (ssize_t)len;
}

int close(int fd) {
    init();
    pthread_mutex_lock(&lock);
    cam *c = find_cam(fd);
    if (c) {
        logf_("close fd %d (/dev/video%d)", fd, c->index);
        c->fd = -1;
    }
    pthread_mutex_unlock(&lock);
    return real_close(fd);
}

/* mmap op een nep-fd (MMAP-streaming): gewoon geheugen per buffer; de offset kiest de buffer (QUERYBUF gaf index * stride). */
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);
static int (*real_munmap)(void *, size_t);
static void *rg_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    if (!real_mmap) *(void **)&real_mmap = dlsym(RTLD_NEXT, "mmap");
    pthread_mutex_lock(&lock);
    cam *c = find_cam(fd);
    if (c && c->memory == V4L2_MEMORY_MMAP && stride(c) && (uint64_t)off % stride(c) == 0 && (uint64_t)off / stride(c) < c->nbufs) {
        const uint32_t i = (uint32_t)((uint64_t)off / stride(c));
        void *p = real_mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) c->buf[i].map = p;
        logf_("mmap fd %d buffer %u (%zu bytes) -> %p", fd, i, len, p);
        pthread_mutex_unlock(&lock);
        (void)addr, (void)prot, (void)flags;
        return p;
    }
    pthread_mutex_unlock(&lock);
    return real_mmap(addr, len, prot, flags, fd, off);
}
static int rg_munmap(void *p, size_t len) {
    if (!real_munmap) *(void **)&real_munmap = dlsym(RTLD_NEXT, "munmap");
    pthread_mutex_lock(&lock);
    for (int k = 0; k < MAX_CAMS; k++)
        for (int i = 0; i < MAX_BUFS; i++)
            if (cams[k].buf[i].map == p) cams[k].buf[i].map = NULL;
    pthread_mutex_unlock(&lock);
    return real_munmap(p, len);
}

/* De GOT van het hoofdprogramma: sommige games (Halo: Fireteam Raven) binden open/ioctl/read/close rechtstreeks aan libc, zodat
   LD_PRELOAD ze niet onderschept (gemeten 2026-10-15: GOT van open = het adres in libc.so.6, niet dat van deze bibliotheek). We zoeken
   de relocaties (DT_JMPREL en DT_RELA) van het hoofdprogramma op naam op en zetten die GOT-plaatsen op onze functies. */
struct got_ctx { int done; };
typedef void (*fnptr)(void);
static void got_patch_one(ElfW(Addr) base, ElfW(Addr) slot, fnptr fn, const char *name) {
    void **p = (void **)(base + slot);
    const long pg = sysconf(_SC_PAGESIZE);
    void *page = (void *)((uintptr_t)p & ~(uintptr_t)(pg - 1));
    if (mprotect(page, (size_t)pg * 2, PROT_READ | PROT_WRITE) != 0) { logf_("GOT %s: mprotect mislukt (%d)", name, errno); return; }
    void *nieuw;
    memcpy(&nieuw, &fn, sizeof nieuw);
    logf_("GOT %s op %p: %p -> %p", name, (void *)p, *p, nieuw);
    *p = nieuw;
}
/* De oude GOT: de herbouwde dump van Halo springt via een tweede GOT (0x1691xxx) die vóór onze constructor al met de adressen in
   libc gevuld is (gemeten: PLT 0x40d990 -> GOT 0x1691250 = open in libc.so.6). Die plaatsen staan niet in de relocaties, dus
   zoeken we in de beschrijfbare segmenten van het hoofdprogramma naar pointers die PRECIES op die libc-functies wijzen. */
static void got_scan(const struct dl_phdr_info *info) {
    void *libc[10];
    *(void **)&libc[0] = dlsym(RTLD_NEXT, "open");
    *(void **)&libc[1] = dlsym(RTLD_NEXT, "open64");
    *(void **)&libc[2] = dlsym(RTLD_NEXT, "ioctl");
    *(void **)&libc[3] = dlsym(RTLD_NEXT, "read");
    *(void **)&libc[4] = dlsym(RTLD_NEXT, "close");
    *(void **)&libc[5] = dlsym(RTLD_NEXT, "popen");
    *(void **)&libc[6] = dlsym(RTLD_NEXT, "getline");
    *(void **)&libc[7] = dlsym(RTLD_NEXT, "mmap");
    *(void **)&libc[8] = dlsym(RTLD_NEXT, "mmap64");
    *(void **)&libc[9] = dlsym(RTLD_NEXT, "munmap");
    fnptr ours[10] = {(fnptr)open, (fnptr)open64, (fnptr)ioctl, (fnptr)read, (fnptr)close, (fnptr)rg_popen, (fnptr)rg_getline,
                      (fnptr)rg_mmap, (fnptr)rg_mmap, (fnptr)rg_munmap};
    static const char *names[10] = {"open", "open64", "ioctl", "read", "close", "popen", "getline", "mmap", "mmap64", "munmap"};
    int n = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_W)) continue;
        uintptr_t a = (info->dlpi_addr + ph->p_vaddr + 7) & ~(uintptr_t)7, e = info->dlpi_addr + ph->p_vaddr + ph->p_filesz;
        for (; a + sizeof(void *) <= e; a += sizeof(void *)) {
            void *v = *(void **)a;
            for (int k = 0; k < 10; k++)
                if (v && v == libc[k]) {
                    got_patch_one(0, a, ours[k], names[k]);
                    n++;
                    break;   /* open en open64 zijn in libc hetzelfde adres: een keer omzetten */
                }
        }
    }
    logf_("oude GOT: %d plaatsen omgezet", n);
}

static int got_cb(struct dl_phdr_info *info, size_t sz, void *data) {
    (void)sz;
    struct got_ctx *ctx = data;
    if (ctx->done++) return 1;   /* alleen het eerste object: het hoofdprogramma */
    const ElfW(Dyn) *dyn = NULL;
    for (int i = 0; i < info->dlpi_phnum; i++)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) dyn = (const ElfW(Dyn) *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    if (!dyn) return 1;
    const ElfW(Sym) *symtab = NULL;
    const char *strtab = NULL;
    const ElfW(Rela) *jmprel = NULL, *rela = NULL;
    size_t jmpsz = 0, relasz = 0;
    for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
        const ElfW(Addr) v = d->d_un.d_ptr;
        switch (d->d_tag) {
        case DT_SYMTAB: symtab = (const ElfW(Sym) *)v; break;
        case DT_STRTAB: strtab = (const char *)v; break;
        case DT_JMPREL: jmprel = (const ElfW(Rela) *)v; break;
        case DT_PLTRELSZ: jmpsz = d->d_un.d_val; break;
        case DT_RELA: rela = (const ElfW(Rela) *)v; break;
        case DT_RELASZ: relasz = d->d_un.d_val; break;
        }
    }
    got_scan(info);
    if (!symtab || !strtab) return 1;
    static const struct { const char *name; fnptr fn; } want[] = {
        {"open", (fnptr)open}, {"open64", (fnptr)open64}, {"ioctl", (fnptr)ioctl}, {"read", (fnptr)read}, {"close", (fnptr)close},
        {"popen", (fnptr)rg_popen}, {"getline", (fnptr)rg_getline}, {"mmap", (fnptr)rg_mmap}, {"mmap64", (fnptr)rg_mmap},
        {"munmap", (fnptr)rg_munmap}};
    const ElfW(Rela) *tabs[2] = {jmprel, rela};
    const size_t sizes[2] = {jmpsz, relasz};
    for (int t = 0; t < 2; t++) {
        if (!tabs[t]) continue;
        for (size_t i = 0; i < sizes[t] / sizeof(ElfW(Rela)); i++) {
            const ElfW(Rela) *r = &tabs[t][i];
            const unsigned type = ELF64_R_TYPE(r->r_info), si = ELF64_R_SYM(r->r_info);
            if ((type != R_X86_64_JUMP_SLOT && type != R_X86_64_GLOB_DAT && type != R_X86_64_64) || !si) continue;
            const char *n = strtab + symtab[si].st_name;
            for (size_t w = 0; w < sizeof want / sizeof want[0]; w++)
                if (strcmp(n, want[w].name) == 0) got_patch_one(info->dlpi_addr, r->r_offset, want[w].fn, n);
        }
    }
    return 1;
}

/* Meting (RG_V4L2_LOG=1): waar wijst de PLT-ingang van open in het hoofdprogramma heen? Een `jmp *disp32(%rip)` (ff 25) lezen we
   uit het eigen geheugen; de GOT-waarde zegt of open() hier terechtkomt. RG_V4L2_PLT=adres (hex) kiest de PLT-ingang. */
__attribute__((constructor)) static void rg_v4l2_fake_init(void) {
    init();
    if (n_fake > 0) {
        struct got_ctx ctx = {0};
        dl_iterate_phdr(got_cb, &ctx);
        /* Kinderen van het spel (popen: sh -c "ls -1 /dev/video*", sha1sum, find) erven LD_PRELOAD. De HASP-shim van de dump
           (halo.so) laat een kindproces dan stil sterven, zodat popen niets teruggeeft (gemeten 2026-10-15 met nsenter: sh -c ls
           met halo.so in LD_PRELOAD geeft geen enkele regel). De bibliotheken zijn in dit proces al geladen; de kinderen hebben ze
           niet nodig. RG_V4L2_KEEP_PRELOAD=1 laat LD_PRELOAD staan. */
        const char *keep = getenv("RG_V4L2_KEEP_PRELOAD");
        if (!(keep && *keep == '1')) {
            logf_("LD_PRELOAD voor kindprocessen gewist (was %s)", getenv("LD_PRELOAD") ? getenv("LD_PRELOAD") : "-");
            unsetenv("LD_PRELOAD");
        }
    }
    if (!log_on) return;
    logf_("geladen: %d virtuele camera('s); open in libc %p, RTLD_DEFAULT open %p", n_fake, dlsym(RTLD_NEXT, "open"), dlsym(RTLD_DEFAULT, "open"));
    const char *plt = getenv("RG_V4L2_PLT");
    if (!plt) return;
    const uint8_t *p = (const uint8_t *)(uintptr_t)strtoull(plt, NULL, 16);
    if (p[0] == 0xff && p[1] == 0x25) {
        int32_t disp;
        memcpy(&disp, p + 2, 4);
        void *const *got = (void *const *)(p + 6 + disp);
        Dl_info di;
        void *tgt = *got;
        const char *who = dladdr(tgt, &di) && di.dli_fname ? di.dli_fname : "?";
        logf_("PLT %p -> GOT %p = %p (%s)", (const void *)p, (const void *)got, tgt, who);
    } else {
        logf_("PLT %p begint met %02x %02x (geen jmp *rip)", (const void *)p, p[0], p[1]);
    }
}

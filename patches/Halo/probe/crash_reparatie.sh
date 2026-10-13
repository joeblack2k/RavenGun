#!/bin/bash
# De crash van run 4 repareren, langs twee kanten:
#   1. de HULP leest het schakelaarbeeld niet meer met mmap maar met pread -- een schrijver die het
#      bestand ook maar even afkapt kan een mapping ongeldig maken (SIGBUS), pread niet;
#   2. mijn PULSEN schrijft niet meer met "wb" (dat kapt af) maar op zijn plaats.
# Daarna een bench-vrije proef: dezelfde afkappende schrijver, twee lezers (mmap vs pread).
set -u
R=~/projects/ravengun
S="$R/tools/rt-input-shim/rg_rt_input.c"
SO="$R/build/librg_rt_input.so"
TMP=$(mktemp -d /tmp/puls-XXXX)

echo "############ 1. de oorzaak, uit de ontleding"
echo "  +0x1f44 = movzbl 0x8(%rdi),%edx  -> dat is de regel 'bits = st[8] | st[9]<<8 | ...'"
echo "  en si_addr was 0x7f035b03c008 = st+8, dus de lezing van het GEMMAPTE schakelaarbeeld."
echo "  Met een mmap van een bestand dat door de schrijver even op 0 bytes wordt gezet, geeft de kernel"
echo "  SIGBUS (BUS_ADRERR, in dit logboek als SEGV_ACCERR nummer 2 weergegeven)."
echo "  In run 4 schreef ik met open(...,'wb') -- dat KAPT AF. Twee pulsen kort na elkaar = twee keer"
echo "  een afkapvenster = de crash op 10:30:08."

echo
echo "############ 2. de hulp repareren: mmap -> pread"
cp -a "$S" "$TMP/rg_rt_input.c.orig"
python3 - "$S" <<'PY'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8").read()
orig = s

# de lezer: geen mmap meer, maar pread per ronde
oud_lezer = """    const volatile uint8_t *st = 0;
    uint32_t prev = 0;"""
nieuw_lezer = """    /* LET OP: hier stond een mmap van het schakelaarbeeld. Dat is gevaarlijk: een schrijver die het
       bestand ook maar even afkapt (open met "wb" doet dat) maakt de mapping ongeldig, en dan sterft
       het spel met SIGBUS op de eerstvolgende lezing. Dat is precies wat in run 4 gebeurde
       (librg_rt_input.so+0x1f44, lezend op st+8). pread kan daar niet op stuklopen. */
    int shm_fd = -1;
    uint8_t buf[64];
    uint32_t prev = 0;"""
assert oud_lezer in s, "lezer-blok niet gevonden"
s = s.replace(oud_lezer, nieuw_lezer, 1)

oud_open = """        if (!st) {
            int fd = open(shm, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                void *p = mmap(0, 64, PROT_READ, MAP_SHARED, fd, 0); close(fd);
                if (p != MAP_FAILED) { st = (const volatile uint8_t *)p; logf_("gedeeld geheugen gekoppeld"); }
            }
            if (!st) { usleep(250000); continue; }
        }"""
nieuw_open = """        if (shm_fd < 0) {
            shm_fd = open(shm, O_RDONLY | O_CLOEXEC);
            if (shm_fd < 0) { usleep(250000); continue; }
            logf_("gedeeld geheugen gekoppeld (pread)");
        }"""
assert oud_open in s, "open-blok niet gevonden"
s = s.replace(oud_open, nieuw_open, 1)

oud_bits = """        uint32_t bits = (uint32_t)st[8] | ((uint32_t)st[9] << 8) | ((uint32_t)st[10] << 16) | ((uint32_t)st[11] << 24);"""
nieuw_bits = """        ssize_t gelezen = pread(shm_fd, buf, sizeof buf, 0);
        if (gelezen < 12) { usleep(10000); continue; }   /* bestand (nog) te kort: niets lezen */
        uint32_t bits = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) | ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);"""
assert oud_bits in s, "bits-regel niet gevonden"
s = s.replace(oud_bits, nieuw_bits, 1)

io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("  hulp aangepast: mmap eruit, pread erin (%d -> %d bytes)" % (len(orig), len(s)))
PY
echo "  --- controle: komt mmap nog voor, en pread wel?"
grep -c 'mmap' "$S" | sed 's/^/    mmap: /'
grep -c 'pread' "$S" | sed 's/^/    pread: /'

echo
echo "############ 3. de hulp opnieuw bouwen"
gcc -shared -fPIC -O2 -o "$SO" "$S" -lpthread 2>&1 | head -8
echo "  gcc exitcode ${PIPESTATUS[0]}"
ls -l "$SO" | sed 's/^/  /'
sha256sum "$SO" | cut -c1-16 | sed 's/^/  sha256 /'
echo "  --- en op welke offset zit de lezing nu? (moet niet meer op +0x1f44 met een movzbl op (%rdi))"
objdump -d "$SO" 2>/dev/null | grep -c 'pread@plt' | sed 's/^/    pread-aanroepen: /'

echo
echo "############ 4. DE PROEF (bench-vrij): dezelfde afkappende schrijver, twee lezers"
cat > "$TMP/lezer.c" <<'EOF'
/* Twee lezers van hetzelfde schakelaarbeeld: een die mmap gebruikt (zoals de oude hulp) en een die
   pread gebruikt (zoals de nieuwe). Een kindproces kapt het bestand af en schrijft het opnieuw --
   precies wat open(...,"wb") doet. De mmap-lezer hoort te sterven met SIGBUS; de pread-lezer niet. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *pad;
static volatile int stop;

static void schrijver(void)          /* kapt af en schrijft opnieuw: de oude manier */
{
    for (int i = 0; i < 200; i++) {
        FILE *f = fopen(pad, "wb");  /* <-- dit KAPT AF */
        if (f) { unsigned char b[64]; memset(b, 0, sizeof b); b[8] = (i & 1) ? 0x40 : 0x00; fwrite(b, 1, 64, f); fclose(f); }
        usleep(2000);
    }
    stop = 1;
}

int main(int argc, char **argv)
{
    int mmap_versie = atoi(argv[1]);
    pad = argv[2];
    int fd = open(pad, O_RDONLY);
    if (fd < 0) { perror("open"); return 2; }
    const unsigned char *st = NULL;
    if (mmap_versie) {
        void *p = mmap(0, 64, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) { perror("mmap"); return 2; }
        st = p;
    }
    pid_t k = fork();
    if (k == 0) { schrijver(); _exit(0); }
    unsigned long rondes = 0;
    while (!stop) {
        unsigned char buf[64];
        if (mmap_versie) {
            volatile unsigned int x = st[8];      /* <-- hier sterft de oude versie */
            (void)x;
        } else {
            ssize_t r = pread(fd, buf, sizeof buf, 0);
            if (r < 12) { usleep(1000); continue; }
            volatile unsigned int y = buf[8];
            (void)y;
        }
        rondes++;
    }
    waitpid(k, NULL, 0);
    printf("    %s: %lu rondes gelezen, geen fout\n", mmap_versie ? "mmap-lezer" : "pread-lezer", rondes);
    return 0;
}
EOF
gcc -O1 -o "$TMP/lezer" "$TMP/lezer.c" 2>&1 | head -5
echo "  gcc exitcode ${PIPESTATUS[0]}"
python3 -c "open('$TMP/jvs','wb').write(bytes(64))"

echo "  --- de OUDE manier (mmap) met een afkappende schrijver:"
"$TMP/lezer" 1 "$TMP/jvs" >/dev/null 2>&1
echo "    exitcode $? (139 = gestorven aan SIGSEGV/SIGBUS, dat is de fout van run 4)"
echo "  --- de NIEUWE manier (pread) met dezelfde afkappende schrijver:"
"$TMP/lezer" 0 "$TMP/jvs" 2>&1 | sed 's/^/  /'
echo "    exitcode ${PIPESTATUS[0]}"

echo
echo "############ 5. en mijn PULSEN zonder afkappen (in plaats van 'wb')"
python3 - "$TMP/jvs" <<'PY'
import os, struct, sys, time
p = sys.argv[1]
fd = os.open(p, os.O_WRONLY)
minsize = 64
for waarde in (0, 0x40, 0x01, 0x00, 0x01):     # START, TEST, en de tweede TEST: allemaal in-place
    os.pwrite(fd, struct.pack("<I", waarde), 8)
    time.sleep(0.05)
    minsize = min(minsize, os.path.getsize(p))
os.close(fd)
print("  het bestand is tijdens het pulsen nooit kleiner geworden dan %d bytes" % minsize)
PY
echo "  --- en de pread-lezer overleeft dat ook:"
python3 -c "open('$TMP/jvs2','wb').write(bytes(64))" 
"$TMP/lezer" 0 "$TMP/jvs2" 2>&1 | sed 's/^/  /'
echo "== done ($TMP)"

/* Lokaal gemaakt door tools/dev/rt-glfix.sh: gnu/stubs-32.h ontbreekt in lib32-glibc.
   De inhoud is gelijk aan gnu/stubs-64.h; zie /usr/include/gnu/stubs.h voor de selectie. */
#ifdef _LIBC
 #error Applications may not define the macro _LIBC
#endif
#define __stub___compat_bdflush
#define __stub_chflags
#define __stub_fchflags
#define __stub_gtty
#define __stub_revoke
#define __stub_setlogin
#define __stub_sigreturn
#define __stub_stty

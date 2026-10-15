/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_glslfix.c -- 32-bits LD_PRELOAD voor de native route (rg-elfloader) van Play Mechanix/Raw Thrills-games: GLSL die NVIDIA
 * (de kast) toeliet en Mesa weigert, op het moment dat de game hem aan OpenGL geeft. Geen game-naam, geen adres.
 *
 * GEMETEN 2026-10-15 (Aliens Armageddon; MESA_SHADER_DUMP_PATH en het compilatielogboek van de driver):
 *  1. Fragment-shaders die `out vec4 out_Color;` en `out vec4 out_Bloom;` verklaren en daarnaast gl_FragData[0]/[1] schrijven.
 *     GLSL verbiedt die menging. Herschreven bij glShaderSource: gl_FragData[0] -> out_Color, gl_FragData[1] -> out_Bloom
 *     (dezelfde uitvoeren: de game bindt out_Color op 0 en out_Bloom op 1). Zelfde fout als Jurassic Park
 *     (patches/JurassicPark/maak-shaders.py), maar hier zijn de shaderbestanden versleuteld.
 *  2. "C-style initialization requires the GL_ARB_shading_language_420pack extension" (uniform-initialisatie met {...}).
 *  3. "sampler arrays indexed with non-constant expressions are forbidden in GLSL 1.30 and later".
 *     Voor 2 en 3 heeft Mesa de extensie wel (GL_ARB_shading_language_420pack, GL_ARB_gpu_shader5), maar de shader zet hem niet
 *     aan. ALLEEN als de compilatie met precies die melding mislukt, zet deze bibliotheek de extensie aan (een #extension-regel
 *     na #version) en compileert opnieuw. Een shader die gewoon compileert, blijft onaangeroerd.
 * De game roept glShaderSource/glCompileShader/glLinkProgram rechtstreeks via zijn PLT aan (de lader bindt die met
 * dlsym(RTLD_DEFAULT), dus deze uitgevoerde namen gaan voor) en sommige via glXGetProcAddress(ARB) (GLEW).
 *
 * Logboek: RG_GLSLFIX_LOG=1. Bouwen: CMake (doel rg_glslfix32), gcc -m32 -shared -fPIC.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef void (*shsrc_fn)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void *(*getproc_fn)(const GLubyte *);
typedef void (*compile_fn)(GLuint);
typedef void (*getshiv_fn)(GLuint, unsigned int, GLint *);
typedef void (*getlog_fn)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*link_fn)(GLuint);

static int log_aan = -1, n_herschreven, n_extensie;

static void meld(const char *m) {
    if (log_aan < 0) { const char *l = getenv("RG_GLSLFIX_LOG"); log_aan = l && *l == '1'; }
    if (log_aan) { fprintf(stderr, "[rg_glslfix] %s\n", m); fflush(stderr); }
}

static void *gl(const char *naam) {
    static getproc_fn gp; static int gezocht;
    if (!gezocht) {
        gezocht = 1;
        gp = (getproc_fn)dlsym(RTLD_NEXT, "glXGetProcAddressARB");
        if (!gp) gp = (getproc_fn)dlsym(RTLD_NEXT, "glXGetProcAddress");
    }
    void *f = dlsym(RTLD_NEXT, naam);
    if (!f && gp) f = gp((const GLubyte *)naam);
    return f;
}

/* ---------------------------------------------------------------- de bron per shader (voor een tweede compilatie) */

#define BRONNEN 2048
static struct { GLuint id; char *bron; } bronnen[BRONNEN];

static void bewaar(GLuint sh, const char *s) {
    size_t i = sh % BRONNEN;
    for (size_t k = 0; k < BRONNEN; k++, i = (i + 1) % BRONNEN) {
        if (bronnen[i].bron && bronnen[i].id != sh) continue;
        free(bronnen[i].bron);
        bronnen[i].id = sh;
        bronnen[i].bron = strdup(s);
        return;
    }
}

static const char *bewaarde(GLuint sh) {
    size_t i = sh % BRONNEN;
    for (size_t k = 0; k < BRONNEN; k++, i = (i + 1) % BRONNEN) {
        if (!bronnen[i].bron) return NULL;
        if (bronnen[i].id == sh) return bronnen[i].bron;
    }
    return NULL;
}

/* Vervang elk voorkomen van `van` door `naar` in een nieuwe buffer. */
static char *vervang(char *s, const char *van, const char *naar) {
    size_t lv = strlen(van), ln = strlen(naar), n = 0;
    for (const char *p = s; (p = strstr(p, van)); p += lv) n++;
    if (!n) return s;
    char *uit = malloc(strlen(s) + n * (ln > lv ? ln - lv : 0) + 1), *q = uit;
    if (!uit) return s;
    for (const char *p = s, *t; ; p = t + lv) {
        t = strstr(p, van);
        if (!t) { strcpy(q, p); break; }
        memcpy(q, p, (size_t)(t - p)); q += t - p;
        memcpy(q, naar, ln); q += ln;
    }
    free(s);
    return uit;
}

/* ---------------------------------------------------------------- 1. glShaderSource */

static void bron(shsrc_fn echt, GLuint sh, GLsizei count, const GLchar *const *str, const GLint *len) {
    if (!echt) return;
    size_t tot = 0;
    for (GLsizei i = 0; i < count; i++) tot += len && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]);
    char *s = malloc(tot + 1);
    if (!s) { echt(sh, count, str, len); return; }
    size_t o = 0;
    for (GLsizei i = 0; i < count; i++) {
        size_t l = len && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]);
        memcpy(s + o, str[i], l); o += l;
    }
    s[o] = 0;
    if (strstr(s, "gl_FragData[") && strstr(s, "out vec4 out_Color") && strstr(s, "out vec4 out_Bloom")) {
        s = vervang(s, "gl_FragData[0]", "out_Color");
        s = vervang(s, "gl_FragData[1]", "out_Bloom");
        char m[100]; snprintf(m, sizeof m, "shader %u herschreven (gl_FragData -> out_Color/out_Bloom), %d totaal", sh, ++n_herschreven);
        meld(m);
    }
    const GLchar *een[1] = {s};
    echt(sh, 1, een, NULL);
    bewaar(sh, s);
    free(s);
}

void glShaderSource(GLuint sh, GLsizei c, const GLchar *const *s, const GLint *l) {
    static shsrc_fn echt;
    if (!echt) echt = (shsrc_fn)gl("glShaderSource");
    bron(echt, sh, c, s, l);
}
void glShaderSourceARB(GLuint sh, GLsizei c, const GLchar *const *s, const GLint *l) {
    static shsrc_fn echt;
    if (!echt) echt = (shsrc_fn)gl("glShaderSourceARB");
    bron(echt, sh, c, s, l);
}

/* ---------------------------------------------------------------- 2/3. glCompileShader */

static int status(GLuint sh, char *log, size_t n) {
    static getshiv_fn iv; static getlog_fn lg;
    if (!iv) iv = (getshiv_fn)gl("glGetShaderiv");
    if (!lg) lg = (getlog_fn)gl("glGetShaderInfoLog");
    GLint ok = 1;
    if (iv) iv(sh, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
    if (log) { log[0] = 0; if (!ok && lg) { GLsizei k = 0; lg(sh, (GLsizei)n - 1, &k, log); } }
    return ok;
}

/* Zet `regel` (een #extension) direct na de #version-regel, of vooraan als er geen is. */
static char *met_regel(const char *s, const char *regel) {
    size_t ls = strlen(s), lr = strlen(regel);
    char *uit = malloc(ls + lr + 2);
    if (!uit) return NULL;
    const char *nl = strncmp(s, "#version", 8) == 0 ? strchr(s, '\n') : NULL;
    size_t kop = nl ? (size_t)(nl - s + 1) : 0;
    memcpy(uit, s, kop);
    memcpy(uit + kop, regel, lr);
    strcpy(uit + kop + lr, s + kop);
    return uit;
}

static void compileer(compile_fn echt, GLuint sh) {
    if (!echt) return;
    echt(sh);
    char log[900];
    if (status(sh, log, sizeof log)) return;
    char *nieuw = NULL;
    const char *src = bewaarde(sh);
    if (src && strstr(log, "GL_ARB_shading_language_420pack") && !strstr(src, "GL_ARB_shading_language_420pack"))
        nieuw = met_regel(src, "#extension GL_ARB_shading_language_420pack : enable\n");
    else if (src && strstr(log, "sampler arrays indexed with non-constant") && !strstr(src, "GL_ARB_gpu_shader5"))
        nieuw = met_regel(src, "#extension GL_ARB_gpu_shader5 : enable\n");
    if (nieuw) {
        static shsrc_fn echte_bron;
        if (!echte_bron) echte_bron = (shsrc_fn)gl("glShaderSource");
        const GLchar *een[1] = {nieuw};
        if (echte_bron) { echte_bron(sh, 1, een, NULL); bewaar(sh, nieuw); echt(sh); }
        free(nieuw);
        char log2[900];
        int ok = status(sh, log2, sizeof log2);
        char m[1000];
        if (ok) snprintf(m, sizeof m, "shader %u: extensie aangezet na \"%.120s\", compileert nu (%d totaal)", sh, log, ++n_extensie);
        else snprintf(m, sizeof m, "shader %u: extensie aangezet, compileert nog niet: %.700s", sh, log2);
        for (char *p = m; *p; p++) if (*p == '\n') *p = ' ';
        meld(m);
        if (!ok && strstr(log2, "sampler arrays indexed") && (src = bewaarde(sh)) && !strstr(src, "GL_ARB_gpu_shader5")) {
            /* tweede fout na de eerste: ook die extensie */
            char *n2 = met_regel(src, "#extension GL_ARB_gpu_shader5 : enable\n");
            if (n2 && echte_bron) {
                const GLchar *e2[1] = {n2};
                echte_bron(sh, 1, e2, NULL); bewaar(sh, n2); echt(sh);
                snprintf(m, sizeof m, "shader %u: ook GL_ARB_gpu_shader5, %s", sh, status(sh, NULL, 0) ? "compileert nu" : "compileert nog niet");
                meld(m);
            }
            free(n2);
        }
        return;
    }
    for (char *p = log; *p; p++) if (*p == '\n') *p = ' ';
    char m[1000]; snprintf(m, sizeof m, "shader %u compileert niet: %.900s", sh, log);
    meld(m);
}

void glCompileShader(GLuint sh) {
    static compile_fn echt;
    if (!echt) echt = (compile_fn)gl("glCompileShader");
    compileer(echt, sh);
}
static void rg_glCompileShaderARB(GLuint sh) {
    static compile_fn echt;
    if (!echt) echt = (compile_fn)gl("glCompileShaderARB");
    compileer(echt, sh);
}

/* ---------------------------------------------------------------- diagnose: glLinkProgram */

void glLinkProgram(GLuint prog) {
    static link_fn echt; static getshiv_fn iv; static getlog_fn lg;
    if (!echt) echt = (link_fn)gl("glLinkProgram");
    if (!echt) return;
    echt(prog);
    if (log_aan <= 0) return;
    if (!iv) iv = (getshiv_fn)gl("glGetProgramiv");
    if (!lg) lg = (getlog_fn)gl("glGetProgramInfoLog");
    GLint ok = 1;
    if (iv) iv(prog, 0x8B82 /* GL_LINK_STATUS */, &ok);
    if (!ok && lg) {
        char buf[800] = {0}; GLsizei n = 0;
        lg(prog, sizeof buf - 1, &n, buf);
        for (char *p = buf; *p; p++) if (*p == '\n') *p = ' ';
        char m[900]; snprintf(m, sizeof m, "programma %u koppelt niet: %s", prog, buf);
        meld(m);
    }
}

/* ---------------------------------------------------------------- glXGetProcAddress(ARB) (GLEW) */

static void *getproc(const GLubyte *naam, const char *wie) {
    const char *n = (const char *)naam;
    if (n && strcmp(n, "glShaderSource") == 0) return (void *)glShaderSource;
    if (n && strcmp(n, "glShaderSourceARB") == 0) return (void *)glShaderSourceARB;
    if (n && strcmp(n, "glCompileShader") == 0) return (void *)glCompileShader;
    if (n && strcmp(n, "glCompileShaderARB") == 0) return (void *)rg_glCompileShaderARB;
    getproc_fn echt = (getproc_fn)dlsym(RTLD_NEXT, wie);
    return echt ? echt(naam) : NULL;
}
void *glXGetProcAddressARB(const GLubyte *naam) { return getproc(naam, "glXGetProcAddressARB"); }
void *glXGetProcAddress(const GLubyte *naam) { return getproc(naam, "glXGetProcAddress"); }

__attribute__((constructor)) static void rg_glslfix_init(void) { meld("geladen: glShaderSource/glCompileShader/glLinkProgram en glXGetProcAddress(ARB) omgeleid"); }

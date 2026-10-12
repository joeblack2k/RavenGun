/* rg_ext.c -- de 6 functies die BudgieLoader exporteert, die onze eigen
 * libglut.so.3 niet heeft en die geen doorverwijzing naar de echte libglut
 * kunnen zijn (de echte libglut kent ze niet):
 *
 *   glutAppStatusFunc          (freeglut-ext, mobiel levenscyclus: cb(int))
 *   glutInitContextFunc        (freeglut-ext, mobiel levenscyclus: cb(void))
 *   glutPositionFunc           (freeglut-ext, cb(int,int))
 *   glutSetVertexAttribCoord3  (freeglut-ext, GLint attrib)
 *   glutSetVertexAttribNormal  (freeglut-ext, GLint attrib)
 *   glutSetVertexAttribTexCoord2 (freeglut-ext, GLint attrib)
 *
 * De prototypes komen uit freeglut 3.4.0 (include/GL/freeglut_ext.h, opgehaald
 * van github.com/freeglut/freeglut tag v3.4.0).
 *
 * BELANGRIJK (aanname, zie README.md "Wat open blijft"): onze echte libglut is
 * een oudere freeglut die deze zes niet kent en er dus ook geen haak voor heeft.
 * Deze functies bewaren hun argument in eigen toestand en doen verder niets: de
 * callback die je hier zet zal nooit vuren en de attribuut-index wordt nergens
 * gebruikt. Ze staan er zodat de naam bestaat en een aanroep niet crasht.
 */
#include "rg_shim.h"

static void (*rg_cb_appstatus)(int);
static void (*rg_cb_initcontext)(void);
static void (*rg_cb_position)(int, int);
static int rg_attrib_coord3 = -1;
static int rg_attrib_normal = -1;
static int rg_attrib_texcoord2 = -1;

void rg_set_appstatus(void (*cb)(int)) { rg_cb_appstatus = cb; }
void rg_set_initcontext(void (*cb)(void)) { rg_cb_initcontext = cb; }
void rg_set_position(void (*cb)(int, int)) { rg_cb_position = cb; }

void glutAppStatusFunc(void (*cb)(int)) { rg_set_appstatus(cb); }
void glutInitContextFunc(void (*cb)(void)) { rg_set_initcontext(cb); }
void glutPositionFunc(void (*cb)(int, int)) { rg_set_position(cb); }
void glutSetVertexAttribCoord3(int attrib) { rg_attrib_coord3 = attrib; }
void glutSetVertexAttribNormal(int attrib) { rg_attrib_normal = attrib; }
void glutSetVertexAttribTexCoord2(int attrib) { rg_attrib_texcoord2 = attrib; }

/* Leest de bewaarde toestand uit. Verborgen, dus niet in de exportlijst; alleen
 * bedoeld zodat de bewaarde waarden ergens gelezen worden en een volgende lezer
 * (of testprogramma binnen de eigen module) ze kan opvragen. */
void rg_ext_status(int *coord3, int *normal, int *texcoord2,
                   void (**appstatus)(int), void (**initcontext)(void),
                   void (**position)(int, int)) __attribute__((visibility("hidden")));

void rg_ext_status(int *coord3, int *normal, int *texcoord2,
                   void (**appstatus)(int), void (**initcontext)(void),
                   void (**position)(int, int))
{
    if (coord3) *coord3 = rg_attrib_coord3;
    if (normal) *normal = rg_attrib_normal;
    if (texcoord2) *texcoord2 = rg_attrib_texcoord2;
    if (appstatus) *appstatus = rg_cb_appstatus;
    if (initcontext) *initcontext = rg_cb_initcontext;
    if (position) *position = rg_cb_position;
}

// Petite bibliothèque de dessin pour Wiil DSi : écrans 256x192 en couleurs 15 bits.
// Tout est dessiné dans une image en mémoire, puis copiée d'un coup à l'écran.
#pragma once
#include <nds.h>
#include <math.h>
#include <string.h>
#include "police.h"

#define LARG 256
#define HAUT 192

// Couleur 24 bits (0xRRGGBB) -> couleur DSi 15 bits (bit 15 = visible)
#define C(rgb) ((u16)((((rgb) >> 19) & 31) | ((((rgb) >> 11) & 31) << 5) | ((((rgb) >> 3) & 31) << 10) | 0x8000))

#define NUIT_ C(0x060912)
#define CIEL_ C(0x38BDF8)
#define BLANC_ C(0xF0F4FA)
#define GRIS_ C(0x96A2B4)

// Zone de découpe verticale (pour la liste qui défile)
static int clip_y0 = 0, clip_y1 = HAUT;

static inline u16 melange(u16 d, u16 s, int a)   // a : 0 (rien) à 255 (opaque)
{
    if (a >= 255) return s;
    if (a <= 0) return d;
    int dr = d & 31, dg = (d >> 5) & 31, db = (d >> 10) & 31;
    int sr = s & 31, sg = (s >> 5) & 31, sb = (s >> 10) & 31;
    dr += ((sr - dr) * a) >> 8;
    dg += ((sg - dg) * a) >> 8;
    db += ((sb - db) * a) >> 8;
    return dr | (dg << 5) | (db << 10) | 0x8000;
}

static inline void point(u16 *e, int x, int y, u16 c, int a)
{
    if (x < 0 || x >= LARG || y < clip_y0 || y >= clip_y1) return;
    e[y * LARG + x] = melange(e[y * LARG + x], c, a);
}

static void rect(u16 *e, int x, int y, int w, int h, u16 c, int a)
{
    int x0 = x < 0 ? 0 : x, x1 = x + w > LARG ? LARG : x + w;
    int y0 = y < clip_y0 ? clip_y0 : y, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    for (int yy = y0; yy < y1; yy++)
    {
        u16 *l = e + yy * LARG;
        if (a >= 255) for (int xx = x0; xx < x1; xx++) l[xx] = c;
        else for (int xx = x0; xx < x1; xx++) l[xx] = melange(l[xx], c, a);
    }
}

// Couverture d'un pixel de coin arrondi (anticrénelage simple)
static inline int couverture(float dx, float dy, float r)
{
    float d = sqrtf(dx * dx + dy * dy);
    float v = r - d + 0.5f;
    if (v <= 0) return 0;
    if (v >= 1) return 256;
    return (int)(v * 256);
}

// Rectangle à coins arrondis, plein (contour = 0) ou seulement le contour (contour = 1)
static void rrect_gen(u16 *e, int x, int y, int w, int h, int r, u16 c, int a, int contour)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int yy = 0; yy < h; yy++)
    {
        int py = y + yy;
        if (py < clip_y0 || py >= clip_y1) continue;
        for (int xx = 0; xx < w; xx++)
        {
            int px = x + xx;
            if (px < 0 || px >= LARG) continue;
            float cx = -1, cy = -1;
            if (xx < r) cx = r - 0.5f; else if (xx >= w - r) cx = w - r - 0.5f;
            if (yy < r) cy = r - 0.5f; else if (yy >= h - r) cy = h - r - 0.5f;
            int cov;
            if (cx >= 0 && cy >= 0)
            {
                float dx = xx - cx, dy = yy - cy;
                cov = couverture(dx, dy, r);
                if (contour) cov = cov - couverture(dx, dy, r - 1.2f);
            }
            else
            {
                cov = 256;
                if (contour && !(xx == 0 || yy == 0 || xx == w - 1 || yy == h - 1)) cov = 0;
            }
            if (cov <= 0) continue;
            int aa = (a * cov) >> 8;
            e[py * LARG + px] = melange(e[py * LARG + px], c, aa);
        }
    }
}
#define rrect(e, x, y, w, h, r, c, a) rrect_gen(e, x, y, w, h, r, c, a, 0)
#define rcontour(e, x, y, w, h, r, c, a) rrect_gen(e, x, y, w, h, r, c, a, 1)

static void disque(u16 *e, int cx, int cy, int r, u16 c, int a)
{
    for (int y = -r - 1; y <= r + 1; y++)
        for (int x = -r - 1; x <= r + 1; x++)
        {
            int cov = couverture(x, y, r);
            if (cov > 0) point(e, cx + x, cy + y, c, (a * cov) >> 8);
        }
}

// Triangle « lecture » pointant à droite
static void triangle_lecture(u16 *e, int x, int y, int h, u16 c)
{
    for (int yy = 0; yy < h; yy++)
    {
        int d = yy < h / 2 ? yy : h - 1 - yy;
        rect(e, x, y + yy, d + 1, 1, c, 255);
    }
}

// Image 15 bits avec coins arrondis (rayon r)
static void image(u16 *e, int x, int y, const u16 *pix, int w, int h, int r)
{
    for (int yy = 0; yy < h; yy++)
    {
        int py = y + yy;
        if (py < clip_y0 || py >= clip_y1) continue;
        for (int xx = 0; xx < w; xx++)
        {
            int px = x + xx;
            if (px < 0 || px >= LARG) continue;
            int cov = 256;
            if (r > 0)
            {
                float cx = -1, cy = -1;
                if (xx < r) cx = r - 0.5f; else if (xx >= w - r) cx = w - r - 0.5f;
                if (yy < r) cy = r - 0.5f; else if (yy >= h - r) cy = h - r - 0.5f;
                if (cx >= 0 && cy >= 0) cov = couverture(xx - cx, yy - cy, r);
            }
            if (cov > 0) e[py * LARG + px] = melange(e[py * LARG + px], pix[yy * w + xx] | 0x8000, cov >= 256 ? 255 : cov);
        }
    }
}

// ------------------------------------------------------------ texte (UTF-8)
static int car_suivant(const char **s)   // renvoie l'index du glyphe, ou -1 à la fin
{
    const unsigned char *p = (const unsigned char *)*s;
    if (!*p) return -1;
    unsigned int cp = *p++;
    if (cp >= 0xE0 && p[0] && p[1]) { cp = ((cp & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (cp >= 0xC0 && p[0]) { cp = ((cp & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    if (cp == 0x2019 || cp == 0x2018) cp = '\'';
    else if (cp == 0x201C || cp == 0x201D) cp = '"';
    else if (cp == 0x2013 || cp == 0x2014) cp = '-';
    else if (cp == 0x2122) cp = ' ';                     // ™ : on l'ignore
    else if (cp == 0x2026) cp = '.';
    if (cp >= 32 && cp <= 126) return cp - 32;
    if (cp >= 160 && cp <= 255) return 95 + cp - 160;
    return '?' - 32;
}

static int largeur(const Police *P, const char *s)
{
    int w = 0, g;
    while ((g = car_suivant(&s)) >= 0) w += P->g[g].av;
    return w;
}

static int texte(u16 *e, const Police *P, int x, int y, const char *s, u16 c, int a)
{
    int g, x0 = x;
    while ((g = car_suivant(&s)) >= 0)
    {
        const Glyphe *gl = &P->g[g];
        const uint8_t *al = P->alpha + gl->off;
        for (int yy = 0; yy < gl->h; yy++)
            for (int xx = 0; xx < gl->w; xx++)
            {
                int v = al[yy * gl->w + xx];
                if (v) point(e, x + gl->x + xx, y + gl->y + yy, c, (v * a) >> 8);
            }
        x += gl->av;
    }
    return x - x0;
}

// Texte coupé avec « ... » s'il dépasse la largeur max
static void texte_coupe(u16 *e, const Police *P, int x, int y, const char *s, int max, u16 c, int a)
{
    if (largeur(P, s) <= max) { texte(e, P, x, y, s, c, a); return; }
    char tmp[160];
    int n = 0;
    const char *p = s;
    int w = largeur(P, "...");
    while (*p && n < 150)
    {
        const char *avant = p;
        int g = car_suivant(&p);
        if (g < 0 || w + P->g[g].av > max) break;
        w += P->g[g].av;
        while (avant < p) tmp[n++] = *avant++;
    }
    while (n > 0 && tmp[n - 1] == ' ') n--;
    strcpy(tmp + n, "...");
    texte(e, P, x, y, tmp, c, a);
}

// Coupe un texte en lignes de largeur max (par mots). Renvoie le nombre de lignes.
static int couper_lignes(const Police *P, const char *s, int max, char lignes[][80], int nb_max)
{
    int n = 0;
    char mot[80], cour[160] = "";
    while (*s && n < nb_max)
    {
        int m = 0;
        while (*s == ' ') s++;
        while (*s && *s != ' ' && m < 79) mot[m++] = *s++;
        mot[m] = 0;
        if (!m) break;
        char essai[160];
        snprintf(essai, sizeof essai, "%s%s%s", cour, cour[0] ? " " : "", mot);
        if (largeur(P, essai) <= max || !cour[0]) snprintf(cour, sizeof cour, "%s", essai);
        else
        {
            snprintf(lignes[n++], 80, "%.79s", cour);
            snprintf(cour, sizeof cour, "%s", mot);
        }
    }
    if (cour[0] && n < nb_max) snprintf(lignes[n++], 80, "%.79s", cour);
    else if (cour[0] && n == nb_max)                     // trop long : on ajoute la suite à la dernière ligne
    {
        char tout[260];
        snprintf(tout, sizeof tout, "%s %s%s", lignes[n - 1], cour, *s ? " ..." : "");
        snprintf(lignes[n - 1], 80, "%.79s", tout);
    }
    return n;
}

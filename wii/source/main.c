/*
 * Wiil — menu personnel pour Wii, ambiance console récente (design original).
 * Version 1 : onglets Jeux / Applis / Rétro, fond flou du jeu sélectionné,
 * cartes de raccourcis, musique d'ambiance, lancement des applis et des jeux
 * (les jeux sont confiés à USB Loader GX grâce à son « démarrage rapide » par ID).
 */
#include <grrlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <ogc/irq.h>
#include <sdcard/wiisd_io.h>
#include <asndlib.h>
#include <tremor/ivorbisfile.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#include <math.h>
#include <ogc/lwp_watchdog.h>

#include "police_ttf.h"
#include "musique_ogg.h"
#include "booter_bin.h"
#include "ombre_gauche_png.h"
#include "ombre_bas_png.h"
#include "ombre_haut_png.h"
#include "cadre_png.h"
#include "halo_png.h"
#include "tuile_png.h"
#include "carte_png.h"
#include "carte_cadre_png.h"
#include "bouton_jouer_png.h"
#include "bouton_options_png.h"
#include "point_png.h"
#include "avatar_png.h"
#include "souligne_png.h"
#include "ic_disque_png.h"
#include "ic_menuwii_png.h"
#include "ic_maison_png.h"
#include "ic_grille_png.h"
#include "ic_flux_png.h"
#include "ic_eteindre_png.h"
#include "ic_manette_png.h"
#include "demo0_png.h"
#include "demo1_png.h"
#include "demo2_png.h"
#include "demo3_png.h"
#include "demo4_png.h"
#include "demo5_png.h"
#include "etoile_png.h"

/* ------------------------------------------------------------------ couleurs */
#define BLANC   0xF0F4FAFF
#define GRIS    0xAAB4C3FF
#define CIEL    0x38BDF8FF
#define NUIT    0x060912FF

#define MAX_ELEMENTS 256
#define CHEMIN_GX    "sd:/apps/usbloader_gx/boot.dol"
#define CHEMIN_WF    "sd:/apps/wiiflow/boot.dol"
#define DOSSIER_WIIL "sd:/apps/wiil"

typedef struct {
	char id[8];            /* jeux : ID à 6 caractères */
	char nom[128];         /* nom affiché (UTF-8) */
	char chemin[256];      /* jeux : jaquette ; applis : boot.dol / boot.elf */
	char image[256];       /* applis : icon.png */
	int gamecube;
	int demo;              /* -1 = vrai élément, sinon index de jaquette de démo */
	int favori;            /* 1 = favori (étoile), classé en premier */
	GRRLIB_texImg *tex;
	GRRLIB_texImg *flou;   /* jeux : fond flou préparé au démarrage */
	char annee[8];         /* fiche du jeu (wiitdb) : « 2008 » */
	char joueurs[16];      /* « 1-4 joueurs » */
	char genre[32];        /* « Course » */
	char editeur[48];      /* « Nintendo » */
} Element;

typedef struct { Element e[MAX_ELEMENTS]; int n; } Liste;

static Liste jeux, applis, retro;
static Liste *onglets[3] = { &jeux, &applis, &retro };
static const wchar_t *noms_onglets[3] = { L"Jeux", L"Applis", L"Rétro" };

static GRRLIB_ttfFont *police;
static GRRLIB_texImg *t_ombre_g, *t_ombre_b, *t_ombre_h, *t_cadre, *t_halo, *t_tuile, *t_carte,
	*t_carte_cadre, *t_jouer, *t_options, *t_point, *t_avatar, *t_souligne, *t_demo[6], *t_etoile;
static GRRLIB_texImg *t_ic[7];
static int sd_ok = 0;

/* GRRLIB_DrawImg agrandit depuis le coin haut-gauche (poignée par défaut à 0,0). */
static void dessin(f32 x, f32 y, const GRRLIB_texImg *t, f32 rot, f32 sx, f32 sy, u32 coul)
{
	if (!t) return;
	GRRLIB_DrawImg(x, y, t, rot, sx, sy, coul);
}

static void zone_face(const GRRLIB_texImg *t, f32 *x0, f32 *w);

/* ------------------------------------------------------------------ texte */
static void utf8_vers_wchar(const char *s, wchar_t *out, int max)
{
	int n = 0;
	const unsigned char *p = (const unsigned char *)s;
	while (*p && n < max - 1) {
		unsigned int c = *p++;
		if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
		else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
		else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
		out[n++] = (wchar_t)c;
	}
	out[n] = 0;
}

static void texte(int x, int y, const char *s, int taille, u32 coul)
{
	wchar_t w[160];
	utf8_vers_wchar(s, w, 160);
	GRRLIB_PrintfTTFW(x, y, police, w, taille, coul);
}

static int largeur(const char *s, int taille)
{
	wchar_t w[160];
	utf8_vers_wchar(s, w, 160);
	return GRRLIB_WidthTTFW(police, w, taille);
}

/* ------------------------------------------------------------------ musique */
typedef struct { const u8 *d; long n, pos; } MemFich;
static MemFich mf;
static OggVorbis_File vf;
static int musique_ok = 0;
#define NB_TAMPONS 4
#define TAILLE_TAMPON 16384
static u8 tampons[NB_TAMPONS][TAILLE_TAMPON] ATTRIBUTE_ALIGN(32);
static int tampon_suivant = 0;

static size_t m_lire(void *p, size_t t, size_t nb, void *ds)
{
	MemFich *m = ds;
	long voulu = (long)(t * nb);
	if (m->pos + voulu > m->n) voulu = m->n - m->pos;
	if (voulu <= 0) return 0;
	memcpy(p, m->d + m->pos, voulu);
	m->pos += voulu;
	return voulu / t;
}
static int m_aller(void *ds, ogg_int64_t off, int d)
{
	MemFich *m = ds;
	long np = d == SEEK_SET ? off : d == SEEK_CUR ? m->pos + off : m->n + off;
	if (np < 0 || np > m->n) return -1;
	m->pos = np;
	return 0;
}
static long m_ou(void *ds) { return ((MemFich *)ds)->pos; }
static int m_fermer(void *ds) { (void)ds; return 0; }

static int remplir(u8 *buf)
{
	int total = 0, bs, essais = 0;
	while (total < TAILLE_TAMPON) {
		long r = ov_read(&vf, (char *)buf + total, TAILLE_TAMPON - total, &bs);
		if (r == 0) {                                    /* fin du morceau : on boucle */
			if (ov_pcm_seek(&vf, 0) != 0) break;         /* sécurité : jamais de boucle infinie */
			if (++essais > 2) break;
			continue;
		}
		if (r < 0) break;
		essais = 0;
		total += r;
	}
	if (total < TAILLE_TAMPON) memset(buf + total, 0, TAILLE_TAMPON - total);
	DCFlushRange(buf, TAILLE_TAMPON);
	return total;
}

/* La musique tourne dans sa propre tâche : elle décode à l'avance pendant que l'écran
   se dessine, et la puce sonore la réveille (rappel) dès qu'un tampon a été joué. */
static lwp_t tache_musique = LWP_THREAD_NULL;
static lwpq_t file_musique;
static volatile int musique_stop = 0;
static u8 pile_musique[64 * 1024] ATTRIBUTE_ALIGN(32);

static void rappel_son(s32 voix) { (void)voix; LWP_ThreadSignal(file_musique); }

static void *boucle_musique(void *arg)
{
	(void)arg;
	while (!musique_stop) {
		int ajoutes = 0;
		/* au plus 2 tampons par réveil : impossible de tourner en rond */
		while (ajoutes < 2 && !musique_stop && ASND_TestVoiceBufferReady(0) == 1) {
			u8 *b = tampons[tampon_suivant];
			remplir(b);
			if (ASND_AddVoice(0, b, TAILLE_TAMPON) != SND_OK) break;
			tampon_suivant = (tampon_suivant + 1) % NB_TAMPONS;
			ajoutes++;
		}
		if (ASND_StatusVoice(0) == SND_UNUSED && !musique_stop) {   /* la voix s'est arrêtée : on relance */
			remplir(tampons[tampon_suivant]);
			ASND_SetVoice(0, VOICE_STEREO_16BIT, 44100, 0, tampons[tampon_suivant], TAILLE_TAMPON, 110, 110, rappel_son);
			tampon_suivant = (tampon_suivant + 1) % NB_TAMPONS;
		}
		LWP_ThreadSleep(file_musique);
	}
	return NULL;
}

static void musique_demarrer(void)
{
	ov_callbacks cb = { m_lire, m_aller, m_fermer, m_ou };
	mf.d = musique_ogg; mf.n = musique_ogg_size; mf.pos = 0;
	if (ov_open_callbacks(&mf, &vf, NULL, 0, cb) < 0) return;
	ASND_Init();
	ASND_Pause(0);
	LWP_InitQueue(&file_musique);
	remplir(tampons[0]);
	remplir(tampons[1]);
	ASND_SetVoice(0, VOICE_STEREO_16BIT, 44100, 0, tampons[0], TAILLE_TAMPON, 110, 110, rappel_son);
	ASND_AddVoice(0, tampons[1], TAILLE_TAMPON);
	tampon_suivant = 2;
	musique_stop = 0;
	if (LWP_CreateThread(&tache_musique, boucle_musique, NULL, pile_musique, sizeof pile_musique, 80) < 0) {
		ASND_StopVoice(0); ASND_End(); ov_clear(&vf);
		return;
	}
	musique_ok = 1;
}

/* Filet de sécurité appelé par l'écran : réveille la tâche musique ~6 fois par seconde. */
static void musique_entretenir(void)
{
	static int compteur = 0;
	if (musique_ok && ++compteur >= 10) { compteur = 0; LWP_ThreadSignal(file_musique); }
}

static void musique_arreter(void)
{
	if (!musique_ok) return;
	musique_stop = 1;
	LWP_ThreadSignal(file_musique);
	LWP_JoinThread(tache_musique, NULL);
	LWP_CloseQueue(file_musique);
	ASND_StopVoice(0);
	ASND_End();
	ov_clear(&vf);
	musique_ok = 0;
}

/* ------------------------------------------------------------------ fichiers */
static int existe(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void lire_balise(const char *fichier, const char *balise, char *out, int max)
{
	out[0] = 0;
	FILE *f = fopen(fichier, "rb");
	if (!f) return;
	char buf[2048];
	int n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n > 0 ? n : 0] = 0;
	char ouv[32], fer[32];
	snprintf(ouv, sizeof ouv, "<%s>", balise);
	snprintf(fer, sizeof fer, "</%s>", balise);
	char *a = strstr(buf, ouv);
	if (!a) return;
	a += strlen(ouv);
	char *b = strstr(a, fer);
	if (!b) return;
	while (a < b && (*a == ' ' || *a == '\t' || *a == '\n' || *a == '\r')) a++;
	while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\n' || b[-1] == '\r')) b--;
	int l = b - a < max - 1 ? b - a : max - 1;
	memcpy(out, a, l);
	out[l] = 0;
}

static int est_retro(const char *dossier)
{
	const char *r[] = { "snes9xgx", "fceugx", "vbagx", "genplus-gx", "genplus", "not64", "wii64", "pcsxgx", NULL };
	for (int i = 0; r[i]; i++) if (!strcasecmp(dossier, r[i])) return 1;
	return 0;
}

static void scanner_applis(void)
{
	DIR *d = opendir("sd:/apps");
	if (!d) return;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.' || !strcasecmp(ent->d_name, "wiil")) continue;
		char base[256], boot[256];
		snprintf(base, sizeof base, "sd:/apps/%s", ent->d_name);
		snprintf(boot, sizeof boot, "%s/boot.dol", base);
		if (!existe(boot)) { snprintf(boot, sizeof boot, "%s/boot.elf", base); if (!existe(boot)) continue; }
		Liste *l = est_retro(ent->d_name) ? &retro : &applis;
		if (l->n >= MAX_ELEMENTS) continue;
		Element *e = &l->e[l->n++];
		memset(e, 0, sizeof *e);
		e->demo = -1;
		strcpy(e->chemin, boot);
		char meta[256];
		snprintf(meta, sizeof meta, "%s/meta.xml", base);
		lire_balise(meta, "name", e->nom, sizeof e->nom);
		if (!e->nom[0]) snprintf(e->nom, sizeof e->nom, "%s", ent->d_name);
		snprintf(e->image, sizeof e->image, "%s/icon.png", base);
	}
	closedir(d);
}

static int deja_jeu(const char *id)
{
	for (int i = 0; i < jeux.n; i++) if (!strcmp(jeux.e[i].id, id)) return 1;
	return 0;
}

static void scanner_jaquettes(const char *dossier)
{
	DIR *d = opendir(dossier);
	if (!d) return;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && jeux.n < MAX_ELEMENTS) {
		const char *n = ent->d_name;
		int l = strlen(n);
		if (l != 10 || strcasecmp(n + 6, ".png")) continue;   /* ID6.png */
		char id[8];
		memcpy(id, n, 6); id[6] = 0;
		if (deja_jeu(id)) continue;
		Element *e = &jeux.e[jeux.n++];
		memset(e, 0, sizeof *e);
		e->demo = -1;
		strcpy(e->id, id);
		strcpy(e->nom, id);
		snprintf(e->chemin, sizeof e->chemin, "%s/%s", dossier, n);
		e->gamecube = (id[0] == 'G' || id[0] == 'D');
	}
	closedir(d);
}

/* Titres : cache sd:/apps/wiil/titres.txt, sinon lecture de wiitdb.xml (une seule fois) */
static Element *jeu_par_id(const char *id)
{
	for (int i = 0; i < jeux.n; i++) if (!strncmp(jeux.e[i].id, id, 6)) return &jeux.e[i];
	return NULL;
}

static void ecran_message(const char *m);

/* Genre de wiitdb (anglais, ex. « racing,party ») -> un mot en français */
static void genre_fr(const char *en, char *out, int max)
{
	static const char *t[][2] = {
		{ "kart racing", "Course" }, { "racing", "Course" }, { "sports", "Sport" }, { "football", "Football" },
		{ "soccer", "Football" }, { "golf", "Golf" }, { "basketball", "Basket" }, { "baseball", "Baseball" },
		{ "tennis", "Tennis" }, { "music", "Musique" }, { "rhythm", "Rythme" }, { "dance", "Danse" },
		{ "karaoke", "Karaoké" }, { "party", "Party game" }, { "platformer", "Plateforme" },
		{ "3d platformer", "Plateforme" }, { "action", "Action" }, { "adventure", "Aventure" },
		{ "puzzle", "Réflexion" }, { "shooter", "Tir" }, { "first-person shooter", "Tir" },
		{ "third-person shooter", "Tir" }, { "fighting", "Combat" }, { "beat 'em up", "Combat" },
		{ "simulation", "Simulation" }, { "role-playing", "RPG" }, { "strategy", "Stratégie" },
		{ "arcade", "Arcade" }, { "fitness", "Fitness" }, { "health", "Santé" }, { "educational", "Éducatif" },
		{ "board game", "Jeu de société" }, { "trivia", "Quiz" }, { "survival horror", "Horreur" },
		{ "virtual pet", "Animaux" }, { "cards", "Cartes" }, { "hunting", "Chasse" }, { NULL, NULL } };
	char premier[40];
	int n = 0;
	while (en[n] && en[n] != ',' && n < 39) { premier[n] = en[n]; n++; }
	premier[n] = 0;
	out[0] = 0;
	for (int i = 0; t[i][0]; i++) if (!strcmp(premier, t[i][0])) { snprintf(out, max, "%s", t[i][1]); return; }
	if (premier[0]) { snprintf(out, max, "%s", premier); if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32; }
}

/* Titres et fiches : cache sd:/apps/wiil/titres.txt (ID, titre, année, joueurs, genre, éditeur
   séparés par des tabulations), sinon lecture de wiitdb.xml (une seule fois). */
static void charger_titres(void)
{
	if (jeux.n == 0) return;      /* pas de jeux : rien à chercher dans wiitdb */
	char cache[128];
	snprintf(cache, sizeof cache, "%s/titres.txt", DOSSIER_WIIL);
	FILE *f = fopen(cache, "rb");
	int manquants = 0, fiches_manquantes = 0;
	if (f) {
		char l[384];
		while (fgets(l, sizeof l, f)) {
			char *champ[6] = { l, NULL, NULL, NULL, NULL, NULL };
			int nb = 1;
			char *fin = strpbrk(l, "\r\n");
			if (fin) *fin = 0;
			for (char *p = l; *p && nb < 6; p++) if (*p == '\t') { *p = 0; champ[nb++] = p + 1; }
			if (nb < 2) continue;
			Element *e = jeu_par_id(champ[0]);
			if (!e) continue;
			snprintf(e->nom, sizeof e->nom, "%s", champ[1]);
			if (nb == 6) {
				snprintf(e->annee, sizeof e->annee, "%s", champ[2]);
				snprintf(e->joueurs, sizeof e->joueurs, "%s", champ[3]);
				snprintf(e->genre, sizeof e->genre, "%s", champ[4]);
				snprintf(e->editeur, sizeof e->editeur, "%s", champ[5]);
			} else fiches_manquantes = 1;     /* ancien cache, sans les fiches */
		}
		fclose(f);
	}
	for (int i = 0; i < jeux.n; i++) if (!strcmp(jeux.e[i].nom, jeux.e[i].id)) manquants++;
	if (!manquants && !fiches_manquantes && f) return;

	const char *xml[] = { "sd:/wiiflow/settings/wiitdb.xml", "sd:/wiiflow/wiitdb.xml",
	                      "sd:/apps/usbloader_gx/wiitdb.xml", "sd:/wiitdb.xml", NULL };
	FILE *x = NULL;
	for (int i = 0; xml[i] && !x; i++) x = fopen(xml[i], "rb");
	if (!x) return;
	ecran_message("Préparation des fiches de tes jeux…");
	static char l[1024];
	char id[8] = "", titre_fr[128] = "", titre_en[128] = "", lang[8] = "";
	char annee[8] = "", joueurs[16] = "", genre[32] = "", editeur[48] = "";
	while (fgets(l, sizeof l, x)) {
		char *p, *q;
		if ((p = strstr(l, "<id>"))) {
			memcpy(id, p + 4, 6); id[6] = 0;
			titre_fr[0] = titre_en[0] = annee[0] = joueurs[0] = genre[0] = editeur[0] = 0;
		}
		if ((p = strstr(l, "<locale lang=\""))) { memcpy(lang, p + 14, 2); lang[2] = 0; }
		if ((p = strstr(l, "<title>")) && (q = strstr(p, "</title>"))) {
			*q = 0;
			if (!strcmp(lang, "FR")) snprintf(titre_fr, sizeof titre_fr, "%s", p + 7);
			else if (!strcmp(lang, "EN")) snprintf(titre_en, sizeof titre_en, "%s", p + 7);
		}
		if ((p = strstr(l, "<date year=\""))) { int a = atoi(p + 12); if (a > 1980) snprintf(annee, sizeof annee, "%d", a); }
		if ((p = strstr(l, "<input players=\""))) {
			int j = atoi(p + 16);
			if (j > 1) snprintf(joueurs, sizeof joueurs, "1-%d joueurs", j);
			else if (j == 1) snprintf(joueurs, sizeof joueurs, "1 joueur");
		}
		if ((p = strstr(l, "<genre>")) && (q = strstr(p, "</genre>"))) { *q = 0; genre_fr(p + 7, genre, sizeof genre); }
		if ((p = strstr(l, "<publisher>")) && (q = strstr(p, "</publisher>"))) { *q = 0; snprintf(editeur, sizeof editeur, "%s", p + 11); }
		if (strstr(l, "</game>") && id[0]) {
			Element *e = jeu_par_id(id);
			if (e) {
				if (!strcmp(e->nom, e->id)) {
					if (titre_fr[0]) snprintf(e->nom, sizeof e->nom, "%s", titre_fr);
					else if (titre_en[0]) snprintf(e->nom, sizeof e->nom, "%s", titre_en);
				}
				snprintf(e->annee, sizeof e->annee, "%s", annee);
				snprintf(e->joueurs, sizeof e->joueurs, "%s", joueurs);
				snprintf(e->genre, sizeof e->genre, "%s", genre);
				snprintf(e->editeur, sizeof e->editeur, "%s", editeur);
			}
			id[0] = 0;
		}
	}
	fclose(x);
	mkdir(DOSSIER_WIIL, 0777);
	f = fopen(cache, "wb");
	if (f) {
		for (int i = 0; i < jeux.n; i++) {
			Element *e = &jeux.e[i];
			fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\n", e->id, e->nom, e->annee, e->joueurs, e->genre, e->editeur);
		}
		fclose(f);
	}
}

static int cmp_nom(const void *a, const void *b)
{
	const Element *x = a, *y = b;
	if (x->favori != y->favori) return y->favori - x->favori;   /* favoris en premier */
	return strcasecmp(x->nom, y->nom);
}

/* Favoris : sd:/apps/wiil/favoris.txt, une ligne par favori :
   « J RMCP01 » pour un jeu (son ID), « A sd:/apps/snes9xgx/boot.dol » pour une appli. */
static const char *cle_favori(Liste *l, Element *e) { return l == &jeux ? e->id : e->chemin; }

static void charger_favoris(void)
{
	FILE *f = fopen(DOSSIER_WIIL "/favoris.txt", "rb");
	if (!f) return;
	char ligne[300];
	while (fgets(ligne, sizeof ligne, f)) {
		ligne[strcspn(ligne, "\r\n")] = 0;
		if (strlen(ligne) < 3 || ligne[1] != ' ') continue;
		for (int o = 0; o < 3; o++) {
			Liste *l = o == 0 ? &jeux : o == 1 ? &applis : &retro;
			if ((ligne[0] == 'J') != (l == &jeux)) continue;
			for (int i = 0; i < l->n; i++)
				if (!strcmp(cle_favori(l, &l->e[i]), ligne + 2)) l->e[i].favori = 1;
		}
	}
	fclose(f);
}

static void enregistrer_favoris(void)
{
	mkdir(DOSSIER_WIIL, 0777);
	FILE *f = fopen(DOSSIER_WIIL "/favoris.txt", "wb");
	if (!f) return;
	Liste *ls[3] = { &jeux, &applis, &retro };
	for (int o = 0; o < 3; o++)
		for (int i = 0; i < ls[o]->n; i++)
			if (ls[o]->e[i].favori && ls[o]->e[i].demo < 0)
				fprintf(f, "%c %s\n", o == 0 ? 'J' : 'A', cle_favori(ls[o], &ls[o]->e[i]));
	fclose(f);
}

static void ajouter_demo(void)
{
	const char *noms[6] = { "Aventure Stellaire", "Course Éclair", "Forêt Émeraude", "Sport Soleil", "Océan Bleu", "Fête Néon" };
	for (int i = 0; i < 6; i++) {
		Element *e = &jeux.e[jeux.n++];
		memset(e, 0, sizeof *e);
		snprintf(e->id, sizeof e->id, "DEMO0%d", i);
		snprintf(e->nom, sizeof e->nom, "%s", noms[i]);
		e->demo = i;
	}
}

/* ------------------------------------------------------------------ textures à la demande */
static GRRLIB_texImg *tex_element(Liste *l, int i)
{
	Element *e = &l->e[i];
	if (e->demo >= 0) return t_demo[e->demo];
	if (!e->tex) {
		const char *p = l == &jeux ? e->chemin : e->image;
		if (p[0] && existe(p)) e->tex = GRRLIB_LoadTextureFromFile(p);
		if (!e->tex) e->tex = t_tuile;     /* évite de réessayer en boucle */
	}
	return e->tex;
}

static void liberer_loin(Liste *l, int centre)
{
	for (int i = 0; i < l->n; i++) {
		Element *e = &l->e[i];
		if (e->tex && e->tex != t_tuile && e->demo < 0 && abs(i - centre) > (e->tex->w > 512 ? 4 : 14)) {   /* grandes boîtes : on en garde peu en mémoire */
			GRRLIB_FreeTexture(e->tex);
			e->tex = NULL;
		}
	}
}

/* Fond flou : petite texture 64x48 moyennée, affichée agrandie (filtrage bilinéaire = flou doux) */
static GRRLIB_texImg *fond_flou(GRRLIB_texImg *src)
{
	GRRLIB_texImg *t = GRRLIB_CreateEmptyTexture(64, 48);
	if (!t || !src || src->w < 4 || src->h < 4) return t;
	f32 fx, fw;
	zone_face(src, &fx, &fw);
	int x0 = (int)fx, w = (int)fw, h = src->h;
	int ch = (int)(w * 0.75f);
	if (ch > h) ch = h;
	int y0 = (h - ch) / 2;
	for (int y = 0; y < 48; y++)
		for (int x = 0; x < 64; x++) {
			u32 r = 0, g = 0, b = 0;
			for (int k = 0; k < 4; k++) {
				int sx = x0 + (x * w) / 64 + (k & 1) * (w / 128);
				if (sx >= (int)src->w) sx = src->w - 1;
				int sy = y0 + (y * ch) / 48 + (k >> 1) * (ch / 96);
				if (sy >= h) sy = h - 1;
				u32 c = GRRLIB_GetPixelFromtexImg(sx, sy, src);
				r += (c >> 24) & 255; g += (c >> 16) & 255; b += (c >> 8) & 255;
			}
			GRRLIB_SetPixelTotexImg(x, y, t, ((r / 4) << 24) | ((g / 4) << 16) | ((b / 4) << 8) | 0xFF);
		}
	GRRLIB_FlushTex(t);
	return t;
}

/* Réduit la face avant d'une jaquette à une petite texture (hauteur hmax) :
   préparée une fois au démarrage, elle se dessine ensuite instantanément. */
static GRRLIB_texImg *reduire_face(GRRLIB_texImg *src, int hmax)
{
	f32 fx, fw;
	zone_face(src, &fx, &fw);
	int x0 = (int)fx, w = (int)fw, h = src->h;
	int th = (h < hmax ? h : hmax) & ~3;
	int tw = ((int)(w * (f32)th / h) + 3) & ~3;
	if (th < 4 || tw < 4) return NULL;
	GRRLIB_texImg *t = GRRLIB_CreateEmptyTexture(tw, th);
	if (!t) return NULL;
	for (int y = 0; y < th; y++)
		for (int x = 0; x < tw; x++) {
			u32 r = 0, g = 0, b = 0, a = 0;
			for (int k = 0; k < 4; k++) {   /* moyenne de 4 points dans la zone d'origine */
				int sx = x0 + (int)(((x + 0.25f + 0.5f * (k & 1)) * w) / tw);
				int sy = (int)(((y + 0.25f + 0.5f * (k >> 1)) * h) / th);
				if (sx >= (int)src->w) sx = src->w - 1;
				if (sy >= h) sy = h - 1;
				u32 c = GRRLIB_GetPixelFromtexImg(sx, sy, src);
				r += c >> 24; g += (c >> 16) & 255; b += (c >> 8) & 255; a += c & 255;
			}
			GRRLIB_SetPixelTotexImg(x, y, t, ((r / 4) << 24) | ((g / 4) << 16) | ((b / 4) << 8) | (a / 4));
		}
	GRRLIB_FlushTex(t);
	return t;
}

static void ecran_progression(int i, int n);

static void preparer_jaquettes(void)
{
	int hmax = jeux.n <= 80 ? 300 : 200;   /* beaucoup de jeux : un peu plus petit pour la mémoire */
	for (int i = 0; i < jeux.n; i++) {
		Element *e = &jeux.e[i];
		if (e->demo >= 0) { e->flou = fond_flou(t_demo[e->demo]); continue; }
		ecran_progression(i, jeux.n);
		GRRLIB_texImg *grand = existe(e->chemin) ? GRRLIB_LoadTextureFromFile(e->chemin) : NULL;
		if (grand) {
			e->tex = reduire_face(grand, hmax);
			GRRLIB_FreeTexture(grand);
		}
		if (!e->tex) e->tex = t_tuile;
		else e->flou = fond_flou(e->tex);
	}
}

/* ------------------------------------------------------------------ lancement */
static char message[128];
static int message_temps = 0;

static void avertir(const char *m) { snprintf(message, sizeof message, "%s", m); message_temps = 150; }

/* ------------------------------------------------------------------ télécommande DSi (réseau) */
/* Une tâche à part démarre le Wi-Fi de la Wii (quelques secondes, sans figer le menu),
   écoute le port UDP 4242 et répond à chaque message. L'écran récupère les messages
   dans une petite boîte aux lettres et les affiche dans la bulle du bas. */
#include <network.h>
#include <errno.h>
#include <unistd.h>
#define PORT_DSI 4242

static lwp_t tache_reseau = LWP_THREAD_NULL;
static u8 pile_reseau[32 * 1024] ATTRIBUTE_ALIGN(32);
static volatile int reseau_stop = 0, reseau_etat = 0;   /* 0 démarrage, 1 prêt, -1 indisponible */
static s32 sock_dsi = -1;
static char boite[8][96];                    /* messages reçus, pas encore affichés */
static volatile int boite_ecrit = 0, boite_lu = 0;
static char ip_wii[16];
static volatile s32 reseau_erreur = 0;
static const char *volatile reseau_etape = "";

/* Touches reçues de la DSi (codes 0..8, voir noms[] plus bas), lues par l'écran */
static volatile u8 touches[32];
static volatile int touches_ecrit = 0, touches_lu = 0;
static void touches_ajouter(int t) { touches[touches_ecrit % 32] = t; touches_ecrit++; }

/* Ordres de la DSi pour l'écran : choisir un élément (écran tactile) */
static volatile int choix_o = 0, choix_i = 0, choix_nouveau = 0;
static volatile int fav_o = 0, fav_i = 0, fav_nouveau = 0;
static volatile int itm_o = 0, itm_i = 0, itm_a_envoyer = 0;

/* Ordres numérotés de la DSi : on garde les 32 derniers numéros pour ne jamais
   exécuter deux fois un ordre renvoyé (quand l'accusé de réception s'est perdu). */
static int ordres_vus[32], ordres_n = 0;
static int deja_vu(int id)
{
	if (id <= 0) return 0;
	for (int k = 0; k < 32 && k < ordres_n; k++) if (ordres_vus[k] == id) return 1;
	ordres_vus[ordres_n % 32] = id;
	ordres_n++;
	return 0;
}

/* Liste en cours d'envoi (un nom à la fois, entre deux messages reçus) */
static int liste_o = -1, liste_i = 0;

/* Images demandées par la DSi : préparées par l'écran (lui seul lit les textures),
   envoyées par la tâche réseau. Couleurs au format de la DSi (RGB 5 bits + bit 15). */
typedef struct { u8 type, o; u16 i; } DemandeImg;           /* type 1 = jaquette, 2 = vignette */
static DemandeImg dem_img[16];
static volatile int dem_ecrit = 0, dem_lu = 0;
#define JAQ_L 96
#define JAQ_H 132
#define MINI_C 20
static u16 jaq_pix[JAQ_L * JAQ_H];
static volatile int jaq_o = -1, jaq_i = -1, jaq_w = 0, jaq_h = 0, jaq_a_envoyer = 0, jaq_occupe = 0;
static u16 mini_pix[8][MINI_C * MINI_C];
static int mini_o[8], mini_i[8];
static volatile int mini_ecrit = 0, mini_lu = 0;

static void demande_image(int type, int o, int i)
{
	if (dem_ecrit - dem_lu >= 16) return;   /* file pleine : la DSi redemandera */
	DemandeImg *d = &dem_img[dem_ecrit % 16];
	d->type = type; d->o = o; d->i = i;
	dem_ecrit++;
}

/* Ce que l'écran veut faire savoir à la DSi (la sélection), et à qui l'envoyer */
static char statut_dsi[160];
static volatile int statut_a_envoyer = 0, dsi_connue = 0;
static struct sockaddr_in adr_dsi;

/* La police de la DSi ne connaît pas les accents : « Rétro » -> « Retro ». */
static void sans_accents(char *dst, const char *src, int max)
{
	int n = 0;
	const unsigned char *p = (const unsigned char *)src;
	while (*p && n < max - 1) {
		if (*p == 0xC3 && p[1]) {
			unsigned char c = p[1];
			char r = '?';
			if (c >= 0x80 && c <= 0x85) r = 'A'; else if (c == 0x87) r = 'C';
			else if (c >= 0x88 && c <= 0x8B) r = 'E'; else if (c >= 0x8C && c <= 0x8F) r = 'I';
			else if (c >= 0x92 && c <= 0x96) r = 'O'; else if (c >= 0x99 && c <= 0x9C) r = 'U';
			else if (c >= 0xA0 && c <= 0xA5) r = 'a'; else if (c == 0xA7) r = 'c';
			else if (c >= 0xA8 && c <= 0xAB) r = 'e'; else if (c >= 0xAC && c <= 0xAF) r = 'i';
			else if (c >= 0xB2 && c <= 0xB6) r = 'o'; else if (c >= 0xB9 && c <= 0xBC) r = 'u';
			dst[n++] = r; p += 2;
		} else if (*p < 0x80) dst[n++] = *p++;
		else { p++; while ((*p & 0xC0) == 0x80) p++; dst[n++] = '?'; }   /* autre caractère spécial */
	}
	dst[n] = 0;
}

static void boite_poster(const char *m)
{
	int i = boite_ecrit % 8;
	snprintf(boite[i], sizeof boite[i], "%s", m);
	boite_ecrit++;
}

/* Envoie la jaquette préparée : tous les morceaux (seul = -1) ou un seul morceau perdu. */
#define MORCEAU 1000
static void envoyer_jaquette(int seul)
{
	jaq_occupe = 1;          /* l'écran ne réécrit pas l'image pendant l'envoi */
	static u8 paquet[64 + MORCEAU];
	int total = jaq_w * jaq_h * 2;
	int nk = (total + MORCEAU - 1) / MORCEAU;
	for (int k = 0; k < nk && !reseau_stop; k++) {
		if (seul >= 0 ? k != seul : k != 0) continue;   /* -1 = premier morceau seulement */
		int h = snprintf((char *)paquet, 64, "IMG %d|%d|%d|%d|%d|%d\n", jaq_o, jaq_i, jaq_w, jaq_h, k, nk);
		int deb = k * MORCEAU, lg = total - deb < MORCEAU ? total - deb : MORCEAU;
		for (int j = 0; j < lg; j += 2) {
			u16 c = jaq_pix[(deb + j) / 2];
			paquet[h + j] = c & 255; paquet[h + j + 1] = c >> 8;      /* petit-boutiste, comme la DSi */
		}
		net_sendto(sock_dsi, paquet, h + lg, 0, (struct sockaddr *)&adr_dsi, 8);
	}
	jaq_occupe = 0;
}

static void *boucle_reseau(void *arg)
{
	(void)arg;
	s32 r = -1;
	/* on laisse d'abord le menu s'afficher tranquillement (3 s) */
	for (int i = 0; i < 30 && !reseau_stop; i++) usleep(100000);
	/* La Wii met souvent 5 à 15 s à se connecter : net_init répond « réessaie »
	   (-EAGAIN) tant que ce n'est pas prêt. On patiente jusqu'à 40 s. */
	for (int essai = 0; essai < 200 && !reseau_stop; essai++) {
		r = net_init();
		if (r != -EAGAIN) break;
		usleep(200000);
	}
	if (r < 0 || reseau_stop) { reseau_erreur = r; reseau_etape = "connexion"; reseau_etat = -1; return NULL; }
	u32 ip = net_gethostip();
	snprintf(ip_wii, sizeof ip_wii, "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);

	sock_dsi = net_socket(AF_INET, SOCK_DGRAM, 0);   /* la Wii refuse IPPROTO_UDP (erreur 123) : 0 = choix automatique */
	struct sockaddr_in adr;
	memset(&adr, 0, sizeof adr);
	adr.sin_len = 8;
	adr.sin_family = AF_INET;
	adr.sin_port = htons(PORT_DSI);
	adr.sin_addr.s_addr = INADDR_ANY;
	if (sock_dsi < 0) { reseau_erreur = sock_dsi; reseau_etape = "socket"; reseau_etat = -1; return NULL; }
	r = net_bind(sock_dsi, (struct sockaddr *)&adr, sizeof adr);
	if (r < 0) { reseau_erreur = r; reseau_etape = "port 4242"; reseau_etat = -1; return NULL; }
	reseau_etat = 1;

	while (!reseau_stop) {
		/* --- envois en attente --- */
		if (statut_a_envoyer && dsi_connue) {        /* la sélection a changé */
			statut_a_envoyer = 0;
			char st[180];
			snprintf(st, sizeof st, "SEL %s", statut_dsi);
			net_sendto(sock_dsi, st, strlen(st), 0, (struct sockaddr *)&adr_dsi, 8);
		}
		if (itm_a_envoyer && dsi_connue) {           /* un favori a changé */
			itm_a_envoyer = 0;
			char it[200];
			Element *e = &onglets[itm_o]->e[itm_i];
			snprintf(it, sizeof it, "ITM %d|%d|%d|%s", itm_o, itm_i, e->favori, e->nom);
			net_sendto(sock_dsi, it, strlen(it), 0, (struct sockaddr *)&adr_dsi, 8);
		}
		if (jaq_a_envoyer && dsi_connue) {           /* une jaquette : en morceaux de 1000 octets */
			envoyer_jaquette(-1);
			jaq_a_envoyer = 0;
		}
		/* envois progressifs : au plus un nom de liste et une vignette par tour,
		   pour que la DSi ne soit pas débordée et que les touches passent avant */
		static u64 dernier_envoi = 0;
		int en_attente = (liste_o >= 0) || (mini_lu != mini_ecrit);
		if (en_attente && dsi_connue && diff_msec(dernier_envoi, gettime()) >= 6) {
			dernier_envoi = gettime();
			if (liste_o >= 0) {
				Liste *ls = onglets[liste_o];
				if (liste_i < ls->n) {
					char it[200];
					snprintf(it, sizeof it, "ITM %d|%d|%d|%s", liste_o, liste_i, ls->e[liste_i].favori, ls->e[liste_i].nom);
					net_sendto(sock_dsi, it, strlen(it), 0, (struct sockaddr *)&adr_dsi, 8);
					liste_i++;
				}
				if (liste_i >= ls->n) liste_o = -1;
			} else {
				int k = mini_lu % 8;
				static u8 paquet[64 + MINI_C * MINI_C * 2];
				int h = snprintf((char *)paquet, 64, "MIN %d|%d|%d|%d\n", mini_o[k], mini_i[k], MINI_C, MINI_C);
				for (int j = 0; j < MINI_C * MINI_C; j++) { paquet[h + 2 * j] = mini_pix[k][j] & 255; paquet[h + 2 * j + 1] = mini_pix[k][j] >> 8; }
				net_sendto(sock_dsi, paquet, h + MINI_C * MINI_C * 2, 0, (struct sockaddr *)&adr_dsi, 8);
				mini_lu++;
			}
		}

		/* --- réception --- */
		struct pollsd p = { sock_dsi, 0x0001 /* POLLIN */, 0 };
		s32 pr = net_poll(&p, 1, en_attente ? 3 : 20);   /* courte attente s'il reste des choses à envoyer */
		if (pr < 0) { usleep(100000); continue; }      /* erreur : on fait une pause, jamais de boucle folle */
		if (pr == 0 || !(p.revents & 0x0001)) continue; /* rien reçu (le poll a déjà attendu 20 ms) */
		char msg[96];
		struct sockaddr_in de;
		socklen_t lg_de = sizeof de;   /* ne pas mettre 8 ici : la réception ne marche plus */
		s32 n = net_recvfrom(sock_dsi, msg, sizeof msg - 1, 0, (struct sockaddr *)&de, &lg_de);
		if (n <= 0) {
			static int deja = 0;     /* on signale une seule fois l'erreur de lecture */
			if (!deja) { deja = 1; char err[96]; snprintf(err, sizeof err, "Lecture du message DSi impossible (code %d)", (int)n); boite_poster(err); }
			usleep(20000);
			continue;
		}
		msg[n] = 0;
		/* Sur Wii, l'adresse doit être complète, longueur (sin_len = 8) comprise :
		   on la reconstruit à partir de celle de l'expéditeur. */
		struct sockaddr_in vers;
		memset(&vers, 0, sizeof vers);
		vers.sin_len = 8;
		vers.sin_family = AF_INET;
		vers.sin_port = de.sin_port;
		vers.sin_addr.s_addr = de.sin_addr.s_addr;
		if (!dsi_connue || adr_dsi.sin_addr.s_addr != vers.sin_addr.s_addr) {
			boite_poster("Télécommande DSi connectée");
			statut_a_envoyer = 1;          /* on lui dit tout de suite ce qui est sélectionné */
		}
		adr_dsi = vers;
		dsi_connue = 1;

		int o = 0, i = 0, k = 0;
		char rep[200];
		rep[0] = 0;
		if (!strncmp(msg, "TOUCHE ", 7)) {           /* une touche de la télécommande */
			char nom[16] = "";
			int id = 0;
			sscanf(msg + 7, "%15s %d", nom, &id);
			if (id > 0) { snprintf(rep, sizeof rep, "ACK %d", id); net_sendto(sock_dsi, rep, strlen(rep), 0, (struct sockaddr *)&vers, 8); rep[0] = 0; }
			if (!deja_vu(id)) {
				const char *noms[] = { "GAUCHE", "DROITE", "HAUT", "BAS", "A", "B", "PLUS", "MOINS", "HOME", "JOUER", NULL };
				for (int t = 0; noms[t]; t++)
					if (!strcmp(nom, noms[t])) { touches_ajouter(t); break; }
			}
		}
		else if (!strcmp(msg, "BONJOUR")) snprintf(rep, sizeof rep, "WIIL");
		else if (sscanf(msg, "LISTE %d", &o) == 1 && o >= 0 && o < 3) {
			snprintf(rep, sizeof rep, "LST %d|%d", o, onglets[o]->n);
			liste_o = o; liste_i = 0;                 /* les noms partiront un par un */
		}
		else if (sscanf(msg, "ITMP %d %d", &o, &i) == 2 && o >= 0 && o < 3 && i >= 0 && i < onglets[o]->n)
			snprintf(rep, sizeof rep, "ITM %d|%d|%d|%s", o, i, onglets[o]->e[i].favori, onglets[o]->e[i].nom);
		else if (sscanf(msg, "CHOISIR %d %d", &o, &i) == 2 && o >= 0 && o < 3) {
			int id = 0;
			sscanf(msg, "CHOISIR %d %d %d", &o, &i, &id);
			if (id > 0) snprintf(rep, sizeof rep, "ACK %d", id);
			if (!deja_vu(id)) { choix_o = o; choix_i = i; choix_nouveau = 1; }
		}
		else if (sscanf(msg, "INFO %d %d", &o, &i) == 2 && o >= 0 && o < 3 && i >= 0 && i < onglets[o]->n) {
			Element *e = &onglets[o]->e[i];
			const char *type = o == 0 ? (e->demo >= 0 ? "Démo" : e->gamecube ? "GameCube" : "Wii") : o == 2 ? "Émulateur" : "Application";
			snprintf(rep, sizeof rep, "INF %d|%d|%s|%s|%s|%s|%s|%s", o, i, type, o == 0 ? e->id : "",
			         e->annee, e->joueurs, e->genre, e->editeur);
		}
		else if (sscanf(msg, "IMGP %d %d %d", &o, &i, &k) == 3) {   /* un morceau perdu */
			if (o == jaq_o && i == jaq_i && !jaq_a_envoyer) envoyer_jaquette(k);
			else if (o >= 0 && o < 3 && i >= 0 && i < onglets[o]->n) demande_image(1, o, i);
		}
		else if (sscanf(msg, "IMG %d %d", &o, &i) == 2 && o >= 0 && o < 3 && i >= 0 && i < onglets[o]->n) {
			if (o == jaq_o && i == jaq_i && !jaq_a_envoyer) envoyer_jaquette(-1);   /* déjà prête */
			else demande_image(1, o, i);
		}
		else if (sscanf(msg, "FAV %d %d", &o, &i) == 2 && o >= 0 && o < 3 && i >= 0 && i < onglets[o]->n) {
			int id = 0;
			sscanf(msg, "FAV %d %d %d", &o, &i, &id);
			if (id > 0) snprintf(rep, sizeof rep, "ACK %d", id);
			if (!deja_vu(id)) { fav_o = o; fav_i = i; fav_nouveau = 1; }   /* l'écran écrit le fichier */
		}
		else if (sscanf(msg, "MINI %d %d", &o, &i) == 2 && o >= 0 && o < 3 && i >= 0 && i < onglets[o]->n)
			demande_image(2, o, i);
		else {                                        /* autre message : affiché, comme « bonjour » */
			char aff[96];
			snprintf(aff, sizeof aff, "DSi : %s", msg);
			boite_poster(aff);
			snprintf(rep, sizeof rep, "Wiil a recu : %s", msg);
		}
		if (rep[0]) {
			s32 env = net_sendto(sock_dsi, rep, strlen(rep), 0, (struct sockaddr *)&vers, 8);
			if (env < 0) {
				static int deja_env = 0;
				if (!deja_env) {
					deja_env = 1;
					char err[96];
					snprintf(err, sizeof err, "Réponse à la DSi impossible (code %d)", (int)env);
					boite_poster(err);
				}
			}
		}
	}
	return NULL;
}

static void reseau_demarrer(void)
{
	reseau_stop = 0;
	if (LWP_CreateThread(&tache_reseau, boucle_reseau, NULL, pile_reseau, sizeof pile_reseau, 40) < 0)
		tache_reseau = LWP_THREAD_NULL;
}

static void reseau_arreter(void)
{
	if (tache_reseau == LWP_THREAD_NULL) return;
	reseau_stop = 1;
	LWP_JoinThread(tache_reseau, NULL);   /* au plus ~0,1 s une fois le réseau prêt */
	tache_reseau = LWP_THREAD_NULL;
	if (sock_dsi >= 0) { net_close(sock_dsi); sock_dsi = -1; }
	if (reseau_etat == 1) net_deinit();
	reseau_etat = 0;
}

/* Appelé à chaque image : affiche les messages reçus et l'état du réseau. */
static void reseau_entretenir(void)
{
	static int annonce = 0, attente_annoncee = 0, images = 0;
	if (reseau_etat == 0 && !attente_annoncee && ++images == 60 * 4) {   /* après 4 s */
		attente_annoncee = 1;
		avertir("Télécommande DSi : connexion du Wi-Fi de la Wii…");
	}
	if (!annonce && reseau_etat != 0) {
		annonce = 1;
		char m[96];
		if (reseau_etat == 1) snprintf(m, sizeof m, "Télécommande DSi prête (%s)", ip_wii);
		else snprintf(m, sizeof m, "Télécommande DSi : réseau indisponible (%s, code %d)", reseau_etape, (int)reseau_erreur);
		avertir(m);
	}
	if (boite_lu != boite_ecrit && message_temps < 120) {   /* un message à la fois, lisible */
		avertir(boite[boite_lu % 8]);
		boite_lu++;
	}
}

static void tout_arreter(void)
{
	reseau_arreter();
	musique_arreter();
	GRRLIB_Exit();
	WPAD_Shutdown();
	if (sd_ok) fatUnmount("sd:");
}

static void lancer_fichier(const char *chemin, const char *arg)
{
	FILE *f = fopen(chemin, "rb");
	if (!f) { avertir("Fichier introuvable"); return; }
	fseek(f, 0, SEEK_END);
	long taille = ftell(f);
	fseek(f, 0, SEEK_SET);
	u32 haut = (u32)SYS_GetArena2Hi();
	u8 *image = (u8 *)((haut - taille - 64) & ~31u);
	char *args = (char *)(((u32)image - 1024) & ~31u);
	if ((u32)args <= (u32)SYS_GetArena2Lo() + 0x100000) { fclose(f); avertir("Pas assez de mémoire"); return; }
	SYS_SetArena2Hi(args);           /* plus personne n'alloue par-dessus */
	ecran_message("Lancement…");
	if (fread(image, 1, taille, f) != (size_t)taille) { fclose(f); avertir("Lecture impossible"); return; }
	fclose(f);
	/* arguments : chemin puis ID éventuel, séparés par des zéros */
	int n = snprintf(args, 512, "%s", chemin) + 1;
	if (arg && arg[0]) n += snprintf(args + n, 512 - n, "%s", arg) + 1;
	args[n++] = 0;
	DCFlushRange(image, taille);
	DCFlushRange(args, 1024);
	memcpy((void *)0x81700000, booter_bin, booter_bin_size);
	DCFlushRange((void *)0x81700000, booter_bin_size);
	ICInvalidateRange((void *)0x81700000, booter_bin_size);
	tout_arreter();
	/* Comme la Chaîne Homebrew : on redémarre l'IOS pour que l'appli lancée
	   retrouve des ports USB « neufs » (sinon USB Loader GX ne voit pas le disque). */
	IOS_ReloadIOS(58);
	SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
	u32 niveau = IRQ_Disable();
	(void)niveau;
	void (*booter)(const u8 *, const char *, u32) = (void (*)(const u8 *, const char *, u32))0x81700000;
	booter(image, args, n);
}

static void lancer_titre(u64 id)
{
	tout_arreter();
	WII_Initialize();
	WII_LaunchTitle(id);
}

/* ------------------------------------------------------------------ disque */
/* Lecture du disque par /dev/di (cIOS 249). Disque Wii : on fait tourner son
   « apploader » (le petit programme du disque qui charge le jeu en mémoire), puis on
   saute dans le jeu. Disque GameCube : on confie le disque au mode GameCube de la Wii.
   Pour que le jeu puisse se charger en bas de la mémoire, Wiil est placé plus haut
   (voir LDFLAGS dans le Makefile : 0x80B00000, comme USB Loader GX). */
static u32 di_in[8] ATTRIBUTE_ALIGN(32);
static u32 di_out[8] ATTRIBUTE_ALIGN(32);
static u32 di_tampon[8] ATTRIBUTE_ALIGN(32);
static u32 di_err[8] ATTRIBUTE_ALIGN(32);
static u8 di_tmd[0x4A00] ATTRIBUTE_ALIGN(32);
static ioctlv di_vec[5] ATTRIBUTE_ALIGN(32);
static s32 di_fd = -1;

static s32 di_cmd(u32 cmd, u32 a, u32 b, void *out, u32 outlen)
{
	memset(di_in, 0, sizeof di_in);
	di_in[0] = cmd << 24; di_in[1] = a; di_in[2] = b;
	if (!out) { out = di_out; outlen = sizeof di_out; }
	return IOS_Ioctl(di_fd, cmd, di_in, sizeof di_in, out, outlen);
}
static s32 di_lire(void *dst, u32 len, u64 pos) { return di_cmd(0x71, len, (u32)(pos >> 2), dst, len); }        /* dans la partition */
static s32 di_lire_brut(void *dst, u32 len, u64 pos) { return di_cmd(0x8D, len, (u32)(pos >> 2), dst, len); }   /* disque brut */

static s32 di_ouvrir_partition(u32 pos_div4)
{
	memset(di_in, 0, sizeof di_in);
	di_in[0] = 0x8B << 24; di_in[1] = pos_div4;
	di_vec[0].data = di_in;  di_vec[0].len = sizeof di_in;
	di_vec[1].data = NULL;   di_vec[1].len = 0x2A4;     /* ticket : lu sur le disque */
	di_vec[2].data = NULL;   di_vec[2].len = 0;
	di_vec[3].data = di_tmd; di_vec[3].len = 0x49E4;
	di_vec[4].data = di_err; di_vec[4].len = sizeof di_err;
	return IOS_Ioctlv(di_fd, 0x8B, 3, 2, di_vec);
}

static void rapport_apploader(const char *f, ...) { (void)f; }
typedef void (*ap_init_t)(void (*)(const char *, ...));
typedef int (*ap_main_t)(void **, int *, int *);
typedef void *(*ap_final_t)(void);
typedef void (*ap_entree_t)(ap_init_t *, ap_main_t *, ap_final_t *);

static void disque_memoire_basse(void)
{
	*(vu32 *)0x80000020 = 0x0D15EA5E;   /* démarrage depuis le disque */
	*(vu32 *)0x80000024 = 1;
	*(vu32 *)0x80000030 = 0;
	*(vu32 *)0x80000060 = 0x38A00040;
	*(vu32 *)0x800000E4 = 0x80431A80;
	*(vu32 *)0x800000EC = 0x81800000;
	*(vu32 *)0x800000F0 = 0x01800000;
	*(vu32 *)0x800000F4 = 0x817E5480;
	*(vu32 *)0x800000F8 = 0x0E7BE2C0;   /* vitesse du bus */
	*(vu32 *)0x800000FC = 0x2B73A840;   /* vitesse du processeur */
	*(vu32 *)0x80003180 = *(vu32 *)0x80000000;
	*(vu32 *)0x80003184 = 0x80000000;
	*(vu32 *)0x80003188 = *(vu32 *)0x80003140;   /* évite l'erreur 002 */
	DCFlushRange((void *)0x80000000, 0x3F00);
}

static void disque_video(void)
{
	GXRModeObj *m;
	u32 reg;
	int prog = CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable();
	switch (CONF_GetVideo()) {
	case CONF_VIDEO_PAL:
		if (CONF_GetEuRGB60() > 0 || prog) { reg = VI_EURGB60; m = prog ? &TVEurgb60Hz480Prog : &TVEurgb60Hz480IntDf; }
		else { reg = VI_PAL; m = &TVPal528IntDf; }
		break;
	case CONF_VIDEO_MPAL: reg = VI_MPAL; m = &TVMpal480IntDf; break;
	default: reg = VI_NTSC; m = prog ? &TVNtsc480Prog : &TVNtsc480IntDf; break;
	}
	*(vu32 *)0x800000CC = reg;
	DCFlushRange((void *)0x800000C0, 32);
	VIDEO_Configure(m);
	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (m->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
	else while (VIDEO_GetNextField()) VIDEO_WaitVSync();
}

/* échec avant le point de non-retour : on revient simplement dans Wiil */
static void disque_abandon(const char *m)
{
	if (di_fd >= 0) { IOS_Close(di_fd); di_fd = -1; }
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS);
	sd_ok = fatMountSimple("sd", &__io_wiisd);
	avertir(m);
}

static void lancer_titre(u64 id);

static void lancer_disque(void)
{
	ecran_message("Lecture du disque…");
	WPAD_Shutdown();
	if (sd_ok) { fatUnmount("sd:"); sd_ok = 0; }
	if (IOS_ReloadIOS(249) < 0) { disque_abandon("cIOS 249 introuvable"); return; }
	di_fd = IOS_Open("/dev/di", 0);
	if (di_fd < 0) { disque_abandon("Lecteur de disque inaccessible"); return; }
	di_cmd(0x8E, 0, 0, NULL, 0);   /* cIOS d2x : lire le vrai disque, pas un disque USB */
	s32 r = 0;
	for (int essai = 0; essai < 3 && r != 1; essai++) {
		di_cmd(0x8A, 1, 0, NULL, 0);                     /* réinitialise et fait tourner le disque */
		r = di_cmd(0x70, 0, 0, di_tampon, 32);           /* identifiant du disque */
	}
	if (r != 1) { disque_abandon("Aucun disque dans la console"); return; }
	memcpy((void *)0x80000000, di_tampon, 32);
	DCFlushRange((void *)0x80000000, 32);

	if (di_tampon[7] == 0xC2339F3D) {                     /* disque GameCube */
		IOS_Close(di_fd); di_fd = -1;
		lancer_titre(0x0000000100000100ULL);              /* mode GameCube de la Wii */
		SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
		return;
	}
	if (di_tampon[6] != 0x5D1C9EA3) { disque_abandon("Disque non reconnu"); return; }

	/* table des partitions : on cherche la partition du jeu (type 0) */
	if (di_lire_brut(di_tampon, 32, 0x40000) != 1) { disque_abandon("Disque illisible"); return; }
	u32 nb = di_tampon[0] > 4 ? 4 : di_tampon[0];
	if (di_lire_brut(di_tampon, 32, (u64)di_tampon[1] << 2) != 1) { disque_abandon("Disque illisible"); return; }
	u32 part = 0;
	for (u32 i = 0; i < nb; i++) if (di_tampon[i * 2 + 1] == 0) { part = di_tampon[i * 2]; break; }
	if (!part || di_ouvrir_partition(part) != 1) { disque_abandon("Partition du jeu illisible"); return; }
	if (di_lire(di_tampon, 32, 0x2440) != 1) { disque_abandon("Disque illisible"); return; }
	ap_entree_t ap_entree = (ap_entree_t)di_tampon[4];
	u32 ap_taille = (di_tampon[5] + di_tampon[6] + 31) & ~31u;

	/* point de non-retour : le jeu va recouvrir la mémoire de l'écran et de la musique */
	musique_arreter();
	GRRLIB_Exit();
	disque_memoire_basse();
	if (di_lire((void *)0x81200000, ap_taille, 0x2460) != 1) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
	DCFlushRange((void *)0x81200000, ap_taille);
	ICInvalidateRange((void *)0x81200000, ap_taille);
	ap_init_t ap_init; ap_main_t ap_main; ap_final_t ap_final;
	ap_entree(&ap_init, &ap_main, &ap_final);
	ap_init(rapport_apploader);
	void *dst; int lg, pos;
	static void *seg_dst[64]; static int seg_lg[64]; int nb_seg = 0;
	while (ap_main(&dst, &lg, &pos)) {
		di_lire(dst, lg, (u64)pos << 2);
		DCFlushRange(dst, lg);
		ICInvalidateRange(dst, lg);
		if (nb_seg < 64) { seg_dst[nb_seg] = dst; seg_lg[nb_seg++] = lg; }
	}
	void *jeu = ap_final();
	/* Anti « erreur 002 » : l'apploader vient d'écrire en 0x80003188 l'IOS attendu par
	   le jeu ; on y met celui qui tourne vraiment, puis on neutralise la vérification
	   dans le code du jeu (même correctif que les chargeurs USB). */
	/* comme WiiFlow : on annonce au jeu que « son » IOS (lu dans le TMD du disque)
	   est déjà chargé, pour qu'il n'essaie pas d'en changer */
	u32 ios_jeu = di_tmd[0x18B];
	*(vu32 *)0x80003140 = (ios_jeu << 16) | 0xFFFF;
	*(vu32 *)0x80003188 = (ios_jeu << 16) | 0xFFFF;
	DCFlushRange((void *)0x80003140, 0x60);
	IOS_Close(di_fd);       /* le jeu rouvrira le lecteur lui-même */
	di_fd = -1;
	/* Le jeu voudra redémarrer « son » IOS au lancement, ce qui fermerait l'accès au
	   disque (écran noir). On demande au cIOS d2x de bloquer ce redémarrage (mode 2 :
	   bloqué, en annonçant au jeu la version qu'il attend, lue dans son TMD). */
	{
		static ioctlv bloc[2] ATTRIBUTE_ALIGN(32);
		static u32 bloc_mode ATTRIBUTE_ALIGN(32), bloc_ios ATTRIBUTE_ALIGN(32);
		bloc_mode = 2;
		bloc_ios = di_tmd[0x18B];
		bloc[0].data = &bloc_mode; bloc[0].len = sizeof(u32);
		bloc[1].data = &bloc_ios;  bloc[1].len = sizeof(u32);
		s32 es = IOS_Open("/dev/es", 0);
		if (es >= 0) { IOS_Ioctlv(es, 0xA0, 2, 0, bloc); IOS_Close(es); }
	}
	for (int s = 0; s < nb_seg; s++) {
		u32 *p = (u32 *)seg_dst[s];
		for (int i = 0; i + 2 < seg_lg[s] / 4; i++)
			if (p[i] == 0x2C000000 && p[i + 1] == 0x40820214 && p[i + 2] == 0x3C608000) {
				p[i + 1] = 0x48000214;
				DCFlushRange(&p[i + 1], 4);
				ICInvalidateRange(&p[i + 1], 4);
			}
	}
	disque_video();
	settime(secs_to_ticks(time(NULL) - 946684800));
	u32 niveau = IRQ_Disable();
	(void)niveau;
	__IOS_ShutdownSubsystems();
	*(vu32 *)0xCC003024 = 1;
	__asm__ volatile("mtlr %0\n\tblr" :: "r"(jeu));
}

/* ------------------------------------------------------------------ cartes du bas */
enum { A_DISQUE, A_MENU, A_HBC, A_GX, A_WF, A_ETEINDRE };
typedef struct { const char *nom; u32 teinte; int icone; int action; } Carte;
static const Carte cartes[] = {
	{ "Disque",        0x7A4FD0FF, 0, A_DISQUE },
	{ "Menu Wii",      0x3C3C9CFF, 1, A_MENU },
	{ "Homebrew",      0x2A88D8FF, 2, A_HBC },
	{ "USB Loader GX", 0x1A9E78FF, 6, A_GX },
	{ "WiiFlow",       0xC0773AFF, 4, A_WF },
	{ "Éteindre",      0x5A6478FF, 5, A_ETEINDRE },
};
#define NB_CARTES 6

/* Retour au vrai menu Wii. Si Wiil démarre avec la console (autoboot Priiloader),
   le mot magique « Pune » demande à Priiloader d'ouvrir le menu Wii au lieu de relancer Wiil. */
static void aller_menu_wii(void)
{
	tout_arreter();
	const u8 pune[4] = { 'P', 'u', 'n', 'e' };
	memcpy((void *)0x8132FFFB, pune, 4);
	memcpy((void *)0x817FEFF0, pune, 4);
	DCFlushRange((void *)0x8132FFE0, 0x40);
	DCFlushRange((void *)0x817FEFE0, 0x40);
	SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

static void action(int a)
{
	switch (a) {
	/* Le lancement direct (lancer_disque) affiche un écran noir avec les disques Wii :
	   en attendant, la carte « Disque » mène au menu Wii, où la chaîne Disque marche toujours. */
	case A_DISQUE: aller_menu_wii(); break;
	case A_MENU: aller_menu_wii(); break;
	case A_HBC: lancer_titre(0x000100014F484243ULL); break;   /* Chaîne Homebrew (OHBC) */
	case A_GX: if (existe(CHEMIN_GX)) lancer_fichier(CHEMIN_GX, NULL); else avertir("USB Loader GX introuvable"); break;
	case A_WF: if (existe(CHEMIN_WF)) lancer_fichier(CHEMIN_WF, NULL); else avertir("WiiFlow introuvable"); break;
	case A_ETEINDRE: tout_arreter(); SYS_ResetSystem(SYS_POWEROFF, 0, 0); break;
	}
}

/* ------------------------------------------------------------------ dessin */
static void ecran_message(const char *m)
{
	GRRLIB_FillScreen(NUIT);
	texte(28, 16, "wiil", 26, BLANC);
	texte(320 - largeur(m, 20) / 2, 225, m, 20, BLANC);
	GRRLIB_Render();
}

static void ecran_progression(int i, int n)
{
	char m[64];
	snprintf(m, sizeof m, "Préparation des jaquettes  %d / %d", i + 1, n);
	GRRLIB_FillScreen(NUIT);
	texte(28, 16, "wiil", 26, BLANC);
	texte(320 - largeur(m, 18) / 2, 215, m, 18, BLANC);
	GRRLIB_Rectangle(170, 250, 300, 6, 0x1E2A40FF, true);
	GRRLIB_Rectangle(170, 250, 300 * (i + 1) / n, 6, CIEL, true);
	GRRLIB_Render();
}

/* Les « boîtes complètes » de WiiFlow (1024x680) contiennent dos + tranche + face :
   on ne garde que la face avant, sur la droite. */
static void zone_face(const GRRLIB_texImg *t, f32 *x0, f32 *w)
{
	if (t->w > t->h * 1.2f) { *x0 = t->w * 0.52f; *w = t->w - *x0; }
	else { *x0 = 0; *w = t->w; }
}

static void dessiner_part_carree(f32 x, f32 y, GRRLIB_texImg *t, f32 cote, u32 coul)
{
	/* recadre la face avant de la jaquette en carré (partie haute, là où est l'illustration) */
	f32 fx, fw;
	zone_face(t, &fx, &fw);
	f32 c = fw < t->h ? fw : t->h;
	f32 py = t->h > fw ? (t->h - c) * 0.25f : 0;
	f32 px = fx + (fw > t->h ? (fw - c) * 0.5f : 0);
	/* DrawPart : le décalage dépend de la taille de l'image entière (sa poignée) et
	   de la partie dessinée ; on le compense pour que (x, y) soit bien le coin haut-gauche. */
	f32 s = cote / c;
	GRRLIB_DrawPart(x - (c - t->w) / 2 * (1 - s), y - (c - t->h) / 2 * (1 - s), px, py, c, c, t, 0, s, s, coul);
}

static u32 alpha(u32 coul, float a) { if (a < 0) a = 0; if (a > 1) a = 1; return (coul & 0xFFFFFF00) | (u32)((coul & 0xFF) * a); }

/* ------------------------------------------------------------------ images pour la DSi */
/* Copie une zone d'une texture en (dw x dh) pixels au format de la DSi, en moyennant
   4 points par pixel ; la transparence est posée sur un fond bleu nuit. */
static void vers_dsi(GRRLIB_texImg *t, f32 sx, f32 sy, f32 sw, f32 sh, int dw, int dh, u16 *out)
{
	for (int y = 0; y < dh; y++)
		for (int x = 0; x < dw; x++) {
			u32 r = 0, g = 0, bl = 0;
			for (int k = 0; k < 4; k++) {
				int px = (int)(sx + (x + 0.25f + 0.5f * (k & 1)) * sw / dw);
				int py = (int)(sy + (y + 0.25f + 0.5f * (k >> 1)) * sh / dh);
				if (px >= (int)t->w) px = t->w - 1;
				if (py >= (int)t->h) py = t->h - 1;
				u32 c = GRRLIB_GetPixelFromtexImg(px, py, t);
				u32 a = c & 255;
				r += (((c >> 24) & 255) * a + 10 * (255 - a)) / 255;
				g += (((c >> 16) & 255) * a + 22 * (255 - a)) / 255;
				bl += (((c >> 8) & 255) * a + 48 * (255 - a)) / 255;
			}
			r /= 4; g /= 4; bl /= 4;
			out[y * dw + x] = (r >> 3) | ((g >> 3) << 5) | ((bl >> 3) << 10) | 0x8000;
		}
}

/* Une demande d'image par image affichée (pour ne pas ralentir le menu). */
static void images_dsi_preparer(void)
{
	if (dem_lu == dem_ecrit) return;
	DemandeImg d = dem_img[dem_lu % 16];
	dem_lu++;
	if (d.o >= 3 || d.i >= onglets[d.o]->n) return;
	GRRLIB_texImg *t = tex_element(onglets[d.o], d.i);
	if (!t || t->w < 4 || t->h < 4) return;
	f32 fx, fw;
	zone_face(t, &fx, &fw);
	if (d.type == 1) {                                /* jaquette : 96 de large, proportions gardées */
		if (jaq_a_envoyer || jaq_occupe) { dem_lu--; return; }   /* la précédente part encore : on réessaiera */
		int w = JAQ_L, h = (int)(JAQ_L * t->h / fw);
		if (h > JAQ_H) { h = JAQ_H; w = (int)(JAQ_H * fw / t->h); }
		if (w < 4) w = 4;
		if (h < 4) h = 4;
		vers_dsi(t, fx, 0, fw, t->h, w, h, jaq_pix);
		jaq_o = d.o; jaq_i = d.i; jaq_w = w; jaq_h = h;
		jaq_a_envoyer = 1;
	} else {                                          /* vignette carrée 20x20 */
		if (mini_ecrit - mini_lu >= 8) { dem_lu--; return; }
		int k = mini_ecrit % 8;
		f32 c = fw < t->h ? fw : t->h;
		f32 px = fx + (fw - c) / 2, py = t->h > fw ? (t->h - c) * 0.25f : 0;
		vers_dsi(t, px, py, c, c, MINI_C, MINI_C, mini_pix[k]);
		mini_o[k] = d.o; mini_i[k] = d.i;
		mini_ecrit++;
	}
}

int main(int argc, char **argv)
{
	GRRLIB_Init();
	WPAD_Init();
	PAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS);
	police = GRRLIB_LoadTTF(police_ttf, police_ttf_size);

	t_ombre_g = GRRLIB_LoadTexturePNG(ombre_gauche_png);
	t_ombre_b = GRRLIB_LoadTexturePNG(ombre_bas_png);
	t_ombre_h = GRRLIB_LoadTexturePNG(ombre_haut_png);
	t_cadre = GRRLIB_LoadTexturePNG(cadre_png);
	t_halo = GRRLIB_LoadTexturePNG(halo_png);
	t_tuile = GRRLIB_LoadTexturePNG(tuile_png);
	t_carte = GRRLIB_LoadTexturePNG(carte_png);
	t_carte_cadre = GRRLIB_LoadTexturePNG(carte_cadre_png);
	t_jouer = GRRLIB_LoadTexturePNG(bouton_jouer_png);
	t_options = GRRLIB_LoadTexturePNG(bouton_options_png);
	t_point = GRRLIB_LoadTexturePNG(point_png);
	t_avatar = GRRLIB_LoadTexturePNG(avatar_png);
	t_souligne = GRRLIB_LoadTexturePNG(souligne_png);
	t_ic[0] = GRRLIB_LoadTexturePNG(ic_disque_png);
	t_ic[1] = GRRLIB_LoadTexturePNG(ic_menuwii_png);
	t_ic[2] = GRRLIB_LoadTexturePNG(ic_maison_png);
	t_ic[3] = GRRLIB_LoadTexturePNG(ic_grille_png);
	t_ic[4] = GRRLIB_LoadTexturePNG(ic_flux_png);
	t_ic[5] = GRRLIB_LoadTexturePNG(ic_eteindre_png);
	t_ic[6] = GRRLIB_LoadTexturePNG(ic_manette_png);
	const u8 *demos[6] = { demo0_png, demo1_png, demo2_png, demo3_png, demo4_png, demo5_png };
	for (int i = 0; i < 6; i++) t_demo[i] = GRRLIB_LoadTexturePNG(demos[i]);
	t_etoile = GRRLIB_LoadTexturePNG(etoile_png);

	ecran_message("Démarrage…");
	sd_ok = fatMountSimple("sd", &__io_wiisd);
	if (sd_ok) {
		scanner_jaquettes("sd:/wiiflow/covers");
		scanner_jaquettes("sd:/wiiflow/boxcovers");
		scanner_jaquettes("sd:/apps/usbloader_gx/images");
		charger_titres();
		scanner_applis();
		charger_favoris();
		qsort(jeux.e, jeux.n, sizeof(Element), cmp_nom);
		qsort(applis.e, applis.n, sizeof(Element), cmp_nom);
		qsort(retro.e, retro.n, sizeof(Element), cmp_nom);
	}
	int mode_demo = 0;
	if (jeux.n == 0) { ajouter_demo(); mode_demo = 1; }
	preparer_jaquettes();
#ifndef SANS_MUSIQUE
	musique_demarrer();
#endif
	reseau_demarrer();

	int onglet = 0, zone = 0, carte = 0;
	int sel[3] = { 0, 0, 0 };
	float defil[3] = { 0, 0, 0 };
	GRRLIB_texImg *fond = NULL, *fond_ancien = NULL;
	float fondu = 1.0f;
	int dernier_sel = -1, dernier_onglet = -1, attente = 0;
	float anim = 0;
	int tenu = 0, bouton = 0;

	while (1) {
		musique_entretenir();
		reseau_entretenir();
		WPAD_ScanPads();
		PAD_ScanPads();
		u32 b = WPAD_ButtonsDown(0);
		u32 g = PAD_ButtonsDown(0);
		/* répétition quand on garde la flèche enfoncée */
		u32 bh = WPAD_ButtonsHeld(0), gh = PAD_ButtonsHeld(0);
		int d_tenu = (bh & WPAD_BUTTON_RIGHT) || (gh & PAD_BUTTON_RIGHT);
		int g_tenu = (bh & WPAD_BUTTON_LEFT) || (gh & PAD_BUTTON_LEFT);
		tenu = (d_tenu || g_tenu) ? tenu + 1 : 0;
		if (tenu > 20 && (tenu - 20) % 6 == 0) {
			if (d_tenu) b |= WPAD_BUTTON_RIGHT;
			if (g_tenu) b |= WPAD_BUTTON_LEFT;
		}
		/* touches de la télécommande DSi : comme si on appuyait sur la Wiimote */
		while (touches_lu != touches_ecrit) {
			static const u32 bouton_wii[9] = { WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT, WPAD_BUTTON_UP, WPAD_BUTTON_DOWN,
			                                   WPAD_BUTTON_A, WPAD_BUTTON_B, WPAD_BUTTON_PLUS, WPAD_BUTTON_MINUS, WPAD_BUTTON_HOME };
			int t = touches[touches_lu % 32];
			touches_lu++;
			if (t >= 0 && t < 9) b |= bouton_wii[t];
			if (t == 9) { zone = 0; b |= WPAD_BUTTON_A; }   /* JOUER : lance l'élément sélectionné */
			break;   /* une touche par image, pour que chaque appui compte */
		}
		if (choix_nouveau) {                       /* un élément touché sur l'écran de la DSi */
			choix_nouveau = 0;
			onglet = choix_o;
			if (onglets[onglet]->n > 0) {
				int i = choix_i;
				if (i < 0) i = 0;
				if (i >= onglets[onglet]->n) i = onglets[onglet]->n - 1;
				sel[onglet] = i;
			}
			zone = 0;
		}
		images_dsi_preparer();
		Liste *l = onglets[onglet];

		/* zones : 0 = tuiles, 1 = boutons Jouer / options, 2 = raccourcis */
		if ((b & WPAD_BUTTON_PLUS) || (g & PAD_TRIGGER_R)) { onglet = (onglet + 1) % 3; zone = 0; bouton = 0; }
		if ((b & WPAD_BUTTON_MINUS) || (g & PAD_TRIGGER_L)) { onglet = (onglet + 2) % 3; zone = 0; bouton = 0; }
		l = onglets[onglet];
		if ((b & WPAD_BUTTON_DOWN) || (g & PAD_BUTTON_DOWN)) { if (zone < 2) zone++; if (zone == 1 && l->n == 0) zone = 2; }
		if ((b & WPAD_BUTTON_UP) || (g & PAD_BUTTON_UP)) { if (zone > 0) zone--; if (zone == 1 && l->n == 0) zone = 0; }
		if (onglet != 0) bouton = 0;   /* le bouton options ne sert qu'aux jeux */
		if ((b & WPAD_BUTTON_RIGHT) || (g & PAD_BUTTON_RIGHT)) {
			if (zone == 0 && sel[onglet] < l->n - 1) sel[onglet]++;
			if (zone == 1 && onglet == 0) bouton = 1;
			if (zone == 2 && carte < NB_CARTES - 1) carte++;
		}
		if ((b & WPAD_BUTTON_LEFT) || (g & PAD_BUTTON_LEFT)) {
			if (zone == 0 && sel[onglet] > 0) sel[onglet]--;
			if (zone == 1) bouton = 0;
			if (zone == 2 && carte > 0) carte--;
		}
		if ((b & WPAD_BUTTON_HOME) || (g & PAD_BUTTON_START)) action(A_MENU);
		/* favori : B sur la Wiimote, ou l'étoile touchée sur la DSi */
		{
			int fo = -1, fi = -1;
			if (((b & WPAD_BUTTON_B) || (g & PAD_BUTTON_B)) && zone == 0 && l->n > 0) { fo = onglet; fi = sel[onglet]; }
			if (fav_nouveau) { fav_nouveau = 0; fo = fav_o; fi = fav_i; }
			if (fo >= 0 && fi >= 0 && fi < onglets[fo]->n) {
				Element *fe = &onglets[fo]->e[fi];
				if (fe->demo >= 0) avertir("Les jeux de démonstration ne peuvent pas être favoris");
				else {
					fe->favori = !fe->favori;
					enregistrer_favoris();
					char m[160];
					snprintf(m, sizeof m, "%s : %.60s", fe->favori ? "Ajouté aux favoris" : "Retiré des favoris", fe->nom);
					avertir(m);
					itm_o = fo; itm_i = fi; itm_a_envoyer = 1;   /* la DSi met son étoile à jour */
				}
			}
		}
		if ((b & WPAD_BUTTON_A) || (g & PAD_BUTTON_A)) {
			if (zone == 2) action(cartes[carte].action);
			else if (zone == 1 && bouton == 1) action(A_GX);   /* options : ouvrir USB Loader GX (réglages du jeu) */
			else if (l->n > 0) {
				Element *e = &l->e[sel[onglet]];
				if (e->demo >= 0) avertir("Jeu de démonstration : mets tes jaquettes sur la carte SD");
				else if (l == &jeux) {
					if (existe(CHEMIN_GX)) lancer_fichier(CHEMIN_GX, e->id);
					else avertir("USB Loader GX introuvable");
				} else lancer_fichier(e->chemin, NULL);
			}
		}

		/* la sélection a changé (DSi, Wiimote ou manette) : on la renvoie à la DSi */
		{
			static int d_onglet = -1, d_sel = -1, d_zone = -1, d_bouton = -1, d_carte = -1;
			l = onglets[onglet];
			int si = l->n ? sel[onglet] : -1;
			if (dsi_connue && (statut_a_envoyer || onglet != d_onglet || si != d_sel || zone != d_zone || bouton != d_bouton || carte != d_carte)) {
				d_onglet = onglet; d_sel = si; d_zone = zone; d_bouton = bouton; d_carte = carte;
				const char *quoi = zone == 2 ? cartes[carte].nom : zone == 1 ? (bouton == 0 ? "Bouton Jouer" : "Bouton options")
				                 : si >= 0 ? l->e[si].nom : "";
				snprintf(statut_dsi, sizeof statut_dsi, "%d|%d|%d|%s", onglet, si, zone, quoi);
				statut_a_envoyer = 1;
			}
		}

		/* fond : recalculé quand la sélection reste stable un court instant */
		int s = l->n ? sel[onglet] : -1;
		if (s != dernier_sel || onglet != dernier_onglet) { attente = 2; dernier_sel = s; dernier_onglet = onglet; }
		if (attente > 0 && --attente == 0) {
			fond_ancien = fond;      /* les fonds sont préparés au démarrage : rien à libérer */
			fond = (onglet == 0 && s >= 0) ? l->e[s].flou : NULL;
			fondu = 0;
			if (l != &jeux) liberer_loin(l, s);
		}
		if (fondu < 1) fondu += 0.06f;
		anim += 0.02f;

		/* ---- dessin ---- */
		GRRLIB_FillScreen(0x0A1630FF);
		if (fond_ancien) dessin(0, 0, fond_ancien, 0, 10, 10, alpha(0xFFFFFFFF, 1 - fondu));
		if (fond) dessin(0, 0, fond, 0, 10, 10, alpha(0xFFFFFFFF, fondu));
		if (onglet != 0) {   /* applis : dégradé bleu nuit animé */
			GRRLIB_Rectangle(0, 0, 640, 480, 0x0B1A36FF, true);
			dessin(380 + sinf(anim) * 20, -60, t_halo, 0, 2.2f, 2.2f, 0x5090D060);
		}

		/* visuel net à droite (jaquette ou icône en grand) */
		if (s >= 0) {
			GRRLIB_texImg *t = tex_element(l, s);
			if (onglet == 0) {
				f32 fx, fw;
				zone_face(t, &fx, &fw);
				f32 sc = 300.0f / t->h;
				f32 hx = 536 - fw * sc / 2, hy = 96;
				/* DrawPart : même compensation que pour les tuiles (poignée = image entière) */
				GRRLIB_DrawPart(hx - (fw - t->w) / 2 * (1 - sc), hy - (t->h - t->h) / 2 * (1 - sc),
				                fx, 0, fw, t->h, t, 0, sc, sc, alpha(0xFFFFFFFF, 0.92f * fondu));
			} else if (t != t_tuile) {
				f32 sc = 2.0f;
				dessin(470 - t->w * sc / 2, 210, t, 0, sc, sc, 0xFFFFFFFF);
			}
		}
		dessin(0, 0, t_ombre_g, 0, 10.0f * 0.9f, 60, 0xFFFFFFFF);
		dessin(0, 0, t_ombre_b, 0, 80, 7.5f, 0xFFFFFFFF);
		dessin(0, 0, t_ombre_h, 0, 80, 3.5f, 0xFFFFFFFF);

		/* en-tête */
		texte(28, 14, "wiil", 26, BLANC);
		dessin(76, 22, t_point, 0, 0.6f, 0.6f, 0xFFFFFFFF);
		int x = 112;
		for (int i = 0; i < 3; i++) {
			wchar_t *w = (wchar_t *)noms_onglets[i];
			GRRLIB_PrintfTTFW(x, 20, police, w, 17, i == onglet ? BLANC : GRIS);
			int lw = GRRLIB_WidthTTFW(police, w, 17);
			if (i == onglet) dessin(x, 44, t_souligne, 0, lw / 40.0f, 0.75f, 0xFFFFFFFF);
			x += lw + 22;
		}
		time_t now = time(NULL);
		struct tm *tm = localtime(&now);
		char heure[8];
		snprintf(heure, sizeof heure, "%02d:%02d", tm->tm_hour, tm->tm_min);
		texte(560, 20, heure, 17, BLANC);
		dessin(522, 15, t_avatar, 0, 0.8f, 0.8f, 0xFFFFFFFF);
		texte(531, 19, "W", 15, BLANC);
		/* piles des Wiimotes connectées (à gauche de l'avatar) */
		{
			int bx = 512;
			for (int m = WPAD_MAX_WIIMOTES - 1; m >= 0; m--) {
				u32 type;
				if (WPAD_Probe(m, &type) != WPAD_ERR_NONE) continue;
				int pct = WPAD_BatteryLevel(m) * 100 / 200;   /* niveau brut : ~200 = piles neuves */
				if (pct > 100) pct = 100;
				char p[8];
				snprintf(p, sizeof p, "%d%%", pct);
				int lw = largeur(p, 12);
				bx -= lw + 44;
				u32 coul = pct > 50 ? 0x4ADE80FF : pct > 20 ? 0xFACC15FF : 0xF87171FF;
				texte(bx, 22, m == 0 ? "1" : m == 1 ? "2" : m == 2 ? "3" : "4", 12, GRIS);
				GRRLIB_Rectangle(bx + 10, 23, 20, 11, BLANC, false);         /* corps de la pile */
				GRRLIB_Rectangle(bx + 30, 26, 2, 5, BLANC, true);            /* borne */
				GRRLIB_Rectangle(bx + 12, 25, 16 * pct / 100, 7, coul, true);
				texte(bx + 36, 22, p, 12, BLANC);
			}
		}

		/* rangée de tuiles */
		const f32 PETIT = 46, GRAND = 76, ECART = 7, X0 = 28, Y0 = 60;
		if (l->n == 0) {
			texte(28, 110, onglet == 0 ? "Aucun jeu trouvé" : "Aucune appli ici", 18, GRIS);
		} else {
			int cible = sel[onglet] - 3;
			if (cible < 0) cible = 0;
			defil[onglet] += (cible - defil[onglet]) * 0.2f;
			f32 xx = X0 - defil[onglet] * (PETIT + ECART);
			for (int i = 0; i < l->n; i++) {
				int est_sel = (i == sel[onglet]);
				f32 cote = est_sel ? GRAND : PETIT;
				if (xx > 650) break;
				if (xx + cote > -10) {
					GRRLIB_texImg *t = tex_element(l, i);
					f32 yy = est_sel ? Y0 : Y0 + (GRAND - PETIT) / 2;
					if (est_sel && zone == 0) dessin(xx - 13, yy - 13, t_halo, 0, (cote + 26) / 160.0f * 1.25f, (cote + 26) / 160.0f * 1.25f, 0xFFFFFFC0);
					if (onglet == 0) dessiner_part_carree(xx, yy, t, cote, est_sel ? 0xFFFFFFFF : 0xFFFFFFD0);
					else {
						dessin(xx, yy, t_tuile, 0, cote / 128.0f, cote / 128.0f, 0xFFFFFFFF);
						if (t != t_tuile) {
							f32 sc = (cote - 6) / t->w;
							dessin(xx + 3, yy + cote / 2 - t->h * sc / 2, t, 0, sc, sc, 0xFFFFFFFF);
						}
					}
					if (l->e[i].favori) dessin(xx + cote - 13, yy - 3, t_etoile, 0, est_sel ? 1.0f : 0.8f, est_sel ? 1.0f : 0.8f, 0xFFFFFFFF);
					if (est_sel) {
						dessin(xx - 2, yy - 2, t_cadre, 0, (cote + 4) / 128.0f, (cote + 4) / 128.0f, zone == 0 ? 0xFFFFFFFF : 0xFFFFFF60);
						char court[40];
						snprintf(court, sizeof court, "%.24s", l->e[i].nom);
						texte(xx, yy + cote + 6, court, 12, BLANC);
					}
				}
				xx += cote + ECART;
			}

			/* nom en grand + infos */
			Element *e = &l->e[sel[onglet]];
			int taille = 40;
			while (taille > 22 && largeur(e->nom, taille) > 400) taille -= 2;
			texte(28, 214, e->nom, taille, BLANC);
			char info[96];
			if (onglet == 0) snprintf(info, sizeof info, "%s  ·  %s", e->gamecube ? "GameCube" : "Wii", e->demo >= 0 ? "Démonstration" : e->id);
			else snprintf(info, sizeof info, "%s", onglet == 2 ? "Émulateur" : "Application");
			texte(30, 214 + taille + 10, info, 14, GRIS);
			int sur_jouer = (zone == 1 && bouton == 0), sur_options = (zone == 1 && bouton == 1);
			if (sur_jouer) dessin(14, 284, t_halo, 0, 1.06f, 0.48f, 0xFFFFFFD0);
			if (sur_options) dessin(160, 284, t_halo, 0, 0.5f, 0.48f, 0xFFFFFFD0);
			dessin(30, 300, t_jouer, 0, 0.9f, 0.9f, sur_jouer || zone == 0 ? 0xFFFFFFFF : 0xFFFFFFB0);
			texte(30 + 135 / 2 - largeur("Jouer", 18) / 2, 309, "Jouer", 18, NUIT);
			if (onglet == 0) {
				dessin(176, 300, t_options, 0, 0.9f, 0.9f, 0xFFFFFFFF);
				if (sur_options) {
					dessin(174, 298, t_cadre, 0, 47 / 128.0f, 47 / 128.0f, 0xFFFFFFFF);
					texte(230, 312, "Réglages dans USB Loader GX", 13, BLANC);
				}
			}
		}

		/* cartes « Raccourcis » */
		texte(28, 362, "Raccourcis", 12, GRIS);
		for (int i = 0; i < NB_CARTES; i++) {
			f32 cx = 22 + i * 100, cy = 382;
			dessin(cx, cy, t_carte, 0, 94 / 176.0f, 64 / 96.0f, cartes[i].teinte);
			dessin(cx + 8, cy + 6, t_ic[cartes[i].icone], 0, 0.42f, 0.42f, 0xFFFFFFFF);
			texte(cx + 8, cy + 40, cartes[i].nom, 11, BLANC);
			if (zone == 2 && i == carte) dessin(cx - 2, cy - 2, t_carte_cadre, 0, 98 / 176.0f, 68 / 96.0f, 0xFFFFFFFF);
		}

		/* aide + message */
		texte(28, 456, mode_demo ? "Démo : aucune jaquette trouvée sur la carte SD" : "A Jouer   ·   B Favori   ·   +/- Onglets   ·   Haut/Bas Raccourcis   ·   HOME Menu Wii", 11, 0x8C99ACFF);
		if (message_temps > 0) {
			message_temps--;
			int lw = largeur(message, 15);
			GRRLIB_Rectangle(320 - lw / 2 - 14, 420, lw + 28, 30, 0x101C34E8, true);
			texte(320 - lw / 2, 426, message, 15, BLANC);
		}
		GRRLIB_Render();
	}
	return 0;
}

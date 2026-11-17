// Wiil DSi — télécommande et second écran pour le menu Wiil (version 3).
//
// 1. Connexion Wi-Fi : la DSi se souvient du réseau (fichier wiil_dsi.ini sur
//    sa carte SD) et s'y reconnecte toute seule aux lancements suivants.
// 2. Interface : écran du haut = le jeu sélectionné (jaquette, fiche) ;
//    écran du bas = onglets et liste tactile, boutons Jouer / Menu Wii / Options.
// 3. Réseau (UDP, port 4242) : la DSi demande à la Wii la liste, la fiche et les
//    images ; la Wii envoie la sélection à chaque changement. Tout message perdu
//    est redemandé.
//
// Partie connexion basée sur l'exemple get_website de BlocksDS (CC0, Antonio Niño Díaz).

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <fat.h>
#include <nds.h>
#include <dswifi9.h>

#include "dessin.h"

#define PORT 4242

static PrintConsole haut;
static PrintConsole bas;
static Wifi_AccessPoint point_acces;

// ------------------------------------------------------------ journal de débogage
// Avant l'interface graphique : affiché sur l'écran du haut (console texte).
// Ensuite : gardé en mémoire, visible dans Options > Messages de débogage.
static bool mode_graphique = false;
static char journal[14][52];
static int journal_n = 0;
static bool haut_a_redessiner = true, bas_a_redessiner = true;
static bool voir_journal = false;   // journal affiché sur l'écran du haut ?

static void journal_ajouter(const char *s)
{
    char ligne[52];
    int n = 0;
    while (*s && n < 51)
    {
        if (*s == '\n') { s++; if (n) break; continue; }
        ligne[n++] = *s++;
    }
    ligne[n] = 0;
    if (!n) return;
    if (journal_n == 14) { memmove(journal[0], journal[1], sizeof journal[0] * 13); journal_n = 13; }
    strcpy(journal[journal_n++], ligne);
    if (voir_journal) haut_a_redessiner = true;   // sinon : pas de dessin inutile
}

#define DEBUG(...) do { \
        char _m[128]; snprintf(_m, sizeof _m, __VA_ARGS__); journal_ajouter(_m); \
        if (!mode_graphique) { consoleSelect(&haut); printf("%s", _m); consoleSelect(&bas); } \
    } while (0)

static void attendre_image(void) { cothread_yield_irq(IRQ_VBLANK); }

static void touche_clavier(int touche)
{
    if (touche > 0)
        printf("%c", touche);
}

// ------------------------------------------------------------ réseau mémorisé
// Fichier texte simple, 2 lignes : le nom du réseau, puis le mot de passe.
static char fichier_cfg[64] = "";
static char cfg_ssid[33];
static char cfg_mdp[65];

static void carte_sd_init(void)
{
    if (!fatInitDefault())
    {
        DEBUG("Carte SD illisible : le reseau\n  ne sera pas memorise\n");
        return;
    }
    // DSi : carte SD interne « sd: » ; sinon linker (« fat: »)
    FILE *t = fopen("sd:/_nds/wiil_dsi.tmp", "wb");
    if (t) { fclose(t); remove("sd:/_nds/wiil_dsi.tmp"); strcpy(fichier_cfg, "sd:/_nds/wiil_dsi.ini"); }
    else strcpy(fichier_cfg, "fat:/_nds/wiil_dsi.ini");
}

static bool cfg_lire(void)
{
    if (!fichier_cfg[0]) return false;
    FILE *f = fopen(fichier_cfg, "rb");
    if (!f) return false;
    bool ok = fgets(cfg_ssid, sizeof cfg_ssid, f) && fgets(cfg_mdp, sizeof cfg_mdp, f);
    fclose(f);
    if (!ok) return false;
    cfg_ssid[strcspn(cfg_ssid, "\r\n")] = 0;
    cfg_mdp[strcspn(cfg_mdp, "\r\n")] = 0;
    return cfg_ssid[0] != 0;
}

static void cfg_ecrire(const char *ssid, const char *mdp)
{
    if (!fichier_cfg[0]) return;
    FILE *f = fopen(fichier_cfg, "wb");
    if (!f) { DEBUG("Impossible d'enregistrer le reseau\n"); return; }
    fprintf(f, "%s\n%s\n", ssid, mdp ? mdp : "");
    fclose(f);
    DEBUG("Reseau memorise sur la carte SD\n");
}

static void cfg_oublier(void)
{
    if (fichier_cfg[0] && remove(fichier_cfg) == 0)
        DEBUG("Reseau oublie.\n");
}

// ------------------------------------------------------------ connexion Wi-Fi
static char mdp_tape[100];   // gardé pour l'enregistrer si la connexion réussit

// Liste les réseaux visibles ; haut/bas pour choisir, A pour valider.
static void choisir_reseau(void)
{
    Wifi_ScanMode();
    int choix = 0;

    while (1)
    {
        attendre_image();
        scanKeys();
        u16 k = keysDown();
        int n = Wifi_GetNumAP();

        consoleClear();
        printf("Reseaux trouves : %d\n", n);
        printf("Haut/Bas puis A\n\n");
        if (n == 0)
            continue;

        if (k & KEY_UP) choix--;
        if (k & KEY_DOWN) choix++;
        if (choix < 0) choix = 0;
        if (choix >= n) choix = n - 1;

        int premier = choix - 3;
        if (premier < 0) premier = 0;
        int dernier = premier + 4;
        if (dernier >= n) dernier = n - 1;

        for (int i = premier; i <= dernier; i++)
        {
            Wifi_AccessPoint ap;
            Wifi_GetAPData(i, &ap);
            printf("%s %.24s\n", i == choix ? "->" : "  ", ap.ssid);
            printf("   %-4s  canal %2d  signal %d\n\n",
                   Wifi_ApSecurityTypeString(ap.security_type), ap.channel, ap.rssi);
            if (i == choix)
                point_acces = ap;
        }

        if ((k & KEY_A) && (point_acces.flags & WFLAG_APDATA_COMPATIBLE))
            break;
    }
}

// Réseau choisi à la main, mot de passe tapé au clavier tactile.
static void connecter_reseau_choisi(void)
{
    choisir_reseau();
    Wifi_SetIP(0, 0, 0, 0, 0);   // adresse donnée par la box (DHCP)
    DEBUG("Reseau choisi : %.24s\n", point_acces.ssid);
    mdp_tape[0] = '\0';

    if (point_acces.security_type == AP_SECURITY_OPEN)
    {
        Wifi_ConnectSecureAP(&point_acces, NULL, 0);
        return;
    }

    consoleClear();
    printf("Mot de passe du Wi-Fi\n(clavier tactile, puis Rtrn) :\n");
    size_t lg;
    while (1)
    {
        mdp_tape[0] = '\0';
        scanf("%99s", mdp_tape);
        lg = strlen(mdp_tape);
        if (lg > 0 && mdp_tape[lg - 1] == '\n')
            mdp_tape[--lg] = '\0';
        if (point_acces.security_type == AP_SECURITY_WEP ? (lg == 5 || lg == 13) : (lg >= 8 && lg <= 64))
            break;
        printf("\nLongueur incorrecte (%u), recommence :\n", (unsigned)lg);
    }
    consoleClear();   // le mot de passe ne reste pas affiché
    DEBUG("Mot de passe tape : %u caracteres\n", (unsigned)lg);
    Wifi_ConnectSecureAP(&point_acces, mdp_tape, lg);
}

// Réseau mémorisé : on le cherche (10 s max), puis on s'y connecte.
static bool connecter_reseau_memorise(void)
{
    DEBUG("Recherche de \"%.20s\"...\n", cfg_ssid);
    Wifi_ScanMode();
    for (int images = 0; images < 60 * 10; images++)
    {
        attendre_image();
        scanKeys();
        if (keysDown() & KEY_B) { DEBUG("Annule.\n"); return false; }
        int n = Wifi_GetNumAP();
        for (int i = 0; i < n; i++)
        {
            Wifi_AccessPoint ap;
            Wifi_GetAPData(i, &ap);
            if (strcmp(ap.ssid, cfg_ssid) == 0 && (ap.flags & WFLAG_APDATA_COMPATIBLE))
            {
                point_acces = ap;
                Wifi_SetIP(0, 0, 0, 0, 0);
                size_t lg = strlen(cfg_mdp);
                Wifi_ConnectSecureAP(&point_acces, lg ? cfg_mdp : NULL, lg);
                return true;
            }
        }
    }
    DEBUG("Reseau memorise introuvable.\n");
    return false;
}

// Attend la connexion. true si connecté, false si échec ou annulation (B).
static bool attendre_connexion(void)
{
    int ancien = -1, images = 0;
    DEBUG("Connexion en cours (B = annuler)\n");
    consoleSelect(&bas);
    consoleClear();
    printf("Connexion... B = annuler\n");
    while (1)
    {
        attendre_image();
        images++;
        scanKeys();
        if (keysDown() & KEY_B)
        {
            DEBUG("Annule apres %d s.\n", images / 60);
            Wifi_DisconnectAP();
            return false;
        }
        int etat = Wifi_AssocStatus();
        if (etat != ancien)
        {
            DEBUG("  %3d s  etat : %s\n", images / 60, ASSOCSTATUS_STRINGS[etat]);
            ancien = etat;
        }
        if (images % 60 == 0)
            printf("\x1b[2;0H%3d s - %s       \n", images / 60, ASSOCSTATUS_STRINGS[etat]);
        if (etat == ASSOCSTATUS_CANNOTCONNECT)
        {
            DEBUG("Echec de la connexion.\n");
            return false;
        }
        if (etat == ASSOCSTATUS_ASSOCIATED)
            return true;
    }
}

// ------------------------------------------------------------ données reçues de la Wii
#define MAX_EL 256
#define JAQ_L 96
#define JAQ_H 132
#define MINI_C 20
#define MORCEAU 1000

typedef struct {
    int n;                     // -1 = pas encore reçue
    char noms[MAX_EL][48];
    u8 ok[MAX_EL];             // nom reçu ?
    u8 fav[MAX_EL];            // favori (étoile) ?
    u16 *mini[MAX_EL];         // vignettes 20x20 (allouées à la réception)
} ListeDSi;

static ListeDSi listes[3];
static const char *noms_onglets[3] = { "Jeux", "Applis", "Rétro" };

static int sel_o = 0, sel_i[3] = { 0, 0, 0 }, sel_zone = 0;
static char sel_nom[128] = "";

static int inf_o = -1, inf_i = -1;
static char inf_type[24], inf_id[8], inf_annee[8], inf_joueurs[16], inf_genre[32], inf_editeur[48];

static u16 jaq[JAQ_L * JAQ_H];           // jaquette affichée
static int jaq_o = -1, jaq_i = -1, jaq_w = 0, jaq_h = 0;
static u16 jr[JAQ_L * JAQ_H];            // jaquette en cours de réception
static int jr_o = -1, jr_i = -1, jr_w = 0, jr_h = 0, jr_nk = 0, jr_recus = 0;
static u8 jr_morceau[32];
static int t_info = -999, t_img = -999;   // dernières demandes de fiche / jaquette (remis à zéro quand la sélection change)
static int jr_demande[32];               // image où chaque morceau a été demandé (-1 = jamais)

// Cache des dernières jaquettes reçues : revenir sur un jeu déjà vu est instantané
#define NB_CACHE 8
typedef struct { int o, i, w, h, age; u16 pix[JAQ_L * JAQ_H]; } JaqCache;
static JaqCache cache_jaq[NB_CACHE];
static int cache_age = 0;

static void cache_ranger(void)
{
    int k, plus_vieux = 0;
    for (k = 0; k < NB_CACHE; k++)
        if (cache_jaq[k].w && cache_jaq[k].o == jaq_o && cache_jaq[k].i == jaq_i) break;
    if (k == NB_CACHE)
        for (k = 0; k < NB_CACHE; k++)
        {
            if (!cache_jaq[k].w) break;
            if (cache_jaq[k].age < cache_jaq[plus_vieux].age) plus_vieux = k;
        }
    if (k == NB_CACHE) k = plus_vieux;
    cache_jaq[k].o = jaq_o; cache_jaq[k].i = jaq_i; cache_jaq[k].w = jaq_w; cache_jaq[k].h = jaq_h;
    cache_jaq[k].age = ++cache_age;
    memcpy(cache_jaq[k].pix, jaq, jaq_w * jaq_h * 2);
}

static void calculer_fond(void);
static bool cache_sortir(int o, int i)
{
    for (int k = 0; k < NB_CACHE; k++)
        if (cache_jaq[k].w && cache_jaq[k].o == o && cache_jaq[k].i == i)
        {
            jaq_o = o; jaq_i = i; jaq_w = cache_jaq[k].w; jaq_h = cache_jaq[k].h;
            memcpy(jaq, cache_jaq[k].pix, jaq_w * jaq_h * 2);
            cache_jaq[k].age = ++cache_age;
            calculer_fond();
            return true;
        }
    return false;
}
static u16 fond_flou[LARG * HAUT];       // fond de l'écran du haut, tiré de la jaquette (voile compris)
static u16 fond_defaut[LARG * HAUT];     // dégradé bleu nuit (voile compris), calculé une fois
static bool fond_ok = false;

// ------------------------------------------------------------ réseau
static int sock = -1;
static struct sockaddr_in dest;          // broadcast au début, puis l'adresse de la Wii
static bool wii_trouvee = false;
static int derniere_reponse = 0;         // image où la Wii a répondu pour la dernière fois

static void envoyer(const char *fmt, ...)
{
    char msg[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof dest) < 0)
        DEBUG("ERREUR envoi : %d\n", errno);
}

// Ordres importants (touches, choix, favoris) : numérotés et renvoyés toutes les
// 15 images (1/4 s) jusqu'à 4 fois, tant que la Wii n'a pas répondu « ACK n ».
typedef struct { int id, t, essais; char msg[64]; } Ordre;
static Ordre ordres[8];
static int prochain_id = 0, pause_images = 0;
static int images_t;

static void envoyer_ordre(const char *fmt, ...)
{
    if (!prochain_id) prochain_id = 1 + (int)(time(NULL) % 100000) * 10;   // différent à chaque lancement
    char base[56];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(base, sizeof base, fmt, ap);
    va_end(ap);
    int k, plus_vieux = 0;
    for (k = 0; k < 8; k++)
    {
        if (!ordres[k].id) break;
        if (ordres[k].t < ordres[plus_vieux].t) plus_vieux = k;
    }
    if (k == 8) k = plus_vieux;          // file pleine : on remplace le plus ancien
    ordres[k].id = prochain_id++;
    snprintf(ordres[k].msg, sizeof ordres[k].msg, "%s %d", base, ordres[k].id);
    ordres[k].t = images_t;
    ordres[k].essais = 1;
    envoyer("%s", ordres[k].msg);
    pause_images = images_t + 30;        // les touches d'abord : pas d'images pendant 0,5 s
}

static void relancer_ordres(void)
{
    for (int k = 0; k < 8; k++)
        if (ordres[k].id && images_t - ordres[k].t >= 15)
        {
            if (ordres[k].essais >= 4) { DEBUG("Ordre perdu : %s\n", ordres[k].msg); ordres[k].id = 0; continue; }
            envoyer("%s", ordres[k].msg);
            ordres[k].t = images_t;
            ordres[k].essais++;
            DEBUG("Renvoi (essai %d) : %s\n", ordres[k].essais, ordres[k].msg);
        }
}

// Découpe « a|b|c » en champs (modifie la chaîne)
static int champs(char *s, char **c, int max)
{
    int n = 0;
    c[n++] = s;
    for (; *s && n < max; s++)
        if (*s == '|') { *s = 0; c[n++] = s + 1; }
    return n;
}


static int defil = 0, defil_cible = 0;   // défilement de la liste, en pixels
#define LISTE_Y0 32
#define LISTE_Y1 162
#define LIGNE_H 26

static void rendre_visible(int i)
{
    int y = i * LIGNE_H, vis = LISTE_Y1 - LISTE_Y0;
    if (y < defil_cible) defil_cible = y;
    if (y + LIGNE_H > defil_cible + vis) defil_cible = y + LIGNE_H - vis;
}

// Fond flou : la jaquette réduite à 16x12 puis agrandie en douceur, assombrie.
// Le processeur de la DSi n'a pas de division rapide : tout ce qui dépend de x est
// précalculé une fois, et l'image est remplie 24 lignes par image (8 images en tout),
// pour ne jamais figer l'écran ni rater un appui.
static int fond_m[12][16][3];
static int fond_ligne = -1, fond_cible_o = -1, fond_cible_i = -1, fond_o = -1, fond_i = -1;
static u16 fond_calc[LARG * HAUT];

static void calculer_fond(void)
{
    int (*m)[16][3] = fond_m;
    for (int by = 0; by < 12; by++)
        for (int bx = 0; bx < 16; bx++)
        {
            int r = 0, g = 0, b = 0, n = 0;
            for (int y = by * jaq_h / 12; y < (by + 1) * jaq_h / 12; y++)
                for (int x = bx * jaq_w / 16; x < (bx + 1) * jaq_w / 16; x++)
                {
                    u16 c = jaq[y * jaq_w + x];
                    r += c & 31; g += (c >> 5) & 31; b += (c >> 10) & 31; n++;
                }
            if (!n) n = 1;
            m[by][bx][0] = r * 256 / n; m[by][bx][1] = g * 256 / n; m[by][bx][2] = b * 256 / n;
        }
    fond_ligne = 0;                     // le remplissage commence (voir avancer_fond)
    fond_cible_o = jaq_o; fond_cible_i = jaq_i;
}

static void avancer_fond(void)
{
    static u8 col_x0[LARG], col_x1[LARG], col_tx[LARG], col_voile[LARG];
    static bool tables = false;
    if (!tables)
    {   // tout ce qui dépend de x, calculé une seule fois
        for (int x = 0; x < LARG; x++)
        {
            int fx = x * 15 * 256 / (LARG - 1);
            col_x0[x] = fx >> 8; col_tx[x] = fx & 255; col_x1[x] = col_x0[x] < 15 ? col_x0[x] + 1 : 15;
            col_voile[x] = x < 200 ? 170 * (200 - x) / 200 : 0;
        }
        tables = true;
    }
    if (fond_ligne < 0) return;
    int (*m)[16][3] = fond_m;
    int fin = fond_ligne + 24 > HAUT ? HAUT : fond_ligne + 24;
    for (int y = fond_ligne; y < fin; y++)
    {
        int fy = y * 11 * 256 / (HAUT - 1), y0 = fy >> 8, ty = fy & 255, y1 = y0 < 11 ? y0 + 1 : 11;
        for (int x = 0; x < LARG; x++)
        {
            int x0 = col_x0[x], x1 = col_x1[x], tx = col_tx[x];
            int c[3];
            for (int k = 0; k < 3; k++)
            {
                int a = (m[y0][x0][k] * (256 - tx) + m[y0][x1][k] * tx) >> 8;
                int b = (m[y1][x0][k] * (256 - tx) + m[y1][x1][k] * tx) >> 8;
                c[k] = (((a * (256 - ty) + b * ty) >> 8) * 141) >> 16;   // ×0,55 : assombri
            }
            u16 px = c[0] | (c[1] << 5) | (c[2] << 10) | 0x8000;
            if (col_voile[x]) px = melange(px, NUIT_, col_voile[x]);   // voile pour lire le texte
            fond_calc[y * LARG + x] = px;
        }
    }
    fond_ligne = fin;
    if (fond_ligne >= HAUT)
    {   // terminé : on l'affiche
        memcpy(fond_flou, fond_calc, sizeof fond_flou);
        fond_o = fond_cible_o; fond_i = fond_cible_i;
        fond_ok = true;
        fond_ligne = -1;
        haut_a_redessiner = true;
    }
}

static void traiter_paquet(char *p, int n, struct sockaddr_in *de)
{
    p[n] = 0;
    if (!wii_trouvee && (!strncmp(p, "WIIL", 4) || !strncmp(p, "SEL ", 4)))
    {
        wii_trouvee = true;
        dest.sin_addr = de->sin_addr;   // désormais on parle directement à la Wii
        DEBUG("Wii trouvee : %s\n", inet_ntoa(de->sin_addr));
        haut_a_redessiner = bas_a_redessiner = true;
    }
    derniere_reponse = images_t;
    char *c[10];

    if (!strncmp(p, "ACK ", 4))
    {
        int id = atoi(p + 4);
        for (int k = 0; k < 8; k++) if (ordres[k].id == id) ordres[k].id = 0;
        return;
    }
    if (!strncmp(p, "SEL ", 4))
    {
        int nb = champs(p + 4, c, 4);
        if (nb < 4) return;
        int o = atoi(c[0]), i = atoi(c[1]);
        if (o < 0 || o > 2) return;
        if (o != sel_o) defil = defil_cible = 0;
        sel_o = o;
        if (i >= 0) { sel_i[o] = i; rendre_visible(i); }
        sel_zone = atoi(c[2]);
        snprintf(sel_nom, sizeof sel_nom, "%s", c[3]);
        t_info = t_img = -999;
        haut_a_redessiner = bas_a_redessiner = true;
    }
    else if (!strncmp(p, "LST ", 4))
    {
        if (champs(p + 4, c, 2) < 2) return;
        int o = atoi(c[0]), nn = atoi(c[1]);
        if (o < 0 || o > 2) return;
        if (nn > MAX_EL) nn = MAX_EL;
        if (listes[o].n != nn) { listes[o].n = nn; memset(listes[o].ok, 0, sizeof listes[o].ok); }
        bas_a_redessiner = haut_a_redessiner = true;
    }
    else if (!strncmp(p, "ITM ", 4))
    {
        if (champs(p + 4, c, 4) < 4) return;
        int o = atoi(c[0]), i = atoi(c[1]);
        if (o < 0 || o > 2 || i < 0 || i >= MAX_EL) return;
        listes[o].fav[i] = atoi(c[2]) != 0;
        snprintf(listes[o].noms[i], 48, "%s", c[3]);
        haut_a_redessiner = true;
        listes[o].ok[i] = 1;
        bas_a_redessiner = true;
    }
    else if (!strncmp(p, "INF ", 4))
    {
        if (champs(p + 4, c, 8) < 8) return;
        inf_o = atoi(c[0]); inf_i = atoi(c[1]);
        snprintf(inf_type, sizeof inf_type, "%s", c[2]);
        snprintf(inf_id, sizeof inf_id, "%s", c[3]);
        snprintf(inf_annee, sizeof inf_annee, "%s", c[4]);
        snprintf(inf_joueurs, sizeof inf_joueurs, "%s", c[5]);
        snprintf(inf_genre, sizeof inf_genre, "%s", c[6]);
        snprintf(inf_editeur, sizeof inf_editeur, "%s", c[7]);
        haut_a_redessiner = true;
    }
    else if (!strncmp(p, "IMG ", 4) || !strncmp(p, "MIN ", 4))
    {
        char *nl = memchr(p, '\n', n);
        if (!nl) return;
        *nl = 0;
        u8 *data = (u8 *)nl + 1;
        int lg = n - (int)(data - (u8 *)p);
        bool jaquette = p[1] == 'M';
        int nb = champs(p + 4, c, 6);
        if (nb < 4) return;
        int o = atoi(c[0]), i = atoi(c[1]), w = atoi(c[2]), h = atoi(c[3]);
        if (o < 0 || o > 2 || i < 0 || i >= MAX_EL || w <= 0 || h <= 0) return;
        if (!jaquette)
        {
            if (w != MINI_C || h != MINI_C || lg < MINI_C * MINI_C * 2) return;
            if (!listes[o].mini[i]) listes[o].mini[i] = malloc(MINI_C * MINI_C * 2);
            if (listes[o].mini[i]) memcpy(listes[o].mini[i], data, MINI_C * MINI_C * 2);
            bas_a_redessiner = true;
            return;
        }
        if (nb < 6 || w > JAQ_L || h > JAQ_H) return;
        int k = atoi(c[4]), nk = atoi(c[5]);
        if (nk <= 0 || nk > 32 || k < 0 || k >= nk) return;
        if (o != jr_o || i != jr_i || w != jr_w || h != jr_h)
        {   // nouvelle image : on repart de zéro
            jr_o = o; jr_i = i; jr_w = w; jr_h = h; jr_nk = nk; jr_recus = 0;
            memset(jr_morceau, 0, sizeof jr_morceau);
            for (int m = 0; m < 32; m++) jr_demande[m] = -1;
        }
        if (jr_morceau[k]) return;
        int deb = k * MORCEAU;
        if (deb + lg > w * h * 2) lg = w * h * 2 - deb;
        if (lg > 0) memcpy((u8 *)jr + deb, data, lg);
        jr_morceau[k] = 1;
        jr_recus++;
        if (jr_recus == jr_nk)
        {   // image complète
            memcpy(jaq, jr, w * h * 2);
            jaq_o = o; jaq_i = i; jaq_w = w; jaq_h = h;
            calculer_fond();
            cache_ranger();
            haut_a_redessiner = true;
        }
    }
}

static void lire_reseau(void)
{
    static char paquet[1200];
    struct sockaddr_in de;
    socklen_t lg_de = sizeof de;
    int n;
    while ((n = recvfrom(sock, paquet, sizeof paquet - 1, 0, (struct sockaddr *)&de, &lg_de)) > 0)
    {
        traiter_paquet(paquet, n, &de);
        lg_de = sizeof de;
    }
}

// Demandes à la Wii, par ordre d'importance, avec relances si un message se perd :
// 1. la sélection (fiche + jaquette), 2. la liste de l'onglet affiché, 3. les vignettes
// visibles, 4. les autres listes et les noms perdus.

static void demander(void)
{
    static int t_bonjour = -999, t_liste[3] = { -999, -999, -999 }, t_trous = 0;
    static int mini_dem_o[2] = { -1, -1 }, mini_dem_i[2], mini_dem_t[2];
    int t = images_t;
    if (!wii_trouvee)
    {
        if (t - t_bonjour > 120) { envoyer("BONJOUR"); t_bonjour = t; }
        return;
    }
    ListeDSi *l = &listes[sel_o];
    int si = sel_i[sel_o];

    // 1. la sélection
    if (sel_zone == 0 && l->n > 0 && si < l->n)
    {
        if ((inf_o != sel_o || inf_i != si) && t - t_info > 20) { envoyer("INFO %d %d", sel_o, si); t_info = t; }
        if ((jaq_o != sel_o || jaq_i != si) && images_t >= pause_images)
        {
            if (!cache_sortir(sel_o, si))
            {
                if (jr_o == sel_o && jr_i == si && jr_nk > 0)
                {   // on tire les morceaux manquants : 3 en vol au plus, relance après 12 images
                    int en_vol = 0;
                    for (int k = 0; k < jr_nk; k++)
                        if (!jr_morceau[k] && jr_demande[k] >= 0 && t - jr_demande[k] < 12) en_vol++;
                    for (int k = 0; k < jr_nk && en_vol < 3; k++)
                        if (!jr_morceau[k] && (jr_demande[k] < 0 || t - jr_demande[k] >= 12))
                        {
                            envoyer("IMGP %d %d %d", sel_o, si, k);
                            jr_demande[k] = t;
                            en_vol++;
                        }
                }
                else if (t - t_img > 20) { envoyer("IMG %d %d", sel_o, si); t_img = t; }
            }
            else haut_a_redessiner = true;
        }
    }

    // 2. la liste de l'onglet affiché
    if (l->n < 0)
    {
        if (t - t_liste[sel_o] > 90) { envoyer("LISTE %d", sel_o); t_liste[sel_o] = t; }
        return;
    }

    if (images_t < pause_images) return;   // un appui vient d'avoir lieu : on laisse passer les touches
    // 3. vignettes des lignes visibles : 2 en vol au plus, relance après 20 images
    for (int k = 0; k < 2; k++)
        if (mini_dem_o[k] >= 0 && (listes[mini_dem_o[k]].mini[mini_dem_i[k]] || t - mini_dem_t[k] > 20)) mini_dem_o[k] = -1;
    int prem = defil / LIGNE_H, dern = (defil + LISTE_Y1 - LISTE_Y0) / LIGNE_H;
    for (int i = prem; i <= dern && i < l->n; i++)
    {
        if (l->mini[i]) continue;
        bool deja = false;
        int libre = -1;
        for (int k = 0; k < 2; k++)
        {
            if (mini_dem_o[k] == sel_o && mini_dem_i[k] == i) deja = true;
            if (mini_dem_o[k] < 0) libre = k;
        }
        if (deja) continue;
        if (libre < 0) break;
        envoyer("MINI %d %d", sel_o, i);
        mini_dem_o[libre] = sel_o; mini_dem_i[libre] = i; mini_dem_t[libre] = t;
    }

    // 4. les autres listes, puis les noms perdus en route
    for (int o = 0; o < 3; o++)
        if (listes[o].n < 0)
        {
            if (t - t_liste[o] > 90) { envoyer("LISTE %d", o); t_liste[o] = t; }
            return;
        }
    if (t - t_trous > 20)
    {
        t_trous = t;
        int demandes = 0;
        for (int k = 0; k < 3 && demandes < 3; k++)
        {
            int o = (sel_o + k) % 3;
            for (int i = 0; i < listes[o].n && demandes < 3; i++)
                if (!listes[o].ok[i]) { envoyer("ITMP %d %d", o, i); demandes++; }
        }
    }
}

// ------------------------------------------------------------ dessin des écrans
static u16 ecran_haut[LARG * HAUT] __attribute__((aligned(32)));
static u16 ecran_bas[LARG * HAUT] __attribute__((aligned(32)));
static u16 *vram_haut, *vram_bas;
static bool options_ouvertes = false;
static char options_msg[64] = "";
static int bouton_presse = -1;   // retour visuel quand on appuie

enum { Z_RIEN = -1, Z_JOUER = 100, Z_MENU, Z_OPTIONS, Z_OUBLIER, Z_JOURNAL, Z_FERMER, Z_LOUPE, Z_FERMER_RECH, Z_EFFACER };
#define Z_TOUCHE 200      // 200 + caractère : touches du clavier de recherche
#define Z_RESULTAT 400    // 400 + n : résultats de la recherche

static bool recherche_ouverte = false;
static char requete[32] = "";
static int resultats[MAX_EL], nb_resultats = 0;

// Minuscules sans accents, pour comparer « Pokemon » et « Pokémon »
static void normaliser(const char *s, char *out, int max)
{
    int n = 0, g;
    while ((g = car_suivant(&s)) >= 0 && n < max - 1)
    {
        int c = g < 95 ? g + 32 : g - 95 + 160;
        if (c >= 'A' && c <= 'Z') c += 32;
        else if (c >= 0xC0)
        {
            static const char base[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
            c = base[(c - 0xC0) & 31];
        }
        if (c < 128) out[n++] = c;
    }
    out[n] = 0;
}

static void filtrer(void)
{
    char q[48], nom[64];
    normaliser(requete, q, sizeof q);
    nb_resultats = 0;
    ListeDSi *l = &listes[sel_o];
    for (int i = 0; i < l->n; i++)
    {
        if (!l->ok[i]) continue;
        normaliser(l->noms[i], nom, sizeof nom);
        if (!q[0] || strstr(nom, q)) resultats[nb_resultats++] = i;
    }
}

static const char *rangees[4] = { "1234567890", "azertyuiop", "qsdfghjklm", "wxcvbn'-" };

static void etoile(u16 *e, int cx, int cy, int r, bool pleine, u16 c, int a);

static void dessiner_haut(void)
{
    u16 *e = ecran_haut;
    clip_y0 = 0; clip_y1 = HAUT;
    int si = sel_i[sel_o];
    bool jaq_ici = jaq_o == sel_o && jaq_i == si && sel_zone == 0;          // jaquette prête
    bool fond_ici = fond_ok && fond_o == sel_o && fond_i == si && sel_zone == 0;   // fond flou prêt

    // fond : flou de la jaquette, sinon dégradé bleu nuit (voile déjà appliqué)
    static bool defaut_pret = false;
    if (!defaut_pret)
    {
        for (int y = 0; y < HAUT; y++)
        {
            u16 c = C(0x060912 + ((y * 20 / HAUT) << 8) + (y * 40 / HAUT));
            for (int x = 0; x < LARG; x++)
                fond_defaut[y * LARG + x] = x < 200 ? melange(c, NUIT_, 170 * (200 - x) / 200) : c;
        }
        defaut_pret = true;
    }
    memcpy(e, fond_ici ? fond_flou : fond_defaut, sizeof ecran_haut);

    // en-tête
    texte(e, &police14, 10, 4, "wiil", BLANC_, 255);
    disque(e, 42, 12, 2, CIEL_, 255);
    char tmp[160];
    if (listes[sel_o].n > 0) snprintf(tmp, sizeof tmp, "%s  ·  %d / %d", noms_onglets[sel_o], si + 1, listes[sel_o].n);
    else snprintf(tmp, sizeof tmp, "%s", noms_onglets[sel_o]);
    texte(e, &police10, 52, 6, tmp, GRIS_, 255);
    time_t maintenant = time(NULL);
    struct tm *tm = localtime(&maintenant);
    snprintf(tmp, sizeof tmp, "%02d:%02d", tm->tm_hour, tm->tm_min);
    texte(e, &police11, 216, 5, tmp, BLANC_, 255);
    bool wifi = Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED;
    for (int k = 0; k < 3; k++)   // petites barres Wi-Fi
        rect(e, 196 + k * 4, 15 - k * 3, 3, 3 + k * 3, wifi ? CIEL_ : GRIS_, wifi ? 255 : 120);

    if (voir_journal)
    {
        rrect(e, 6, 24, 244, 162, 8, NUIT_, 220);
        texte(e, &police10, 12, 27, "Messages de débogage (Y pour fermer)", CIEL_, 255);
        for (int k = 0; k < journal_n; k++) texte_coupe(e, &police9, 12, 42 + k * 10, journal[k], 232, BLANC_, 255);
        return;
    }

    if (!wii_trouvee)
    {
        texte(e, &police20, 10, 66, "Recherche", BLANC_, 255);
        texte(e, &police20, 10, 88, "de la Wii...", BLANC_, 255);
        texte(e, &police10, 11, 118, "Allume Wiil sur la Wii et attends", GRIS_, 255);
        texte(e, &police10, 11, 131, "« Télécommande DSi prête ».", GRIS_, 255);
    }
    else
    {
        // jaquette (ou emplacement vide), à droite
        if (sel_zone == 0)
        {
            if (jaq_ici)
            {
                int x = 150 + (JAQ_L - jaq_w) / 2, y = 38 + (JAQ_H - jaq_h) / 2;
                rrect(e, x + 3, y + 4, jaq_w, jaq_h, 8, NUIT_, 110);   // ombre
                image(e, x, y, jaq, jaq_w, jaq_h, 7);
                rcontour(e, x, y, jaq_w, jaq_h, 7, BLANC_, 70);
            }
            else
            {
                rrect(e, 150, 38, JAQ_L, JAQ_H, 7, BLANC_, 18);
                texte(e, &police10, 182, 98, "...", GRIS_, 255);
            }
        }
        // titre sur 2 lignes maximum
        const char *titre = sel_zone == 0 && listes[sel_o].n > 0 && listes[sel_o].ok[si] ? listes[sel_o].noms[si] : sel_nom;
        if (!titre[0]) titre = listes[sel_o].n == 0 ? "Aucun élément" : "...";
        char lignes[2][80];
        int nl = couper_lignes(&police20, titre, sel_zone == 0 ? 134 : 230, lignes, 2);
        int y = nl == 1 ? 72 : 60;
        for (int k = 0; k < nl; k++) texte_coupe(e, &police20, 10, y + k * 22, lignes[k], sel_zone == 0 ? 134 : 230, BLANC_, 255);
        y += nl * 22 + 6;
        if (sel_zone == 0 && listes[sel_o].n > 0 && si < listes[sel_o].n && listes[sel_o].fav[si])
            etoile(e, 144, 11, 5, true, C(0xFACC15), 255);
        if (sel_zone == 0 && inf_o == sel_o && inf_i == si)
        {
            if (inf_id[0]) snprintf(tmp, sizeof tmp, "%s  ·  %s", inf_type, inf_id);
            else snprintf(tmp, sizeof tmp, "%s", inf_type);
            texte(e, &police10, 11, y, tmp, GRIS_, 255);
            y += 18;
            int x = 10;
            const char *pastilles[2] = { inf_annee, inf_joueurs };
            bool une = false;
            for (int k = 0; k < 2; k++)
            {
                if (!pastilles[k][0]) continue;
                int lw = largeur(&police10, pastilles[k]) + 12;
                if (x + lw > 144) break;
                rrect(e, x, y, lw, 16, 8, BLANC_, 40);
                texte(e, &police10, x + 6, y + 2, pastilles[k], BLANC_, 255);
                x += lw + 4;
                une = true;
            }
            if (une) y += 21;
            if (inf_genre[0] && inf_editeur[0]) snprintf(tmp, sizeof tmp, "%s  ·  %s", inf_genre, inf_editeur);
            else snprintf(tmp, sizeof tmp, "%s%s", inf_genre, inf_editeur);
            if (tmp[0]) texte_coupe(e, &police10, 11, y, tmp, 134, GRIS_, 255);
        }
        else if (sel_zone == 2) texte(e, &police10, 11, y, "Raccourci  ·  A pour l'ouvrir", GRIS_, 255);
        else if (sel_zone == 1) texte(e, &police10, 11, y, "Bouton de Wiil", GRIS_, 255);
    }

    // état de la connexion, en bas
    bool reponse_recente = wii_trouvee && images_t - derniere_reponse < 60 * 15;
    texte(e, &police9, 10, 178, wii_trouvee ? "Télécommande connectée" : "Recherche de la Wii", GRIS_, 255);
    disque(e, wii_trouvee ? 112 : 102, 183, 2, reponse_recente ? C(0x4ADE80) : C(0xFACC15), 255);
}

// Zones touchables de l'écran du bas
typedef struct { int x, y, w, h, id; } Zone;
static Zone zones[16];
static int nb_zones = 0;
static void zone_ajouter(int x, int y, int w, int h, int id)
{
    if (nb_zones < 16) zones[nb_zones++] = (Zone){ x, y, w, h, id };
}
static int zone_touchee(int x, int y)
{
    for (int k = nb_zones - 1; k >= 0; k--)
        if (x >= zones[k].x && x < zones[k].x + zones[k].w && y >= zones[k].y && y < zones[k].y + zones[k].h)
            return zones[k].id;
    return Z_RIEN;
}

static bool dans_etoile(float x, float y, float r)
{
    // étoile à 5 branches centrée en 0 : test « point dans polygone »
    static float px[10], py[10];
    static float r_calc = -1;
    if (r != r_calc)
    {
        for (int k = 0; k < 10; k++)
        {
            float rr = k % 2 == 0 ? r : r * 0.42f, a = 1.5708f + k * 0.62832f;
            px[k] = rr * cosf(a); py[k] = -rr * sinf(a);
        }
        r_calc = r;
    }
    bool dedans = false;
    for (int i = 0, j = 9; i < 10; j = i++)
        if (((py[i] > y) != (py[j] > y)) && (x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i]))
            dedans = !dedans;
    return dedans;
}

// Les calculs à virgule sont très lents sur la DSi : chaque étoile (taille, pleine ou
// contour) est calculée UNE fois en masque d'opacité, puis simplement recopiée.
#define ETOILE_MAX 10
typedef struct { int r; bool pleine; u8 masque[(2 * ETOILE_MAX + 3) * (2 * ETOILE_MAX + 3)]; } MasqueEtoile;
static MasqueEtoile masques[6];
static int nb_masques = 0;

static const MasqueEtoile *masque_etoile(int r, bool pleine)
{
    for (int k = 0; k < nb_masques; k++)
        if (masques[k].r == r && masques[k].pleine == pleine) return &masques[k];
    if (nb_masques == 6 || r > ETOILE_MAX) return NULL;
    MasqueEtoile *m = &masques[nb_masques++];
    m->r = r; m->pleine = pleine;
    int cote = 2 * r + 3;
    for (int y = -r - 1; y <= r + 1; y++)
        for (int x = -r - 1; x <= r + 1; x++)
        {
            int n = 0, nin = 0;
            for (int k = 0; k < 4; k++)
            {
                float sx = x + 0.25f + 0.5f * (k & 1), sy = y + 0.25f + 0.5f * (k >> 1);
                if (dans_etoile(sx, sy + 0.5f, r)) n++;
            }
            if (!pleine)
                for (int k = 0; k < 4; k++)
                {
                    float sx = x + 0.25f + 0.5f * (k & 1), sy = y + 0.25f + 0.5f * (k >> 1);
                    if (dans_etoile(sx, sy + 0.5f, r - 1.6f)) nin++;
                }
            m->masque[(y + r + 1) * cote + (x + r + 1)] = (n - nin) * 64;
        }
    return m;
}

static void etoile(u16 *e, int cx, int cy, int r, bool pleine, u16 c, int a)
{
    const MasqueEtoile *m = masque_etoile(r, pleine);
    if (!m) return;
    int cote = 2 * r + 3;
    for (int y = 0; y < cote; y++)
        for (int x = 0; x < cote; x++)
        {
            int cov = m->masque[y * cote + x];
            if (cov > 0) point(e, cx + x - r - 1, cy + y - r - 1, c, (a * cov) >> 8);
        }
}

static void bouton(u16 *e, int x, int y, int w, int h, const char *txt, bool principal, int id)
{
    bool presse = bouton_presse == id;
    if (principal)
    {
        rrect(e, x, y, w, h, h / 2, presse ? C(0xB8C4D4) : BLANC_, 255);
        triangle_lecture(e, x + 12, y + 5, h - 10, CIEL_);
        texte(e, &police13, x + 30, y + 3, txt, NUIT_, 255);
    }
    else
    {
        rrect(e, x, y, w, h, h / 2, BLANC_, presse ? 70 : 28);
        int lw = largeur(&police11, txt);
        texte(e, &police11, x + (w - lw) / 2, y + 4, txt, BLANC_, 255);
    }
    zone_ajouter(x, y, w, h, id);
}

static void dessiner_bas(void)
{
    u16 *e = ecran_bas;
    clip_y0 = 0; clip_y1 = HAUT;
    nb_zones = 0;
    for (int y = 0; y < HAUT; y++)
    {
        u16 c = C(0x0A1224 + (((10 * y / HAUT)) << 8) + (16 * y / HAUT));
        for (int x = 0; x < LARG; x++) e[y * LARG + x] = c;
    }

    // onglets
    int x = 8;
    for (int o = 0; o < 3; o++)
    {
        int lw = largeur(&police12, noms_onglets[o]) + 20;
        bool actif = o == sel_o;
        rrect(e, x, 5, lw, 20, 10, actif ? CIEL_ : BLANC_, actif ? 255 : 22);
        texte(e, &police12, x + 10, 7, noms_onglets[o], actif ? NUIT_ : GRIS_, 255);
        zone_ajouter(x, 3, lw, 24, o);   // id 0..2 = onglets
        x += lw + 6;
    }

    // liste qui défile
    ListeDSi *l = &listes[sel_o];
    clip_y0 = LISTE_Y0; clip_y1 = LISTE_Y1;
    if (l->n < 0) texte(e, &police12, 12, LISTE_Y0 + 6, "Chargement de la liste...", GRIS_, 255);
    else if (l->n == 0) texte(e, &police12, 12, LISTE_Y0 + 6, "Rien dans cet onglet", GRIS_, 255);
    for (int i = 0; i < l->n; i++)
    {
        int y = LISTE_Y0 + i * LIGNE_H - defil;
        if (y + LIGNE_H < LISTE_Y0 || y > LISTE_Y1) continue;
        bool sel = i == sel_i[sel_o] && sel_zone == 0;
        if (sel)
        {
            rrect(e, 6, y, 244, LIGNE_H - 2, 8, CIEL_, 60);
            rcontour(e, 6, y, 244, LIGNE_H - 2, 8, CIEL_, 255);
        }
        if (l->mini[i]) image(e, 12, y + 2, l->mini[i], MINI_C, MINI_C, 4);
        else rrect(e, 12, y + 2, MINI_C, MINI_C, 4, BLANC_, 25);
        texte_coupe(e, &police12, 40, y + 5, l->ok[i] ? l->noms[i] : "...", 180, sel ? BLANC_ : C(0xCDD6E4), 255);
        if (l->ok[i]) etoile(e, 235, y + 11, 7, l->fav[i], l->fav[i] ? C(0xFACC15) : BLANC_, l->fav[i] ? 255 : 70);
    }
    clip_y0 = 0; clip_y1 = HAUT;
    int total = l->n * LIGNE_H, vis = LISTE_Y1 - LISTE_Y0;
    if (total > vis)
    {   // barre de défilement
        rrect(e, 251, LISTE_Y0 + 2, 3, vis - 4, 1, BLANC_, 30);
        int th = (vis - 4) * vis / total, ty = LISTE_Y0 + 2 + (vis - 4 - th) * defil / (total - vis);
        rrect(e, 251, ty, 3, th, 1, CIEL_, 255);
    }
    zone_ajouter(0, LISTE_Y0, 250, vis, 50);   // id 50 = la liste

    // boutons du bas
    bouton(e, 6, 166, 114, 22, "Jouer", true, Z_JOUER);
    bouton(e, 126, 166, 64, 22, "Menu Wii", false, Z_MENU);
    bouton(e, 196, 166, 54, 22, "Options", false, Z_OPTIONS);

    // loupe (recherche), à droite des onglets
    rrect(e, 222, 5, 28, 20, 10, BLANC_, bouton_presse == Z_LOUPE ? 70 : 22);
    for (int k = 0; k < 360; k += 20)
    {
        float a = k * 0.01745f;
        point(e, 234 + (int)(4.5f * cosf(a)), 13 + (int)(4.5f * sinf(a)), BLANC_, 255);
    }
    for (int k = 0; k < 4; k++) { point(e, 238 + k, 17 + k, BLANC_, 255); point(e, 239 + k, 17 + k, BLANC_, 200); }
    zone_ajouter(220, 3, 34, 24, Z_LOUPE);

    if (recherche_ouverte)
    {
        nb_zones = 0;
        for (int y = 0; y < HAUT; y++) for (int x = 0; x < LARG; x++) e[y * LARG + x] = C(0x0A1224);
        // champ de saisie
        rrect(e, 6, 4, 196, 22, 11, BLANC_, 240);
        char aff[48];
        snprintf(aff, sizeof aff, "%s%s", requete, (images_t / 30) % 2 ? "|" : "");
        if (requete[0]) texte(e, &police12, 16, 7, aff, NUIT_, 255);
        else texte(e, &police12, 16, 7, "Rechercher...", C(0x8090A8), 255);
        bouton(e, 206, 4, 44, 22, "Fermer", false, Z_FERMER_RECH);
        // résultats (3 premiers)
        char nb[48];
        snprintf(nb, sizeof nb, nb_resultats > 1 ? "%d résultats dans %s" : "%d résultat dans %s", nb_resultats, noms_onglets[sel_o]);
        texte(e, &police9, 10, 29, nb, GRIS_, 255);
        for (int k = 0; k < 3 && k < nb_resultats; k++)
        {
            int i = resultats[k], y = 40 + k * 20;
            rrect(e, 6, y, 244, 18, 8, BLANC_, bouton_presse == Z_RESULTAT + k ? 60 : 16);
            if (listes[sel_o].fav[i]) etoile(e, 15, y + 9, 5, true, C(0xFACC15), 255);
            texte_coupe(e, &police11, 24, y + 2, listes[sel_o].noms[i], 220, BLANC_, 255);
            zone_ajouter(6, y, 244, 18, Z_RESULTAT + k);
        }
        // clavier AZERTY
        for (int r = 0; r < 4; r++)
        {
            int n = strlen(rangees[r]), kw = 24, x0 = (LARG - n * (kw + 1)) / 2, y = 102 + r * 20;
            if (r == 3) x0 = 6;
            for (int k = 0; k < n; k++)
            {
                char t[2] = { rangees[r][k], 0 };
                int id = Z_TOUCHE + (unsigned char)t[0], x = x0 + k * (kw + 1);
                rrect(e, x, y, kw, 18, 5, BLANC_, bouton_presse == id ? 90 : 30);
                int lw = largeur(&police12, t);
                texte(e, &police12, x + (kw - lw) / 2, y + 1, t, BLANC_, 255);
                zone_ajouter(x, y, kw, 18, id);
            }
            if (r == 3)
            {   // effacer
                int x = x0 + n * (kw + 1);
                rrect(e, x, y, LARG - 6 - x, 18, 5, BLANC_, bouton_presse == Z_EFFACER ? 90 : 30);
                texte(e, &police11, x + 10, y + 2, "Effacer", BLANC_, 255);
                zone_ajouter(x, y, LARG - 6 - x, 18, Z_EFFACER);
            }
        }
        // espace
        rrect(e, 60, 182, 136, 9, 4, BLANC_, bouton_presse == Z_TOUCHE + ' ' ? 90 : 30);
        zone_ajouter(60, 180, 136, 12, Z_TOUCHE + ' ');
        return;
    }

    if (options_ouvertes)
    {
        nb_zones = 0;   // seul le panneau est touchable
        rect(e, 0, 0, LARG, HAUT, NUIT_, 170);
        rrect(e, 16, 18, 224, 156, 12, C(0x14203A), 255);
        rcontour(e, 16, 18, 224, 156, 12, CIEL_, 90);
        texte(e, &police14, 30, 26, "Options", BLANC_, 255);
        bouton(e, 30, 52, 196, 24, "Oublier le réseau Wi-Fi", false, Z_OUBLIER);
        bouton(e, 30, 82, 196, 24, voir_journal ? "Messages de débogage : oui" : "Messages de débogage : non", false, Z_JOURNAL);
        if (options_msg[0]) texte(e, &police10, 32, 114, options_msg, C(0x4ADE80), 255);
        bouton(e, 30, 140, 196, 24, "Fermer", false, Z_FERMER);
    }
}

static void afficher(void)
{
    // un seul écran par image : le bas d'abord (le tactile doit rester réactif)
    bool bas = bas_a_redessiner, haut = !bas && haut_a_redessiner;
    if (bas) dessiner_bas();
    if (haut) dessiner_haut();
    lire_reseau();
    attendre_image();
    if (haut) { DC_FlushRange(ecran_haut, sizeof ecran_haut); dmaCopy(ecran_haut, vram_haut, sizeof ecran_haut); haut_a_redessiner = false; }
    if (bas) { DC_FlushRange(ecran_bas, sizeof ecran_bas); dmaCopy(ecran_bas, vram_bas, sizeof ecran_bas); bas_a_redessiner = false; }
}

static volatile int vbl_compteur = 0;   // compte les images (mesure de fluidité)
static void sur_vbl(void) { vbl_compteur++; }

static void passer_en_graphique(void)
{
    irqSet(IRQ_VBLANK, sur_vbl);
    irqEnable(IRQ_VBLANK);
    mode_graphique = true;
    // Écran du haut : image 16 bits dans la banque B (la console texte reste dans la banque A)
    videoSetMode(MODE_5_2D);
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
    int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 8, 0);
    vram_haut = bgGetGfxPtr(bg);
    // Écran du bas : image 16 bits dans la banque C (le clavier n'est plus utile)
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    int bgs = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vram_bas = bgGetGfxPtr(bgs);
    haut_a_redessiner = bas_a_redessiner = true;
}

// ------------------------------------------------------------ actions
static void choisir_onglet(int o)
{
    if (o == sel_o) return;
    sel_o = o;
    defil = defil_cible = 0;
    rendre_visible(sel_i[o]);
    envoyer_ordre("CHOISIR %d %d", o, sel_i[o]);
    haut_a_redessiner = bas_a_redessiner = true;
}

static void action(int id, int tx, int ty)
{
    if (id >= 0 && id <= 2) choisir_onglet(id);
    else if (id == 50)
    {
        int i = (ty - LISTE_Y0 + defil) / LIGNE_H;
        if (i >= 0 && i < listes[sel_o].n && tx >= 222 && listes[sel_o].ok[i])
        {   // l'étoile : ajouter / retirer des favoris
            listes[sel_o].fav[i] = !listes[sel_o].fav[i];
            envoyer_ordre("FAV %d %d", sel_o, i);
            haut_a_redessiner = true;
        }
        else if (i >= 0 && i < listes[sel_o].n)
        {
            sel_i[sel_o] = i;
            sel_zone = 0;
            envoyer_ordre("CHOISIR %d %d", sel_o, i);
            t_info = t_img = -999;
            haut_a_redessiner = bas_a_redessiner = true;
        }
    }
    else if (id == Z_JOUER) envoyer_ordre("TOUCHE JOUER");
    else if (id == Z_LOUPE) { recherche_ouverte = true; requete[0] = 0; filtrer(); }
    else if (id == Z_FERMER_RECH) recherche_ouverte = false;
    else if (id == Z_EFFACER)
    {
        int n = strlen(requete);
        if (n) requete[n - 1] = 0;
        filtrer();
    }
    else if (id >= Z_TOUCHE && id < Z_TOUCHE + 256)
    {
        int n = strlen(requete);
        if (n < (int)sizeof requete - 1) { requete[n] = id - Z_TOUCHE; requete[n + 1] = 0; }
        filtrer();
    }
    else if (id >= Z_RESULTAT && id < Z_RESULTAT + 3 && id - Z_RESULTAT < nb_resultats)
    {
        int i = resultats[id - Z_RESULTAT];
        sel_i[sel_o] = i;
        sel_zone = 0;
        rendre_visible(i);
        envoyer_ordre("CHOISIR %d %d", sel_o, i);
        recherche_ouverte = false;
        haut_a_redessiner = true;
    }
    else if (id == Z_MENU) envoyer_ordre("TOUCHE HOME");
    else if (id == Z_OPTIONS) { options_ouvertes = true; options_msg[0] = 0; }
    else if (id == Z_FERMER) options_ouvertes = false;
    else if (id == Z_JOURNAL) { voir_journal = !voir_journal; haut_a_redessiner = true; }
    else if (id == Z_OUBLIER)
    {
        cfg_oublier();
        snprintf(options_msg, sizeof options_msg, "Réseau oublié : mot de passe redemandé");
    }
    bas_a_redessiner = true;
}

int main(int argc, char *argv[])
{
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&haut, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    consoleInit(&bas, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 3, false, true);
    Keyboard *clavier = keyboardDemoInit();
    clavier->OnKeyPressed = touche_clavier;
    consoleArm7Setup(&haut, 1024);
    for (int o = 0; o < 3; o++) listes[o].n = -1;

    DEBUG("=== Wiil DSi ===\n");
    DEBUG("Console : %s\n", isDSiMode() ? "mode DSi (WPA2 OK)" : "mode DS (pas de WPA2 !)");
    carte_sd_init();

    if (!Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE))
    {
        DEBUG("ERREUR : Wi-Fi impossible a demarrer\n");
        goto fin;
    }
    DEBUG("Wi-Fi demarre.\n");

    // 1) d'abord le réseau mémorisé, s'il y en a un
    bool connecte = false;
    Wifi_EnableWifi();
    if (cfg_lire())
        connecte = connecter_reseau_memorise() && attendre_connexion();

    // 2) sinon, menu de connexion
    while (!connecte)
    {
        Wifi_EnableWifi();
        consoleSelect(&bas);
        consoleClear();
        int nb = Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, NULL);
        printf("Connexion Wi-Fi :\n\n");
        printf("A : reseau enregistre dans la\n    DSi (%d trouve%s)\n", nb, nb > 1 ? "s" : "");
        printf("B : chercher un reseau et\n    taper le mot de passe\n");

        int choix = 0;
        while (!choix)
        {
            attendre_image();
            scanKeys();
            if (keysDown() & KEY_A) choix = 1;
            if (keysDown() & KEY_B) choix = 2;
        }
        consoleClear();
        if (choix == 1) { DEBUG("Reseaux enregistres...\n"); Wifi_AutoConnect(); }
        else connecter_reseau_choisi();

        connecte = attendre_connexion();
        if (connecte && choix == 2)
            cfg_ecrire(point_acces.ssid, mdp_tape);   // la prochaine fois : automatique
    }

    // --- Connecté
    {
        struct in_addr passerelle, masque, dns1, dns2;
        struct in_addr ip = Wifi_GetIPInfo(&passerelle, &masque, &dns1, &dns2);
        // « Associated » peut arriver avant que la box ait donné l'adresse (IP 0.0.0.0) :
        // on attend la vraie adresse, 10 s au plus, sinon le 1er message part dans le vide
        for (int k = 0; k < 600 && (ip.s_addr == 0 || masque.s_addr == 0); k++)
        {
            attendre_image();
            ip = Wifi_GetIPInfo(&passerelle, &masque, &dns1, &dns2);
        }
        if (masque.s_addr == 0) masque.s_addr = 0x00FFFFFF;   // 255.255.255.0 par défaut
        DEBUG("Connecte ! IP de la DSi : %s\n", inet_ntoa(ip));
        memset(&dest, 0, sizeof dest);
        dest.sin_family = AF_INET;
        dest.sin_port = htons(PORT);
        dest.sin_addr.s_addr = ip.s_addr | ~masque.s_addr;   // broadcast pour trouver la Wii
    }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        DEBUG("ERREUR socket : %d\n", errno);
        goto fin;
    }
    {
        int oui = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &oui, sizeof oui);
        ioctl(sock, FIONBIO, (char *)&oui);
    }

    passer_en_graphique();
    keysSetRepeat(20, 6);
    bool appui = false, glisse = false;
    int tx0 = 0, ty0 = 0, defil_depart = 0, minute = -1;

    u32 k_tard = 0, kd_tard = 0;    // appuis vus pendant le tour précédent, après le dessin
    while (1)
    {
        images_t++;
        static int vbl_avant = 0;
        int vbl = vbl_compteur;                 // images réellement affichées (60 par seconde)
        static int pire = 0, t_pire = 0;     // au plus une ligne par seconde : le pire tour
        if (vbl_avant && vbl - vbl_avant > pire) pire = vbl - vbl_avant;
        if (vbl - t_pire >= 60)
        {
            if (pire > 6 && images_t > 120) DEBUG("Tour lent : %d ms\n", pire * 1000 / 60);   // seulement > 100 ms
            pire = 0; t_pire = vbl;
        }
        vbl_avant = vbl;
        scanKeys();
        u32 k = keysDownRepeat() | k_tard, kd = keysDown() | kd_tard;
        k_tard = kd_tard = 0;

        // boutons de la DSi : comme la Wiimote
        if (k & KEY_LEFT)  envoyer_ordre("TOUCHE GAUCHE");
        if (k & KEY_RIGHT) envoyer_ordre("TOUCHE DROITE");
        if (k & KEY_UP)    envoyer_ordre("TOUCHE HAUT");
        if (k & KEY_DOWN)  envoyer_ordre("TOUCHE BAS");
        if (kd & KEY_A)     envoyer_ordre("TOUCHE A");
        if (kd & KEY_B)
        {
            if (options_ouvertes || recherche_ouverte) { options_ouvertes = recherche_ouverte = false; bas_a_redessiner = true; }
            else envoyer_ordre("TOUCHE B");     // B = ajouter / retirer des favoris (comme sur la Wiimote)
        }
        if (recherche_ouverte && images_t % 30 == 0) bas_a_redessiner = true;   // curseur qui clignote
        if (kd & KEY_L)     envoyer_ordre("TOUCHE MOINS");
        if (kd & KEY_R)     envoyer_ordre("TOUCHE PLUS");
        if (kd & KEY_START) envoyer_ordre("TOUCHE HOME");
        if (kd & KEY_SELECT) { options_ouvertes = !options_ouvertes; options_msg[0] = 0; bas_a_redessiner = true; }
        if (kd & KEY_Y)     { voir_journal = !voir_journal; haut_a_redessiner = true; }

        // écran tactile : toucher = choisir, glisser = faire défiler la liste
        if (keysHeld() & KEY_TOUCH)
        {
            touchPosition tp;
            touchRead(&tp);
            if (!appui)
            {
                appui = true; glisse = false;
                tx0 = tp.px; ty0 = tp.py; defil_depart = defil_cible;
                int id = zone_touchee(tx0, ty0);
                if (id >= Z_JOUER) { bouton_presse = id; bas_a_redessiner = true; }
            }
            else if (zone_touchee(tx0, ty0) == 50 && !options_ouvertes && !recherche_ouverte)
            {
                int dy = tp.py - ty0;
                if (dy > 5 || dy < -5) glisse = true;
                if (glisse)
                {
                    int max = listes[sel_o].n * LIGNE_H - (LISTE_Y1 - LISTE_Y0);
                    defil_cible = defil_depart - dy;
                    if (defil_cible > max) defil_cible = max;
                    if (defil_cible < 0) defil_cible = 0;
                    defil = defil_cible;
                    bas_a_redessiner = true;
                }
            }
        }
        else if (appui)
        {
            appui = false;
            if (bouton_presse >= 0) { bouton_presse = -1; bas_a_redessiner = true; }
            if (!glisse) action(zone_touchee(tx0, ty0), tx0, ty0);
        }

        // défilement en douceur vers la sélection
        if (defil != defil_cible)
        {
            int d = (defil_cible - defil) / 3;
            if (d == 0) d = defil_cible > defil ? 1 : -1;
            defil += d;
            bas_a_redessiner = true;
        }

        lire_reseau();
        relancer_ordres();
        avancer_fond();                       // le fond flou se calcule petit à petit
        demander();
        lire_reseau();

        // l'heure et l'état de connexion changent de temps en temps
        time_t maintenant = time(NULL);
        struct tm *tm = localtime(&maintenant);
        if (tm->tm_min != minute) { minute = tm->tm_min; haut_a_redessiner = true; }
        static int wifi_avant = -1, vert_avant = -1;
        int wifi_maint = Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED;
        int vert_maint = wii_trouvee && images_t - derniere_reponse < 60 * 15;
        if (wifi_maint != wifi_avant || vert_maint != vert_avant) { wifi_avant = wifi_maint; vert_avant = vert_maint; haut_a_redessiner = true; }

        afficher();
    }

fin:
    DEBUG("\nFin. START pour quitter.\n");
    while (1)
    {
        attendre_image();
        scanKeys();
        if (keysDown() & KEY_START)
            break;
    }
    return 0;
}

# Images de l'interface de Wiil (toutes dessinées ici, design original).
import os, sys, math
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
S = 4
BLANC = (240, 244, 250); CIEL = (56, 189, 248); NUIT = (6, 9, 18)


def save(img, nom):
    img.save(os.path.join(OUT, nom + '.png'))


# Dégradés d'ombre (noir -> transparent) pour la lisibilité
def ombre_gauche():
    im = Image.new('RGBA', (64, 8))
    for x in range(64):
        a = int(200 * max(0.0, 1 - x / 40) ** 1.6)
        for y in range(8):
            im.putpixel((x, y), NUIT + (a,))
    return im


def ombre_bas():
    im = Image.new('RGBA', (8, 64))
    for y in range(64):
        a = int(225 * max(0.0, (y - 34) / 30) ** 1.4)
        for x in range(8):
            im.putpixel((x, y), NUIT + (a,))
    return im


def ombre_haut():
    im = Image.new('RGBA', (8, 32))
    for y in range(32):
        a = int(170 * (1 - y / 32) ** 1.5)
        for x in range(8):
            im.putpixel((x, y), NUIT + (a,))
    return im


save(ombre_gauche(), 'ombre_gauche'); save(ombre_bas(), 'ombre_bas'); save(ombre_haut(), 'ombre_haut')


def arrondi(w, h, r, fill=None, outline=None, width=0):
    im = Image.new('RGBA', (w * S, h * S), (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle((width * S // 2, width * S // 2, w * S - 1 - width * S // 2, h * S - 1 - width * S // 2), r * S,
                                         fill=fill, outline=outline, width=width * S)
    return im.resize((w, h), Image.LANCZOS)


# Cadre de sélection (blanc) et halo
save(arrondi(128, 128, 14, outline=BLANC + (255,), width=4), 'cadre')
halo = Image.new('RGBA', (160, 160), (0, 0, 0, 0))
ImageDraw.Draw(halo).rounded_rectangle((16, 16, 143, 143), 18, fill=CIEL + (170,))
save(halo.filter(ImageFilter.GaussianBlur(9)), 'halo')
# Masque de coins arrondis : 4 coins noirs à superposer sur une jaquette carrée (couleur du fond assombri)
coins = Image.new('RGBA', (128 * S, 128 * S), NUIT + (255,))
ImageDraw.Draw(coins).rounded_rectangle((0, 0, 128 * S - 1, 128 * S - 1), 14 * S, fill=(0, 0, 0, 0))
save(coins.resize((128, 128), Image.LANCZOS), 'coins')
# Tuile vide (fond des applis)
tuile = Image.new('RGBA', (128, 128))
d = ImageDraw.Draw(tuile)
for y in range(128):
    k = y / 128
    d.line([(0, y), (128, y)], fill=(int(40 - 18 * k), int(56 - 22 * k), int(84 - 30 * k), 255))
m = arrondi(128, 128, 14, fill=(255, 255, 255, 255)).split()[3]
tuile.putalpha(m)
save(tuile, 'tuile')
# Carte du bas (dégradé neutre teinté par le code)
carte = Image.new('RGBA', (176, 96))
d = ImageDraw.Draw(carte)
for y in range(96):
    v = int(255 * (1.0 - 0.45 * y / 96))
    d.line([(0, y), (176, y)], fill=(v, v, v, 255))
carte.putalpha(arrondi(176, 96, 12, fill=(255, 255, 255, 255)).split()[3])
save(carte, 'carte')
save(arrondi(176, 96, 12, outline=BLANC + (255,), width=3), 'carte_cadre')
# Boutons
# NB : sur Wii, chaque image doit avoir une largeur et une hauteur multiples de 4.
save(arrondi(152, 48, 24, fill=BLANC + (255,)), 'bouton_jouer')
rond = Image.new('RGBA', (48 * S, 48 * S), (0, 0, 0, 0))
dr = ImageDraw.Draw(rond)
dr.ellipse((S, S, 47 * S, 47 * S), fill=(30, 42, 64, 235), outline=(170, 180, 195, 255), width=2 * S)
for k in range(3):
    cx = (14 + k * 10) * S
    dr.ellipse((cx - 3 * S, 21 * S, cx + 3 * S, 27 * S), fill=BLANC + (255,))
save(rond.resize((48, 48), Image.LANCZOS), 'bouton_options')
# Point bleu du logo
p = Image.new('RGBA', (12 * S, 12 * S), (0, 0, 0, 0))
ImageDraw.Draw(p).ellipse((S, S, 11 * S, 11 * S), fill=CIEL + (255,))
save(p.resize((12, 12), Image.LANCZOS), 'point')
# Avatar
av = Image.new('RGBA', (36 * S, 36 * S), (0, 0, 0, 0))
ImageDraw.Draw(av).ellipse((S, S, 35 * S, 35 * S), fill=(40, 70, 120, 255), outline=CIEL + (255,), width=2 * S)
save(av.resize((36, 36), Image.LANCZOS), 'avatar')
# Soulignement d'onglet
save(arrondi(40, 4, 2, fill=CIEL + (255,)), 'souligne')


# Icônes blanches simples (64x64) pour les tuiles système
def icone(nom, dessin):
    t = 64 * S
    im = Image.new('RGBA', (t, t), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    dessin(d, t / 2, t / 64)
    save(im.resize((64, 64), Image.LANCZOS), 'ic_' + nom)


B = BLANC + (255,)


def disque(d, c, u):
    d.ellipse((c - 22 * u, c - 22 * u, c + 22 * u, c + 22 * u), outline=B, width=int(4 * u))
    d.ellipse((c - 5 * u, c - 5 * u, c + 5 * u, c + 5 * u), fill=B)


def menuwii(d, c, u):
    d.rounded_rectangle((c - 24 * u, c - 16 * u, c + 24 * u, c + 16 * u), 5 * u, outline=B, width=int(4 * u))
    for i in range(3):
        d.rounded_rectangle((c - 16 * u + i * 12 * u, c - 8 * u, c - 8 * u + i * 12 * u, c + 8 * u), 2 * u, fill=B)


def maison(d, c, u):
    d.line([(c - 20 * u, c), (c, c - 18 * u), (c + 20 * u, c)], fill=B, width=int(4 * u), joint='curve')
    d.line([(c - 13 * u, c - 5 * u), (c - 13 * u, c + 17 * u), (c + 13 * u, c + 17 * u), (c + 13 * u, c - 5 * u)], fill=B, width=int(4 * u), joint='curve')


def grille(d, c, u):
    for i in range(2):
        for j in range(2):
            d.rounded_rectangle((c - 20 * u + i * 22 * u, c - 20 * u + j * 22 * u, c - 2 * u + i * 22 * u, c - 2 * u + j * 22 * u), 4 * u, outline=B, width=int(4 * u))


def flux(d, c, u):
    for k, h in enumerate((16, 26, 16)):
        x = c - 16 * u + k * 16 * u
        d.rounded_rectangle((x - 5 * u, c - h * u / 2 - 4 * u, x + 5 * u, c + h * u / 2 + 4 * u), 3 * u, fill=B)


def eteindre(d, c, u):
    d.arc((c - 20 * u, c - 18 * u, c + 20 * u, c + 22 * u), 300, 240, fill=B, width=int(4 * u))
    d.line([(c, c - 22 * u), (c, c)], fill=B, width=int(4 * u))


def manette(d, c, u):
    d.rounded_rectangle((c - 24 * u, c - 12 * u, c + 24 * u, c + 14 * u), 12 * u, outline=B, width=int(4 * u))
    d.line([(c - 16 * u, c + 1 * u), (c - 6 * u, c + 1 * u)], fill=B, width=int(4 * u))
    d.line([(c - 11 * u, c - 4 * u), (c - 11 * u, c + 6 * u)], fill=B, width=int(4 * u))
    for x, y in ((c + 9 * u, c - 3 * u), (c + 16 * u, c + 4 * u)):
        d.ellipse((x - 3 * u, y - 3 * u, x + 3 * u, y + 3 * u), fill=B)


for nom, f in (('disque', disque), ('menuwii', menuwii), ('maison', maison), ('grille', grille),
               ('flux', flux), ('eteindre', eteindre), ('manette', manette)):
    icone(nom, f)

# Jaquettes de démonstration (pour tester sans carte SD) : aplats de couleur, aucun visuel de jeu
for i, col in enumerate([(110, 70, 240), (220, 50, 60), (20, 170, 120), (240, 140, 30), (40, 120, 230), (200, 60, 160)]):
    im = Image.new('RGB', (160, 224))
    d = ImageDraw.Draw(im)
    for y in range(224):
        k = y / 224
        d.line([(0, y), (160, y)], fill=tuple(int(c * (1.15 - 0.6 * k)) for c in col))
    for r in range(6):
        d.ellipse((100 - r * 22, 40 - r * 18, 200 + r * 22, 140 + r * 18), outline=(255, 255, 255), width=1)
    save(im.convert('RGBA'), f'demo{i}')
print('ok', len(os.listdir(OUT)))

# Étoile des favoris (16x16, jaune doré, contour sombre pour rester lisible sur une jaquette)
def etoile():
    t = 16 * S
    im = Image.new('RGBA', (t, t), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pts = []
    for k in range(10):
        r = (7.4 if k % 2 == 0 else 3.2) * S
        a = math.pi / 2 + k * math.pi / 5
        pts.append((t / 2 + r * math.cos(a), t / 2 - r * math.sin(a) + 0.4 * S))
    d.polygon(pts, fill=(250, 204, 21, 255), outline=(30, 24, 8, 255), width=int(1.2 * S))
    return im.resize((16, 16), Image.LANCZOS)


save(etoile(), 'etoile')
print('etoile ok')

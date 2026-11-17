# Génère source/police.h : la police Exo 2 (OFL) pré-dessinée pour la DSi.
# Caractères : ASCII 32-126 + Latin-1 160-255 (accents français), plusieurs tailles.
# Chaque glyphe est stocké en niveaux d'opacité (0-255), dessinés avec mélange sur l'écran.
import sys
from PIL import Image, ImageDraw, ImageFont

TTF, SORTIE = sys.argv[1], sys.argv[2]
TAILLES = [9, 10, 11, 12, 13, 14, 20]
CARS = list(range(32, 127)) + list(range(160, 256))

lignes = ["// Fichier généré par outils/gen_police.py — ne pas modifier à la main.",
          "// Police Exo 2 (SIL Open Font License), pré-dessinée pour la DSi.",
          "#pragma once", "#include <stdint.h>", "",
          "typedef struct { uint8_t w, h; int8_t x, y; uint8_t av; uint32_t off; } Glyphe;",
          "typedef struct { uint8_t taille, hauteur, ascent; const Glyphe *g; const uint8_t *alpha; } Police;",
          "#define NB_GLYPHES %d" % len(CARS), ""]
polices = []
for t in TAILLES:
    f = ImageFont.truetype(TTF, t)
    asc, desc = f.getmetrics()
    glyphes, alpha = [], []
    for cp in CARS:
        ch = chr(cp)
        av = round(f.getlength(ch))
        bb = f.getbbox(ch)
        w, h = max(0, bb[2] - bb[0]), max(0, bb[3] - bb[1])
        if ch == ' ' or w == 0 or h == 0:
            glyphes.append((0, 0, 0, 0, av, len(alpha)))
            continue
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).text((-bb[0], -bb[1]), ch, font=f, fill=255)
        glyphes.append((w, h, bb[0], bb[1], av, len(alpha)))
        alpha.extend(im.tobytes())
    n = "p%d" % t
    lignes.append("static const Glyphe %s_g[NB_GLYPHES] = {" % n)
    for g in glyphes:
        lignes.append("  {%d,%d,%d,%d,%d,%d}," % g)
    lignes.append("};")
    lignes.append("static const uint8_t %s_a[%d] = {" % (n, max(1, len(alpha))))
    for i in range(0, len(alpha), 32):
        lignes.append("  " + ",".join(str(b) for b in alpha[i:i + 32]) + ",")
    if not alpha:
        lignes.append("  0")
    lignes.append("};")
    lignes.append("static const Police police%d = { %d, %d, %d, %s_g, %s_a };" % (t, t, asc + desc, asc, n, n))
    lignes.append("")
open(SORTIE, 'w', encoding='utf-8').write("\n".join(lignes))
print('ok', SORTIE)

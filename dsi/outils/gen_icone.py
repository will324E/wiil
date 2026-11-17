# Icône 32x32 (16 couleurs max) de Wiil DSi pour le menu de la console / TWiLight Menu++.
# Design original : carré bleu nuit arrondi, « w » blanc et point bleu ciel, comme Wiil.
import sys
from PIL import Image, ImageDraw, ImageFont

TTF, SORTIE = sys.argv[1], sys.argv[2]
S = 8
im = Image.new('RGB', (32 * S, 32 * S), (255, 0, 255))      # magenta = transparent
d = ImageDraw.Draw(im)
d.rounded_rectangle((S, S, 31 * S - 1, 31 * S - 1), 7 * S, fill=(14, 28, 58))
d.rounded_rectangle((S, 18 * S, 31 * S - 1, 31 * S - 1), 7 * S, fill=(10, 20, 44))
d.rectangle((S, 18 * S, 31 * S - 1, 22 * S), fill=(10, 20, 44))
f = ImageFont.truetype(TTF, 22 * S)
d.text((5 * S, -1 * S), "w", font=f, fill=(240, 244, 250))
d.ellipse((22 * S, 7 * S, 27 * S, 12 * S), fill=(56, 189, 248))
# petit écran double, clin d'œil à la DSi
d.rounded_rectangle((7 * S, 26 * S, 25 * S, 28 * S), 1 * S, fill=(56, 189, 248))
petit = im.resize((32, 32), Image.LANCZOS)
# le magenta transparent doit rester pur sur les bords
px = petit.load()
for y in range(32):
    for x in range(32):
        r, g, b = px[x, y]
        if r > 150 and b > 150 and g < 120:
            px[x, y] = (255, 0, 255)
q = petit.quantize(colors=16, method=Image.MEDIANCUT, dither=Image.NONE)
pal = q.getpalette()[:48]
# la couleur transparente doit être l'index 0
idx = None
for i in range(16):
    if pal[i * 3:i * 3 + 3] == [255, 0, 255]:
        idx = i
if idx not in (None, 0):
    data = list(q.getdata())
    data = [0 if v == idx else (idx if v == 0 else v) for v in data]
    pal[0:3], pal[idx * 3:idx * 3 + 3] = pal[idx * 3:idx * 3 + 3], pal[0:3]
    q = Image.new('P', (32, 32))
    q.putdata(data)
    q.putpalette(pal + [0] * (768 - len(pal)))
q.save(SORTIE, transparency=0)
im.resize((128, 128), Image.NEAREST).save(SORTIE.replace('.gif', '_apercu.png'))
print('ok')

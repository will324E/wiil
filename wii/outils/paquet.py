# Prépare le dossier apps/wiil (icône de la Chaîne Homebrew + meta.xml).
import os, sys
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]; POLICE = sys.argv[2]
os.makedirs(OUT, exist_ok=True)
S = 4
im = Image.new('RGBA', (128 * S, 48 * S))
d = ImageDraw.Draw(im)
for y in range(48 * S):
    k = y / (48 * S)
    d.line([(0, y), (128 * S, y)], fill=(int(8 + 14 * k), int(14 + 30 * k), int(30 + 50 * k), 255))
m = Image.new('L', im.size, 0)
ImageDraw.Draw(m).rounded_rectangle((0, 0, 128 * S - 1, 48 * S - 1), 8 * S, fill=255)
im.putalpha(m)
f = ImageFont.truetype(POLICE, 30 * S)
d.text((22 * S, 4 * S), 'wiil', font=f, fill=(240, 244, 250, 255))
d.ellipse((92 * S, 15 * S, 99 * S, 22 * S), fill=(56, 189, 248, 255))
im.resize((128, 48), Image.LANCZOS).save(os.path.join(OUT, 'icon.png'))
open(os.path.join(OUT, 'meta.xml'), 'w', encoding='utf-8').write('''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
	<name>Wiil</name>
	<coder>Wiil</coder>
	<version>1.0</version>
	<release_date>20261115000000</release_date>
	<short_description>Mon menu Wii personnel</short_description>
	<long_description>Menu d'accueil personnel, ambiance console récente : jeux, applis, rétro et raccourcis.</long_description>
	<ahb_access/>
</app>
''')
print('ok')

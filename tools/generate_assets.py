"""Generate RGB565 background and antialiased type from the supplied reference.
Run from any directory with Python 3 and Pillow installed.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import struct

ROOT = Path(__file__).resolve().parents[1]
image = Image.open(ROOT / 'reference/High-Contrast Cyan Automotive Dashboard UI.png').convert('RGB')
# Work in the original reference coordinates; preserve its aspect ratio on the LCD.
w, h = image.size
regions = [(640, 415, 1005, 551), (218, 309, 390, 371),
           (237, 624, 382, 686), (1271, 279, 1448, 348),
           (1254, 623, 1453, 689), (1090, 41, 1347, 109),
           (283, 835, 493, 885), (775, 835, 1001, 885)]
# Numeric readouts become real rendered data. Retain the central carbon texture.
for x0,y0,x1,y1 in regions:
    if x0 == 640:
        for y in range(y0,y1+1):
            for x in range(x0,x1+1):
                v = 8 + (5 if (x+y)%22 < 5 else 0)
                image.putpixel((x,y),(v,v+3,v+5))
    else:
        ImageDraw.Draw(image).rectangle((x0,y0,x1,y1), fill='black')
scale = min(1024/w, 600/h)
size = (round(w*scale), round(h*scale))
background = Image.new('RGB', (1024,600))
background.paste(image.resize(size, Image.Resampling.LANCZOS), ((1024-size[0])//2,(600-size[1])//2))
background.save(ROOT/'assets/dashboard-background.png')

def array(f, name, data, ctype):
    f.write(f'const {ctype} {name}[] = {{\n')
    for i in range(0,len(data),24):
        f.write(','.join(str(v) for v in data[i:i+24])+',\n')
    f.write('};\n')

chars = '0123456789.,%FVP SIGAMHhDEMO'
chars = ''.join(dict.fromkeys(chars))
font = ImageFont.truetype('C:/Windows/Fonts/arialbi.ttf', 100)
atlas = Image.new('L',(96*len(chars),120))
d = ImageDraw.Draw(atlas)
widths=[]
for i,ch in enumerate(chars):
    d.text((i*96+5,0),ch,font=font,fill=255,stroke_width=0)
    widths.append(round(font.getlength(ch)))
with (ROOT/'src/dashboard_assets.cpp').open('w') as f:
    f.write('#include "dashboard_assets.h"\n')
    rgb=[((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in background.get_flattened_data()]
    array(f,'dashboard_background',rgb,'uint16_t')
    array(f,'font_alpha',list(atlas.get_flattened_data()),'uint8_t')
    array(f,'font_widths',widths,'uint8_t')
with (ROOT/'include/dashboard_assets.h').open('w') as f:
    f.write('#pragma once\n#include <stdint.h>\n')
    f.write('extern const uint16_t dashboard_background[];\nextern const uint8_t font_alpha[], font_widths[];\n')
    f.write(f'constexpr char font_chars[] = "{chars}";\n')
print('Generated 1024x600 background and font atlas')

preview = background.copy()
draw = ImageDraw.Draw(preview)
readouts = [('3,450',511,250,76,226),('176',175,186,43,80),('F',224,194,24,20),
    ('58',171,382,43,70),('PSI',216,397,21,40),('72%',841,168,45,114),
    ('13.8',829,382,43,99),('V',889,399,24,24),('32.4 MPH',760,21,42,160),
    ('487.6 h',243,515,35,142),('12.4 GPH',554,515,35,150),
    ('DEMO',512,571,14,80)]
for value,cx,top,height,maxwidth in readouts:
    width=sum(widths[chars.index(c)] for c in value)
    factor=min(height/100,maxwidth/width)
    left=cx-int(width*factor)//2
    for ch in value:
        index=chars.index(ch)
        for y in range(int(120*factor)):
            for x in range(int(96*factor)):
                a=atlas.getpixel((index*96+int(x/factor),int(y/factor)))
                dx,dy=left+x,top+y
                if a and 0<=dx<1024 and 0<=dy<600:
                    bg=preview.getpixel((dx,dy))
                    preview.putpixel((dx,dy),tuple((v*(255-a)+255*a)//255 for v in bg))
        left+=int(widths[index]*factor)
preview.save(ROOT/'assets/dashboard-preview.png')

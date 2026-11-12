"""Generate RGB565 background and antialiased type from the supplied reference.
Run from any directory with Python 3 and Pillow installed.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math

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

# The reference contains a baked-in band. Extract its colored/gray track pixels
# while protecting white tick marks and the surrounding chrome. Store angle and
# both colors once so firmware only compares thresholds, without per-frame trig.
gauges = [
    ('rpm',507,290,183,183,145,250),
    ('temperature',180,196,109,99,145,250),
    ('oil',180,405,110,104,165,210),
    ('fuel',841,196,110,100,180,180),
    ('voltage',839,405,113,105,165,210),
]
bands=[]
band_ranges=[0]
scale_ticks = {
    # Angles measured at the reference's printed major tick marks. Temperature
    # artwork has a nonuniform last interval (220 to 260), so do not assume a
    # uniformly numbered dial. Values here are normalized to each gauge range.
    'temperature': [(166.9,0),(198.5,.125),(227,.25),(255.7,.375),
                    (286.8,.5),(315.4,.625),(343,.75),(372.7,1)],
    'oil': [(176.7,0),(218.2,.2),(252.9,.4),(290.7,.6),(326.2,.8),(362.3,1)],
    'voltage': [(177.4,0),(213.7,1/6),(242.7,2/6),(270.5,.5),
                (298.9,4/6),(329.1,5/6),(361.7,1)],
}
def scale_fraction(name, angle, start, sweep):
    ticks=scale_ticks.get(name,[(start,0),(start+sweep,1)])
    if angle<=ticks[0][0]: return 0
    for (a,v),(b,w) in zip(ticks,ticks[1:]):
        if angle<=b: return v+(w-v)*(angle-a)/(b-a)
    return 1

for name,cx,cy,rx,ry,start,sweep in gauges:
    for y in range(int(cy-ry-2),int(cy+ry+3)):
        for x in range(int(cx-rx-2),int(cx+rx+3)):
            nx,ny=(x-cx)/rx,(y-cy)/ry
            radius=math.hypot(nx,ny)
            if not .855 <= radius <= 1.005:
                continue
            angle=(math.degrees(math.atan2(ny,nx))-start)%360
            if angle>sweep:
                continue
            r,g,b=background.getpixel((x,y))
            # Keep tick marks and their antialiasing, dark outlines, and chrome.
            if min(r,g,b)>135 or max(r,g,b)<32:
                continue
            if r>110 and g>100 and b>100:
                continue
            red=r>g*1.6 and r>b*1.4
            if red:
                active=(max(150,r),g,b)
                inactive=(55,5,8)
            else:
                brightness=max(.65,min(1,max(g,b)/220))
                active=(0,int(210*brightness),int(255*brightness))
                inactive=(35,43,53)
            pack=lambda c: ((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3)
            bands.append((y*1024+x,round(scale_fraction(name,angle+start,start,sweep)*65535),pack(active),pack(inactive)))
    band_ranges.append(len(bands))

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
    f.write('const BandPixel band_pixels[] = {\n')
    for entry in bands:
        f.write('{'+','.join(map(str,entry))+'},\n')
    f.write('};\n')
    array(f,'band_ranges',band_ranges,'uint32_t')
with (ROOT/'include/dashboard_assets.h').open('w') as f:
    f.write('#pragma once\n#include <stdint.h>\n')
    f.write('extern const uint16_t dashboard_background[];\nextern const uint8_t font_alpha[], font_widths[];\n')
    f.write(f'constexpr char font_chars[] = "{chars}";\n')
    f.write('struct BandPixel { uint32_t offset; uint16_t threshold, active, inactive; };\n')
    f.write('extern const BandPixel band_pixels[];\nextern const uint32_t band_ranges[];\n')
print('Generated 1024x600 background and font atlas')

preview = background.copy()
def paint_bands(target, fractions):
    for i,fraction in enumerate(fractions):
        cutoff=round(max(0,min(1,fraction))*65535)
        for offset,threshold,active,inactive in bands[band_ranges[i]:band_ranges[i+1]]:
            c=active if cutoff>0 and threshold<=cutoff else inactive
            target.putpixel((offset%1024,offset//1024),(((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
paint_bands(preview,[3450/7000,(176-100)/160,58/100,.72,(13.8-10)/6])
for label,fraction in [('empty',0),('half',.5),('full',1)]:
    sample=background.copy()
    paint_bands(sample,[fraction]*5)
    sample.save(ROOT/f'assets/bands-{label}.png')
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

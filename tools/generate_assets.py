"""Generate RGB565 background and antialiased type from the supplied reference.
Run from any directory with Python 3 and Pillow installed.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math
import re

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
# Reclaim the former bottom demo-label area for the dashboard itself.
Y_SCALE = 600 / 570

config=(ROOT/'include/gauge_limits.h').read_text()
RPM_MAX=float(re.search(r'constexpr float rpm_max = ([0-9.]+)f;',config)[1])
RPM_REDLINE=float(re.search(r'constexpr float rpm_redline = ([0-9.]+)f;',config)[1])

# Rebuild only the central scale: the old 0..7 labels and large 6000..7000
# red sector are artwork, so changing the numeric readout alone is insufficient.
for y in range(133,448):
    for x in range(350,665):
        distance=math.hypot(x-507,y-290)
        if distance<=157:
            shade=7+(5 if (x+y)%14<4 else 0)+int(3*(1-distance/157))
            background.putpixel((x,y),(shade,shade+3,shade+5))
rpm_overlay=Image.new('RGBA',(1024*3,600*3))
rd=ImageDraw.Draw(rpm_overlay)
def rpm_arc(radius,width,start,end,color):
    rd.arc(((507-radius)*3,(290-radius)*3,(507+radius)*3,(290+radius)*3),start,end,fill=color,width=width*3)
rpm_arc(184,32,145,395,(31,39,49,255))
red_start=145+250*RPM_REDLINE/RPM_MAX
rpm_arc(184,25,red_start,395,(95,5,8,255))
for value in range(0,int(RPM_MAX)+1,250):
    angle=math.radians(145+250*value/RPM_MAX)
    major=value%1000==0
    inner=153 if major else (160 if value%500==0 else 170)
    rd.line([(int((507+inner*math.cos(angle))*3),int((290+inner*math.sin(angle))*3)),
             (int((507+184*math.cos(angle))*3),int((290+184*math.sin(angle))*3))],
            fill='white',width=4*3 if major else 2*3)
    if major:
        cx=507+137*math.cos(angle); cy=290+137*math.sin(angle)
        f=ImageFont.truetype('C:/Windows/Fonts/arialbi.ttf',36*3)
        rd.text((cx*3,cy*3),str(value//1000),font=f,fill='white',anchor='mm')
# A narrow red marker explicitly identifies 4800, just before the 5000 endpoint.
angle=math.radians(red_start)
rd.line([((507+158*math.cos(angle))*3,(290+158*math.sin(angle))*3),
         ((507+185*math.cos(angle))*3,(290+185*math.sin(angle))*3)],fill='white',width=3*3)
rd.text((507*3,232*3),'RPM',font=ImageFont.truetype('C:/Windows/Fonts/arialbi.ttf',30*3),fill='white',anchor='mm')
background=Image.alpha_composite(background.convert('RGBA'),rpm_overlay.resize((1024,600),Image.Resampling.LANCZOS)).convert('RGB')

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
            red=(angle/sweep>=RPM_REDLINE/RPM_MAX) if name=='rpm' else r>g*1.6 and r>b*1.4
            if red:
                active=(255,8,8) if name=='rpm' else (max(150,r),g,b)
                inactive=(55,5,8)
            else:
                brightness=1 if name=='rpm' else max(.65,min(1,max(g,b)/220))
                active=(0,int(210*brightness),int(255*brightness))
                inactive=(35,43,53)
            pack=lambda c: ((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3)
            bands.append((y*1024+x,round(scale_fraction(name,angle+start,start,sweep)*65535),pack(active),pack(inactive)))
    band_ranges.append(len(bands))

expanded=[]
expanded_ranges=[0]
for i in range(5):
    for offset,threshold,active,inactive in bands[band_ranges[i]:band_ranges[i+1]]:
        x,y=offset%1024,offset//1024
        for screen_y in range(int(y*Y_SCALE),min(600,int((y+1)*Y_SCALE))):
            expanded.append((screen_y*1024+x,threshold,active,inactive))
    expanded_ranges.append(len(expanded))
bands,band_ranges=expanded,expanded_ranges
highlights=[]
highlight_ranges=[0]
for name,cx,cy,rx,ry,start,sweep in gauges:
    rx,ry=rx+6,ry+6
    for y in range(cy-ry-1,cy+ry+2):
        for x in range(cx-rx-1,cx+rx+2):
            if .98<=math.hypot((x-cx)/rx,(y-cy)/ry)<=1:
                for sy in range(int(y*Y_SCALE),min(600,int((y+1)*Y_SCALE))):
                    if 0<=x<1024 and 0<=sy<600: highlights.append(sy*1024+x)
    highlight_ranges.append(len(highlights))
background=background.crop((0,0,1024,570)).resize((1024,600),Image.Resampling.LANCZOS)
engine_icon=background.crop((854,24,930,79))
engine_icon.save(ROOT/'assets/check-engine-icon.png')
ImageDraw.Draw(background).rectangle((852,23,930,79),fill='black')
background.save(ROOT/'assets/dashboard-background.png')

def array(f, name, data, ctype):
    f.write(f'const {ctype} {name}[] = {{\n')
    for i in range(0,len(data),24):
        f.write(','.join(str(v) for v in data[i:i+24])+',\n')
    f.write('};\n')

chars = '0123456789.,%ABCDEFGHIJKLMNOPQRSTUVWXYZ /-h'
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
    array(f,'highlight_pixels',highlights,'uint32_t')
    array(f,'highlight_ranges',highlight_ranges,'uint32_t')
    array(f,'engine_icon_pixels',[((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in engine_icon.get_flattened_data()],'uint16_t')
with (ROOT/'include/dashboard_assets.h').open('w') as f:
    f.write('#pragma once\n#include <stdint.h>\n')
    f.write('extern const uint16_t dashboard_background[];\nextern const uint8_t font_alpha[], font_widths[];\n')
    f.write(f'constexpr char font_chars[] = "{chars}";\n')
    f.write('struct BandPixel { uint32_t offset; uint16_t threshold, active, inactive; };\n')
    f.write('extern const BandPixel band_pixels[];\nextern const uint32_t band_ranges[];\n')
    f.write('constexpr float dashboard_y_scale = 600.0f / 570.0f;\n')
    f.write('extern const uint32_t highlight_pixels[], highlight_ranges[];\n')
    f.write('extern const uint16_t engine_icon_pixels[];\nconstexpr int engine_icon_x=854,engine_icon_y=24,engine_icon_width=76,engine_icon_height=55;\n')
print('Generated 1024x600 background and font atlas')

preview = background.copy()
def paint_bands(target, fractions,alerts=None,flash_on=True):
    for i,fraction in enumerate(fractions):
        cutoff=round(max(0,min(1,fraction))*65535)
        for offset,threshold,active,inactive in bands[band_ranges[i]:band_ranges[i+1]]:
            filled=cutoff>0 and threshold<=cutoff
            c=active if filled else inactive
            alert=alerts[i] if alerts else 'normal'
            if alert=='warning': c=0xffe0 if filled else 0x39a0
            if alert=='critical' and flash_on: c=0xf800 if filled else 0x9800
            target.putpixel((offset%1024,offset//1024),(((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        if alerts and (alerts[i]=='warning' or (alerts[i]=='critical' and flash_on)):
            color=(255,255,0) if alerts[i]=='warning' else (255,0,0)
            for offset in highlights[highlight_ranges[i]:highlight_ranges[i+1]]:
                target.putpixel((offset%1024,offset//1024),color)
paint_bands(preview,[3450/RPM_MAX,(176-100)/160,58/100,.72,(13.8-10)/6])
for label,fraction in [('empty',0),('half',.5),('full',1)]:
    sample=background.copy()
    paint_bands(sample,[fraction]*5)
    sample.save(ROOT/f'assets/bands-{label}.png')
draw = ImageDraw.Draw(preview)
readouts = [('3,450',511,250,76,226),('176',175,186,43,80),('F',224,194,24,20),
    ('58',171,382,43,70),('PSI',216,397,21,40),('72%',841,168,45,114),
    ('13.8',829,382,43,99),('V',889,399,24,24),('32.4 MPH',760,21,42,160),
    ('487.6 h',243,515,35,142),('12.4 GPH',554,515,35,150)]
def draw_value(target,value,cx,top,height,maxwidth,center=False,ink=(255,255,255)):
    width=sum(widths[chars.index(c)] for c in value)
    factor=min(height/100,maxwidth/width)
    if center:
        top-=int(55.5*factor)
    elif value.endswith('MPH'):
        top=48-int(55.5*factor)
    left=cx-int(width*factor)//2
    for ch in value:
        index=chars.index(ch)
        for y in range(int(120*factor*Y_SCALE)):
            for x in range(int(96*factor)):
                a=atlas.getpixel((index*96+int(x/factor),int(y/(factor*Y_SCALE))))
                dx,dy=left+x,int(top*Y_SCALE)+y
                if a and 0<=dx<1024 and 0<=dy<600:
                    bg=target.getpixel((dx,dy))
                    target.putpixel((dx,dy),tuple((v*(255-a)+color*a)//255 for v,color in zip(bg,ink)))
        left+=int(widths[index]*factor)
for value,cx,top,height,maxwidth in readouts:
    draw_value(preview,value,cx,top,height,maxwidth)
preview.save(ROOT/'assets/dashboard-preview.png')

def night_color(c):
    r,g,b=c
    r,g,b=(r>>3)*255//31,(g>>2)*255//63,(b>>3)*255//31
    light=(r*30+g*59+b*11)//100
    nr,ng,nb=light*3//4,light//5,light//32
    if r>g*2 and r>b*2: nr,ng,nb=r*3//4,g//6,0
    return ((nr>>3)*255//31,(ng>>2)*255//63,(nb>>3)*255//31)

def night_image(source):
    target=Image.new('RGB',source.size)
    target.putdata([night_color(c) for c in source.get_flattened_data()])
    return target

night_image(preview).save(ROOT/'assets/dashboard-night-preview.png')
alert_frames=[]
for flash_on in (True,False):
    sample=background.copy()
    paint_bands(sample,[4900/RPM_MAX,(210-100)/160,.1,.07,0],['critical']*5,flash_on)
    for (original,cx,top,height,maxwidth),value in zip(readouts,
            ['4,900','210','F','10','PSI','7%','9.5','V','32.4 MPH','487.6 h','12.4 GPH']):
        ink=(255,0,0) if flash_on and not original.endswith(('MPH','h','GPH')) else (255,255,255)
        draw_value(sample,value,cx,top,height,maxwidth,ink=ink)
    sample.save(ROOT/f'assets/alerts-critical-{"on" if flash_on else "off"}.png')
    alert_frames.append(sample)
alert_frames[0].save(ROOT/'assets/alerts-preview.gif',save_all=True,append_images=alert_frames[1:],duration=500,loop=0)
warning=background.copy()
paint_bands(warning,[3450/RPM_MAX,(176-100)/160,.58,.16,(13.8-10)/6],['normal']*3+['warning','normal'])
for value,cx,top,height,maxwidth in readouts:
    draw_value(warning,'16%' if value=='72%' else value,cx,top,height,maxwidth,ink=(255,255,0) if value=='72%' else (255,255,255))
warning.save(ROOT/'assets/alerts-fuel-warning.png')
for night,brightness in [(False,100),(True,25)]:
    popup=preview.copy()
    d=ImageDraw.Draw(popup)
    d.rounded_rectangle((236,130,787,479),18,fill=(0,255,255))
    d.rounded_rectangle((239,133,784,476),15,fill=(8,12,16))
    d.rounded_rectangle((725,145,776,194),10,fill=(33,32,33))
    d.rounded_rectangle((310,274,713,285),6,fill=(57,56,57))
    knob=310+(brightness-5)*404//95
    if knob>310: d.rounded_rectangle((310,274,knob,285),6,fill=(0,255,255))
    d.rounded_rectangle((knob-17,263,knob+17,297),17,fill='white')
    d.rounded_rectangle((knob-10,270,knob+10,290),10,fill=(0,255,255))
    d.rounded_rectangle((290,345,733,414),12,fill=(192,124,0) if night else (0,255,255))
    d.rounded_rectangle((292,347,731,412),10,fill=(16,16,16))
    for value,cx,cy,height,maxwidth in [
        ('DISPLAY SETTINGS',474,171,29,380),('X',750,170,29,40),
        ('BACKLIGHT',391,225,25,230),(f'{brightness}%',686,225,29,95),
        ('5%',310,320,16,80),('100%',714,320,16,80),
        ('NIGHT MODE  ON' if night else 'NIGHT MODE  OFF',512,380,28,405),
        ('DAY AND NIGHT BRIGHTNESS SAVED',512,448,17,500)]:
        draw_value(popup,value,cx,int(cy/Y_SCALE),height,maxwidth,True)
    if night: popup=night_image(popup)
    popup.save(ROOT/f'assets/settings-{"night" if night else "day"}-preview.png')

from selectable_assets import generate_selectable_assets
generate_selectable_assets(ROOT,night_image)

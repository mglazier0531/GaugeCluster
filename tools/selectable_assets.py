"""Build reusable gauge faces and sparse animated tracks from both references."""
from PIL import Image, ImageDraw, ImageFont
import math

SIZE=320
CATALOG=[
    ('RPM',0,5000,1000,4800,'high',[],None),
    ('ENGINE TEMP',100,260,20,200,'high',['ENGINE TEMP'],('primary',(160,145,205,193))),
    ('OIL PRESSURE',0,100,20,15,'low',['OIL PRESSURE'],('primary',(150,355,220,394))),
    ('FUEL LEVEL',0,100,25,10,'low',['FUEL LEVEL'],('primary',(821,132,868,182))),
    ('BATTERY VOLTAGE',10,16,1,10,'low',['BATTERY VOLTAGE'],('primary',(815,365,868,403))),
    ('TRIM',0,100,10,60,'high',['TRIM'],('secondary',(212,135,368,224))),
    ('AIR/FUEL RATIO',10,18,1,16,'high',['AFR','AIR/FUEL RATIO'],('secondary',(770,126,848,209))),
    ('INTAKE AIR TEMP',40,180,20,130,'high',['INTAKE AIR TEMP'],('secondary',(1260,140,1410,224))),
    ('ACCELERATOR POSITION',0,100,10,80,'high',['ACCELERATOR','POSITION'],('secondary',(449,571,590,660))),
    ('FUEL PRESSURE',0,100,10,25,'low',['FUEL PRESSURE'],('secondary',(1032,570,1142,665))),
]

def pack(c): return ((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3)
def unpack(c): return (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31)
def font(size,bold=False): return ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf' if bold else 'C:/Windows/Fonts/arialbi.ttf',size)
def fit_text(d,xy,value,size,width,color='white',bold=False):
    f=font(size,bold)
    while f.getlength(value)>width:
        size-=1; f=font(size,bold)
    d.text(xy,value,font=f,fill=color,anchor='mm')

def generate_selectable_assets(root,night_image):
    primary=Image.open(root/'assets/dashboard-background.png').convert('RGB')
    secondary=Image.open(root/'reference/Five-Gauge Marine Performance Dashboard.png').convert('RGB')
    chrome=secondary.crop((52,16,496,460)).resize((SIZE,SIZE),Image.Resampling.LANCZOS)
    out=root/'assets/selectable-gauges'; out.mkdir(exist_ok=True)
    faces=[]; pixels=[]; ranges=[0]; specs=[]
    for index,(name,minimum,maximum,step,limit,direction,labels,icon) in enumerate(CATALOG):
        face=chrome.copy()
        p=face.load()
        for y in range(SIZE):
            for x in range(SIZE):
                radius=math.hypot(x-160,y-160)
                if radius<145:
                    value=7+(5 if (x+y)%14<4 else 0) if index==0 else 0
                    p[x,y]=(value,value+2,value+3) if index==0 else (0,0,0)
                elif radius>159: p[x,y]=(0,0,0)
        start,sweep=(180,180) if index==3 else (145,250)
        # Track and ticks are generated at 3x resolution to keep small slots crisp.
        layer=Image.new('RGBA',(SIZE*3,SIZE*3)); d=ImageDraw.Draw(layer)
        box=((160-140)*3,(160-140)*3,(160+140)*3,(160+140)*3)
        d.arc(box,start,start+sweep,fill=(35,43,53),width=16*3)
        fraction=(limit-minimum)/(maximum-minimum)
        if 0<fraction<1:
            lo,hi=(start+fraction*sweep,start+sweep) if direction=='high' else (start,start+fraction*sweep)
            d.arc(box,lo,hi,fill=(55,5,8),width=16*3)
        count=round((maximum-minimum)/step)
        for i in range(count*2+1):
            angle=math.radians(start+sweep*i/(count*2))
            major=i%2==0
            inner=120 if major else 131
            d.line([((160+inner*math.cos(angle))*3,(160+inner*math.sin(angle))*3),
                    ((160+141*math.cos(angle))*3,(160+141*math.sin(angle))*3)],fill='white',width=(3 if major else 1)*3)
            if major:
                value=minimum+step*i/2
                label=('E' if i==0 else ('F' if i==count*2 else '')) if index==3 else (str(int(value/1000)) if index==0 else str(int(value)))
                if label:
                    fit_text(d,((160+109*math.cos(angle))*3,(160+109*math.sin(angle))*3),label,(23 if count<=8 else 20)*3,65*3)
        if index==0:
            angle=math.radians(start+sweep*fraction)
            d.line([((160+120*math.cos(angle))*3,(160+120*math.sin(angle))*3),
                    ((160+142*math.cos(angle))*3,(160+142*math.sin(angle))*3)],fill='white',width=2*3)
        face=Image.alpha_composite(face.convert('RGBA'),layer.resize((SIZE,SIZE),Image.Resampling.LANCZOS)).convert('RGB')
        if icon:
            source,rect=icon
            crop=(primary if source=='primary' else secondary).crop(rect)
            alpha=Image.new('L',crop.size)
            alpha.putdata([max(0,min(255,(min(c)-60)*255//195)) for c in crop.get_flattened_data()])
            glyph=Image.new('RGBA',crop.size,'white'); glyph.putalpha(alpha)
            glyph.thumbnail((104,62),Image.Resampling.LANCZOS)
            face.paste(glyph,(160-glyph.width//2,121-glyph.height//2),glyph)
        draw=ImageDraw.Draw(face)
        if index==0: fit_text(draw,(160,124),'RPM',34,180)
        elif index==6:
            fit_text(draw,(160,239),'AFR',28,200)
            fit_text(draw,(160,268),'AIR/FUEL RATIO',19,260,bold=True)
        else:
            draw.line((88,235,232,235),fill='white',width=2)
            for n,label in enumerate(labels): fit_text(draw,(160,253+n*24),label,22,268,bold=True)
        # Readouts are rendered live; these faces have no baked-in sample value.
        faces.append(face)
        face.save(out/f'{index:02d}.png')
        for y in range(SIZE):
            for x in range(SIZE):
                dx,dy=x-160,y-160
                radius=math.hypot(dx,dy)
                if not 124<=radius<=140: continue
                angle=(math.degrees(math.atan2(dy,dx))-start)%360
                if angle>sweep: continue
                if min(face.getpixel((x,y)))>140: continue
                value=minimum+(maximum-minimum)*angle/sweep
                danger=value>limit if direction=='high' else value<limit
                pixels.append((y*SIZE+x,round(angle/sweep*65535),0xf800 if danger else 0x06bf,0x3801 if danger else 0x2146))
        ranges.append(len(pixels))
        specs.append((name,minimum,maximum))
    with (root/'include/selectable_assets.h').open('w') as f:
        f.write('#pragma once\n#include "dashboard_assets.h"\n')
        f.write('constexpr unsigned gauge_face_size=320;\nstruct GaugeFaceSpec { const char* name; float minimum,maximum; };\n')
        f.write('extern const GaugeFaceSpec gauge_face_specs[10];\nextern const uint16_t gauge_face_pixels[];\n')
        f.write('extern const BandPixel gauge_face_bands[];\nextern const uint32_t gauge_face_ranges[];\n')
    with (root/'src/selectable_assets.cpp').open('w') as f:
        f.write('#include "selectable_assets.h"\nconst GaugeFaceSpec gauge_face_specs[10]={\n')
        for name,lo,hi in specs: f.write(f'{{"{name}",{lo}.0f,{hi}.0f}},\n')
        f.write('};\nconst uint16_t gauge_face_pixels[]={\n')
        for face in faces:
            data=[pack(c) for c in face.get_flattened_data()]
            for i in range(0,len(data),24): f.write(','.join(map(str,data[i:i+24]))+',\n')
        f.write('};\nconst BandPixel gauge_face_bands[]={\n')
        for item in pixels: f.write('{'+','.join(map(str,item))+'},\n')
        f.write('};\nconst uint32_t gauge_face_ranges[]={'+','.join(map(str,ranges))+'};\n')
    # Contact sheet for reviewing all ten choices, with reference-style sample values.
    samples=['3,450','176 F','58 PSI','72%','13.8 V','35%','14.2','108 F','42%','58 PSI']
    sample_values=[3450,176,58,72,13.8,35,14.2,108,42,58]
    sheet=Image.new('RGB',(SIZE*5,SIZE*2))
    populated=[]
    for i,(face,value) in enumerate(zip(faces,samples)):
        example=face.copy()
        _,lo,hi=specs[i]
        cutoff=round((sample_values[i]-lo)/(hi-lo)*65535)
        for offset,threshold,active,inactive in pixels[ranges[i]:ranges[i+1]]:
            example.putpixel((offset%SIZE,offset//SIZE),unpack(active if cutoff>0 and threshold<=cutoff else inactive))
        d=ImageDraw.Draw(example)
        fit_text(d,(160,190),value,54,218)
        populated.append(example)
        sheet.paste(example,((i%5)*SIZE,(i//5)*SIZE))
    sheet.save(root/'assets/gauge-catalog-preview.png')
    layout=Image.open(root/'assets/dashboard-preview.png').convert('RGB')
    slots=[(302,90,410,431),(56,86,248,230),(56,305,248,230),(716,86,248,230),(715,305,250,230)]
    for kind,(l,t,w,h) in zip([6,5,8,7,9],slots):
        resized=populated[kind].resize((w,h),Image.Resampling.LANCZOS)
        mask=Image.new('L',(w,h)); ImageDraw.Draw(mask).ellipse((0,0,w-1,h-1),fill=255)
        layout.paste(resized,(l,t),mask)
    layout.save(root/'assets/selectable-layout-preview.png')
    night_image(layout).save(root/'assets/selectable-layout-night-preview.png')
    picker=layout.copy(); d=ImageDraw.Draw(picker)
    d.rounded_rectangle((195,90,829,520),18,fill=(0,255,255))
    d.rounded_rectangle((198,93,826,517),15,fill=(8,12,16))
    fit_text(d,(475,123),'SELECT GAUGE',29,430)
    d.rounded_rectangle((746,100,803,145),10,fill=(33,32,33)); fit_text(d,(775,122),'X',28,45)
    for i,(name,_,_) in enumerate(specs):
        l=222+(i%2)*304; t=158+(i//2)*66
        d.rounded_rectangle((l,t,l+276,t+54),9,outline=(0,255,255),width=2,fill=(16,20,24))
        fit_text(d,(l+138,t+27),name,22,252)
    picker.save(root/'assets/gauge-picker-preview.png')
    print(f'Generated ten reusable gauge faces and {len(pixels)} animated track pixels')
    generate_hidden_alert_previews(root)

def generate_hidden_alert_previews(root):
    base=Image.open(root/'assets/dashboard-preview.png').convert('RGB')
    rows=[('TRIM','70%'),('AIR/FUEL RATIO','17.2'),('INTAKE AIR TEMP','145 F'),
          ('ACCELERATOR POSITION','88%'),('FUEL PRESSURE','20 PSI')]
    frames=[]
    for flash in (True,False):
        frame=base.copy(); d=ImageDraw.Draw(frame)
        red=(255,0,0); ink=red if flash else 'white'
        d.rounded_rectangle((215,105,808,504),18,fill=red if flash else (66,65,66))
        d.rounded_rectangle((219,109,804,500),14,fill=(8,12,16))
        fit_text(d,(512,140),'GAUGE ALERTS',30,540,ink)
        fit_text(d,(512,173),'GAUGES NOT CURRENTLY DISPLAYED',17,540)
        for row,(name,value) in enumerate(rows):
            fit_text(d,(415,209+row*25),name,20,360,ink)
            fit_text(d,(716,209+row*25),value,22,135,ink)
        d.rounded_rectangle((327,445,696,488),10,fill=red)
        d.rounded_rectangle((329,447,694,486),8,fill=(16,16,16))
        fit_text(d,(512,467),'ACKNOWLEDGE',25,335)
        frame.save(root/f'assets/hidden-alert-{"on" if flash else "off"}.png')
        frames.append(frame)
    frames[0].save(root/'assets/hidden-alert-preview.gif',save_all=True,append_images=frames[1:],duration=500,loop=0)

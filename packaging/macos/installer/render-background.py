"""Render the DMG's static grid artwork using Forward's existing brand assets."""
from pathlib import Path
import math
import subprocess
from PIL import Image, ImageDraw, ImageFilter, ImageFont
root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
scale = 2
w,h = 720,440
# Match the website's --ink (#010305) and restrained surface/grid tones.
im = Image.new('RGB',(w*scale,h*scale),(1,3,5))
d = ImageDraw.Draw(im)
for x in range(0,w+1,24):
    d.line((x*scale,0,x*scale,h*scale), fill=(17,24,29),width=1)
for y in range(0,h+1,24):
    d.line((0,y*scale,w*scale,y*scale), fill=(17,24,29),width=1)
for x in range(0,w+1,96):
    for y in range(0,h+1,96):
        d.line(((x-3)*scale,y*scale,(x+3)*scale,y*scale),fill=(35,47,55),width=1)
        d.line((x*scale,(y-3)*scale,x*scale,(y+3)*scale),fill=(35,47,55),width=1)

# A frozen pulse of website pink (#e967c8) travels along the existing grid.
# Color the grid itself, with a faint surrounding glow, rather than drawing
# an unrelated ribbon over the installation controls.
pulse = Image.new('L', im.size)
alpha = pulse.load()
for x in range(w*scale):
    center = 208 + 83*math.sin((x/scale - 65)/153)
    edge_fade = min(1, x/(24*scale), (w*scale-1-x)/(24*scale))
    rows = range(h*scale) if x % (24*scale) == 0 else range(0,h*scale,24*scale)
    for y in rows:
        distance = y/scale - center
        alpha[x,y] = int(235*edge_fade*math.exp(-0.5*(distance/23)**2))
pink = Image.new('RGB',im.size,(233,103,200))
halo = pulse.filter(ImageFilter.GaussianBlur(7*scale)).point(lambda value: min(255,value*4))
im = Image.composite(pink,im,halo)
im = Image.composite(pink,im,pulse)
d = ImageDraw.Draw(im)
def text(value, xy, size, color, bold=False, anchor='mm'):
    font=ImageFont.truetype(str(root/'assets/fonts'/('Aileron-SemiBold.otf' if bold else 'Aileron-Regular.otf')),size*scale)
    d.text((xy[0]*scale,xy[1]*scale),value,font=font,fill=color,anchor=anchor)
text('Mirdain Forward',(360,72),32,(231,238,244),True)
text('Drag into Applications. Then you’re ready to go.',(360,110),16,(168,183,197))
# Finder draws native filenames in black on image backgrounds. Use the site's
# ice accent for small nameplates rather than changing or hiding the real names.
for x in (190,530):
    d.rounded_rectangle(((x-76)*scale,283*scale,(x+76)*scale,310*scale),radius=3*scale,fill=(178,206,227))
# A deliberate fixed path. The app itself never animates or disappears.
d.line((306*scale,231*scale,414*scale,231*scale),fill=(178,206,227),width=2*scale)
d.line((402*scale,219*scale,414*scale,231*scale,402*scale,243*scale),fill=(178,206,227),width=2*scale)
im.save(out/'background@2x.png')
im.resize((w,h),Image.Resampling.LANCZOS).save(out/'background.png')
subprocess.run(['tiffutil','-cathidpicheck',str(out/'background.png'),str(out/'background@2x.png'),'-out',str(out/'background.tiff')],check=True)

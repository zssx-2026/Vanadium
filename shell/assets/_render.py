
# -*- coding: utf-8 -*-
# Render icon.svg (rounded square + lemniscate stroke, gradient) without a browser.
# PIL stamps filled circles along the cubic Bezier loop; supersample 4x then LANCZOS down.
from PIL import Image, ImageDraw
import math, os

SS = 4          # supersample factor
W = 256 * SS    # working canvas

GRAD0 = (0xA7, 0x94, 0xFF)  # #A794FF top-left
GRAD1 = (0x5B, 0x4B, 0xFF)  # #5B4BFF bottom-right
BG    = (0x17, 0x1A, 0x2E)  # #171A2E rounded square
RADIUS = 52 * SS
STROKE = 21.0 * SS          # stroke width in canvas px (radius per stamp)

# Cubic Bezier segments of the lemniscate path (256-space, scaled by SS)
P = [(64,128),(128,128),(192,128)]
SEGS = [
    ((64,128),(64,98),(104,98),(128,128)),
    ((128,128),(152,158),(192,158),(192,128)),
    ((192,128),(192,98),(152,98),(128,128)),
    ((128,128),(104,158),(64,158),(64,128)),
]

def cubic(p0,p1,p2,p3,t):
    mt = 1.0-t
    x = mt*mt*mt*p0[0] + 3*mt*mt*t*p1[0] + 3*mt*t*t*p2[0] + t*t*t*p3[0]
    y = mt*mt*mt*p0[1] + 3*mt*mt*t*p1[1] + 3*mt*t*t*p2[1] + t*t*t*p3[1]
    return (x*SS, y*SS)

def grad(px, py):
    # diagonal gradient like x1=0,y1=0 -> x2=1,y2=1 over the 256 box
    t = (px + py) / (2.0 * W)
    t = max(0.0, min(1.0, t))
    return tuple(int(GRAD0[i] + (GRAD1[i]-GRAD0[i])*t) for i in range(3))

img = Image.new("RGBA", (W, W), (0,0,0,0))
d = ImageDraw.Draw(img)
d.rounded_rectangle([0,0,W-1,W-1], radius=RADIUS, fill=BG+(255,))

# stamp circles along the path, denser than needed for smoothness
N = 2400
for (p0,p1,p2,p3) in SEGS:
    for i in range(N):
        t = i / float(N)
        x,y = cubic(p0,p1,p2,p3,t)
        c = grad(x,y)
        r = STROKE/2.0
        d.ellipse([x-r,y-r,x+r,y+r], fill=c+(255,))

out = os.path.join(r"D:\dev\Infinity File Version Control System\shell\assets", "icon_256.png")
img.resize((256,256), Image.LANCZOS).save(out)

# multi-size set for the .ico
ico_path = os.path.join(r"D:\dev\Infinity File Version Control System\shell\assets", "inv.ico")
base = img.resize((256,256), Image.LANCZOS)
base.save(ico_path, format="ICO", sizes=[(256,256),(64,64),(48,48),(32,32),(16,16)])
for s in (64,48,32,16):
    base.resize((s,s), Image.LANCZOS).save(os.path.join(r"D:\dev\Infinity File Version Control System\shell\assets", "icon_%d.png" % s))
print("done", os.path.getsize(out), os.path.getsize(ico_path))

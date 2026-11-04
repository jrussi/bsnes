# Poses extraidas automaticamente da silhueta do grupo original + desenho da lontra de frente / costas / lado.
import json,statistics
from PIL import Image
from gruposlib import *
from lontra import lontra as lontra_lado, O, stroke
def compor(sig,pngdir):
    W,H=bbox(sig); o=Image.new('RGBA',(W,H),(0,0,0,0))
    for h,x,y,hf,vf,w,hh in sig:
        t=Image.open(f'{pngdir}/{h}.png').convert('RGBA')
        if hf: t=t.transpose(Image.FLIP_LEFT_RIGHT)
        if vf: t=t.transpose(Image.FLIP_TOP_BOTTOM)
        o.alpha_composite(t,(x,y))
    return o
def extrair(img):
    W,H=img.size; px=img.load()
    op=[(x,y) for y in range(H) for x in range(W) if px[x,y][3]>0]
    top=min(y for x,y in op)
    cabeca=[(x,y) for x,y in op if y<top+12]
    hx=sum(x for x,y in cabeca)/len(cabeca)+0.5
    hy=top+6
    quad=[x for x,y in op if 23<=y<=29]; cx=(sum(quad)/len(quad)+0.5) if quad else hx
    sapato=[(x,y) for x,y in op if y>=31 and px[x,y][0]>150 and px[x,y][1]>100 and px[x,y][2]<110]
    def dois(pts,padrao_y):
        if not pts: return (cx-3,padrao_y),(cx+3,padrao_y)
        xs=sorted(x for x,y in pts); m=statistics.median(xs)
        a=[p for p in pts if p[0]<m] or pts; b=[p for p in pts if p[0]>=m] or pts
        ma=(sum(p[0] for p in a)/len(a)+0.5,sum(p[1] for p in a)/len(a)+0.5)
        mb=(sum(p[0] for p in b)/len(b)+0.5,sum(p[1] for p in b)/len(b)+0.5)
        if mb[0]-ma[0]<5: ma=(ma[0]-2.5,ma[1]); mb=(mb[0]+2.5,mb[1])
        return ma,mb
    pl,pr=dois(sapato,36.5)
    luva=[(x,y) for x,y in op if 15<=y<=33 and min(px[x,y][:3])>185]
    if luva: ml,mr=dois(luva,27)
    else: ml,mr=(hx-6.5,27),(hx+6.5,27)
    cl=lambda p,a,b:(min(max(p[0],a),b),p[1])
    return dict(h=(hx,hy),p=(cx,27.5),fl=cl(pl,3.6,W-3.6),fr=cl(pr,3.6,W-3.6),ml=cl(ml,2.2,W-2.2),mr=cl(mr,2.2,W-2.2))
def cabeca_base(hx,hy):
    return (f'<circle cx="{hx-4.6}" cy="{hy-4.3}" r="2" fill="#8b5a2b" stroke="{O}" stroke-width=".8"/><circle cx="{hx+4.6}" cy="{hy-4.3}" r="2" fill="#8b5a2b" stroke="{O}" stroke-width=".8"/>'
            f'<ellipse cx="{hx}" cy="{hy}" rx="5.6" ry="5.2" fill="#a46b35" stroke="{O}" stroke-width=".9"/>')
def pernas(ps):
    px,py=ps['p']; r=''
    for (fx,fy),dx in ((ps['fl'],-2),(ps['fr'],2)):
        r+=stroke([(px+dx,py),((px+dx+fx)/2,(py+fy)/2-0.5),(fx,fy-2.2)],3.6,"#8b5a2b")
        r+=f'<ellipse cx="{fx}" cy="{fy-0.2}" rx="3.3" ry="2.1" fill="#f0a830" stroke="{O}" stroke-width=".8"/>'
    return r
def bracos(ps,sy,lado=4.6):
    hx=ps['h'][0]; r=''
    for (mx,my),dx in ((ps['ml'],-lado),(ps['mr'],lado)):
        sx=hx+dx; mx=max(min(mx,sx+(mx-sx)*.9),0)
        r+=stroke([(sx,sy+1),((sx+mx)/2,(sy+my)/2),(mx,my)],3.0,"#8b5a2b")+f'<circle cx="{mx:.2f}" cy="{my:.2f}" r="1.9" fill="#fff" stroke="{O}" stroke-width=".7"/>'
    return r
def torso(ps,sy):
    hx=ps['h'][0]; px,py=ps['p']
    return stroke([(px,py),(hx,sy)],9.4,"#2a9d8f",.8)
def frente(ps,w,h):
    hx,hy=ps['h']; px,py=ps['p']; sy=hy+5.4
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">
<ellipse cx="{px:.1f}" cy="38.4" rx="{abs(ps['fr'][0]-ps['fl'][0])/2+5.5:.1f}" ry="1.6" fill="#000" opacity=".3"/>
{pernas(ps)}{torso(ps,sy)}{stroke([(px,py-.5),(hx,sy+.8)],4.0,"#f1dfc0",0)}
{bracos(ps,sy)}
{cabeca_base(hx,hy)}
<ellipse cx="{hx}" cy="{hy+2.2}" rx="3.5" ry="2.7" fill="#f1dfc0" stroke="{O}" stroke-width=".7"/>
<ellipse cx="{hx}" cy="{hy+.8}" rx="1.5" ry="1" fill="#1d1208"/>
<circle cx="{hx-2.3}" cy="{hy-1.1}" r="1.6" fill="#fff" stroke="{O}" stroke-width=".6"/><circle cx="{hx+2.3}" cy="{hy-1.1}" r="1.6" fill="#fff" stroke="{O}" stroke-width=".6"/>
<circle cx="{hx-2.2}" cy="{hy-.9}" r=".8" fill="#1d1208"/><circle cx="{hx+2.2}" cy="{hy-.9}" r=".8" fill="#1d1208"/>
<path d="M{hx-5.3},{hy-2.4} q5.3,-7.4 10.6,0 l-1,-.9 q-4.3,-1.6 -8.6,0z" fill="#d64045" stroke="{O}" stroke-width=".8" stroke-linejoin="round"/>
<path d="M{hx-3.6},{hy-2} q3.6,1.6 7.2,0" fill="none" stroke="{O}" stroke-width="1.4" stroke-linecap="round"/><path d="M{hx-3.6},{hy-2} q3.6,1.6 7.2,0" fill="none" stroke="#d64045" stroke-width=".7" stroke-linecap="round"/>
</svg>'''
def costas(ps,w,h):
    hx,hy=ps['h']; px,py=ps['p']; sy=hy+5.4
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">
<ellipse cx="{px:.1f}" cy="38.4" rx="{abs(ps['fr'][0]-ps['fl'][0])/2+5.5:.1f}" ry="1.6" fill="#000" opacity=".3"/>
<path d="M{px},{py+2} q{3},{5} {6},{3}" fill="none" stroke="{O}" stroke-width="4.2" stroke-linecap="round"/><path d="M{px},{py+2} q{3},{5} {6},{3}" fill="none" stroke="#8b5a2b" stroke-width="2.8" stroke-linecap="round"/>
{pernas(ps)}{torso(ps,sy)}<path d="M{(px+hx)/2},{sy+2} L{px},{py-1}" stroke="#1f6f66" stroke-width=".8" fill="none"/>
{bracos(ps,sy)}
{cabeca_base(hx,hy)}
<path d="M{hx-5.7},{hy+.4} q0,-8 5.7,-8 q5.7,0 5.7,8 q-5.7,2.4 -11.4,0z" fill="#d64045" stroke="{O}" stroke-width=".8" stroke-linejoin="round"/>
<circle cx="{hx}" cy="{hy-7.6}" r="1" fill="#f0a830" stroke="{O}" stroke-width=".5"/>
</svg>'''

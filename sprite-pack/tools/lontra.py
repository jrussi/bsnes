# Personagem original "Lontra" (otter com colete e bone). Poses imitam os 6 grupos laterais de 26x40.
import math
POSES=[ # cabeca, quadril, pe da frente, pe de tras  (px do grupo 26x40)
 dict(h=(19,12),p=(13,27),f=(20,35),b=(8,35)),
 dict(h=(18,11),p=(14,26),f=(19,34),b=(12,36.5)),
 dict(h=(19,12),p=(13,28),f=(21,36.5),b=(5,32)),
 dict(h=(19,12),p=(12,28),f=(19,36),b=(8,35.5)),
 dict(h=(17,11),p=(15,27),f=(21,32.5),b=(13,36.5)),
 dict(h=(19,12),p=(13,28),f=(21,35.5),b=(7,36)),
]
O="#3b2414"
def stroke(pts,w,cor,borda=0.7):
    d="M"+" L".join(f"{x:.2f},{y:.2f}" for x,y in pts)
    return (f'<path d="{d}" fill="none" stroke="{O}" stroke-width="{w+2*borda}" stroke-linecap="round" stroke-linejoin="round"/>'
            f'<path d="{d}" fill="none" stroke="{cor}" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"/>')
def perna(p,f,cor,joelho_dx):
    kx=(p[0]+f[0])/2+joelho_dx; ky=(p[1]+f[1])/2-0.5
    return stroke([p,(kx,ky),(f[0]-1,f[1]-2.2)],3.6,cor)+f'<ellipse cx="{f[0]+0.4}" cy="{f[1]-0.2}" rx="3.9" ry="2.2" fill="#f0a830" stroke="{O}" stroke-width=".8"/>'
def lontra(ps,w=26,h=40):
    hx,hy=ps['h']; px,py=ps['p']; fx,fy=ps['f']; bx,by=ps['b']
    sx,sy=hx-2.2,hy+5.2
    cx,cy=(sx+px)/2,(sy+py)/2
    mao_f=(sx+(bx-px)*0.45+2.5,sy+8.5); mao_b=(sx+(fx-px)*0.35-1,sy+8.5)
    braco=lambda m,cor: stroke([(sx,sy+1),((sx+m[0])/2,(sy+m[1])/2),m],3.0,cor)+f'<circle cx="{m[0]:.2f}" cy="{m[1]:.2f}" r="1.9" fill="#fff" stroke="{O}" stroke-width=".7"/>'
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">
<ellipse cx="{(fx+bx)/2+1:.1f}" cy="38.4" rx="{abs(fx-bx)/2+6:.1f}" ry="1.6" fill="#000" opacity=".3"/>
{perna((px-1,py),(bx,by),"#6e4524",-1.5)}
{braco(mao_b,"#6e4524")}
{stroke([(px,py),(sx,sy)],9.6,"#2a9d8f",.8)}
{stroke([(px+.8,py-.5),(sx+1,sy+.8)],4.4,"#f1dfc0",0)}
{perna((px+1,py),(fx,fy),"#8b5a2b",1.5)}
<path d="M{px-4.5:.1f},{py:.1f} q-4,1 -6,-1.5" fill="none" stroke="{O}" stroke-width="3.8" stroke-linecap="round"/>
<path d="M{px-4.5:.1f},{py:.1f} q-4,1 -6,-1.5" fill="none" stroke="#8b5a2b" stroke-width="2.4" stroke-linecap="round"/>
<circle cx="{hx-3.6}" cy="{hy-4.2}" r="2" fill="#8b5a2b" stroke="{O}" stroke-width=".8"/>
<ellipse cx="{hx}" cy="{hy}" rx="5.8" ry="5.2" fill="#a46b35" stroke="{O}" stroke-width=".9"/>
<ellipse cx="{hx+3.1}" cy="{hy+2}" rx="3.4" ry="2.8" fill="#f1dfc0" stroke="{O}" stroke-width=".8"/>
<ellipse cx="{hx+5.3}" cy="{hy+1}" rx="1.3" ry="1" fill="#1d1208"/>
<circle cx="{hx+2}" cy="{hy-1.3}" r="1.6" fill="#fff" stroke="{O}" stroke-width=".6"/><circle cx="{hx+2.5}" cy="{hy-1.2}" r="0.8" fill="#1d1208"/>
<path d="M{hx-5.2},{hy-2.2} q5,-6.6 10.6,-1.2 l-1,-2 q-5,-4 -9,1z" fill="#d64045" stroke="{O}" stroke-width=".8" stroke-linejoin="round"/>
<path d="M{hx+3},{hy-3.4} q2,-.1 3,1" fill="none" stroke="{O}" stroke-width="1.6" stroke-linecap="round"/><path d="M{hx+3},{hy-3.4} q2,-.1 3,1" fill="none" stroke="#d64045" stroke-width=".8" stroke-linecap="round"/>
{braco(mao_f,"#8b5a2b")}
</svg>'''
if __name__=='__main__':
    for i,ps in enumerate(POSES): open(f'svg/lontra_{i}.svg','w').write(lontra(ps))

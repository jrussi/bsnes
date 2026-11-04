import csv,collections,hashlib
def carregar(path):
    fr=collections.defaultdict(list)
    for r in csv.DictReader(open(path)):
        fr[int(r['frame'])].append({k:(v if k=='hash' else int(v)) for k,v in r.items()})
    return fr
def agrupar(sp,gap=1):
    n=len(sp); p=list(range(n))
    def find(i):
        while p[i]!=i: p[i]=p[p[i]]; i=p[i]
        return i
    for i in range(n):
        for j in range(i+1,n):
            a,b=sp[i],sp[j]
            if not(a['x']+a['width']+gap<b['x'] or b['x']+b['width']+gap<a['x'] or a['y']+a['height']+gap<b['y'] or b['y']+b['height']+gap<a['y']): p[find(i)]=find(j)
    cl=collections.defaultdict(list)
    for i in range(n): cl[find(i)].append(sp[i])
    return list(cl.values())
def assinatura(c):
    mx=min(s['x'] for s in c); my=min(s['y'] for s in c)
    return tuple(sorted((s['hash'],s['x']-mx,s['y']-my,s['hflip'],s['vflip'],s['width'],s['height']) for s in c))
def gid(sig):
    # FNV-1a 64 sobre "hash:dx:dy:hflip:vflip:w:h;" ordenado -- identico ao calculo feito dentro do bsnes
    t=''.join(f"{h}:{x}:{y}:{hf}:{vf}:{w}:{hh};" for h,x,y,hf,vf,w,hh in sig).encode()
    v=0xcbf29ce484222325
    for b in t: v=((v^b)*0x100000001b3)&0xffffffffffffffff
    return f"{v:016x}"
def bbox(sig):
    return (max(x+w for _,x,y,_,_,w,h in sig), max(y+h for _,x,y,_,_,w,h in sig))

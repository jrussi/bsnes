import sys,json,collections
from gruposlib import *
fr=carregar(sys.argv[1]); cnt=collections.Counter(); sg={}
for sp in fr.values():
    for c in agrupar(sp):
        s=assinatura(c); cnt[gid(s)]+=1; sg[gid(s)]=s
out=[{'id':i,'ocorrencias':n,'largura':bbox(sg[i])[0],'altura':bbox(sg[i])[1],'pecas':len(sg[i])} for i,n in cnt.most_common()]
json.dump(out,open(sys.argv[2],'w'),indent=1)
for k,o in enumerate(out[:30],1): print(k,o)

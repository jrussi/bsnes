# uso: render_pack.py tabela.json grupos.json pasta_saida [escala]
import sys,json,os,re,subprocess
tab=json.load(open(sys.argv[1])); gr={g['id']:g for g in json.load(open(sys.argv[2]))}
out=sys.argv[3]; esc=int(sys.argv[4]) if len(sys.argv)>4 else tab['escala_padrao']; PAD=tab.get('margem',8)
os.makedirs(out,exist_ok=True); idx=[]
for e in tab['entradas']:
    g=gr.get(e['id'])
    if not g: print('AVISO: grupo desconhecido',e['id']); continue
    vb=re.search(r'viewBox="0 0 (\d+) (\d+)"',open(e['svg']).read())
    if (int(vb[1]),int(vb[2]))!=(g['largura'],g['altura']): print('AVISO: viewBox',vb[1],vb[2],'difere do grupo',g['largura'],g['altura'],e['id'])
    png=os.path.join(out,e['id']+'.png')
    w,h=g['largura'],g['altura']
    # margem de PAD px nativos em volta da caixa do grupo: o desenho pode passar dos limites do original
    txt=open(e['svg']).read()
    txt=re.sub(r'viewBox="0 0 \d+ \d+" width="\d+" height="\d+"',f'viewBox="{-PAD} {-PAD} {w+2*PAD} {h+2*PAD}" width="{w+2*PAD}" height="{h+2*PAD}"',txt,count=1)
    tmp=os.path.join(out,'_tmp.svg'); open(tmp,'w').write(txt)
    subprocess.run(['rsvg-convert','-w',str((w+2*PAD)*esc),'-h',str((h+2*PAD)*esc),'-o',png,tmp],check=True)
    os.remove(tmp)
    idx.append({'id':e['id'],'png':e['id']+'.png','escala':esc,'largura':(w+2*PAD)*esc,'altura':(h+2*PAD)*esc,'margem':PAD})
json.dump({'escala':esc,'grupos':idx},open(os.path.join(out,'pack.json'),'w'),indent=1)
open(os.path.join(out,'pack.txt'),'w').write(''.join(f"{i['id']} {i['png']} {i['escala']} {i['largura']} {i['altura']} {i['margem']}\n" for i in idx))
print(len(idx),'renderizados em',out,'escala',esc)

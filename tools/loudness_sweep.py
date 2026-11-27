#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Every punch, alone or in pairs, against the same scene without it: how much louder (dB, 100 ms windows) while held
   and in its tail. ./build.sh --host first.  tools/loudness_sweep.py 1,7,11 single|pairs ['[[macro,value],...]'] [only-over-dB]"""
import wave,math,array,subprocess,os,sys,itertools,json
from concurrent.futures import ThreadPoolExecutor
KEY=[1,3,5,8,10,13,15,17,20,22,25,0]
NAME=['BURN','VOID','SHATTER','WARBLE','INFINITE','SHIMMER','THROW','RING','DECIMATE','GATE','SCREAM','ROTATE']
BASE='/tmp/monodrone-sweep'
os.makedirs(BASE,exist_ok=True)
def run(job):
    scene,combo,macros=job
    tag=f"s{scene}_{'-'.join(map(str,combo))}_{'-'.join(f'{a}{b}' for a,b in macros)}"
    d=f"{BASE}/{tag}"; os.makedirs(d,exist_ok=True)
    t=""
    if scene!=1: t+=f"wait 300\npress SEL\ntapkey w{scene-1}\nrelease SEL\n"
    t+="wait 600\ntap PLAY\nwait 4500\n"
    for a,b in macros: t+=f"expect setmc{a*1000+b} 1\n"
    t+="wait 1500\ntap FX\nwav o.wav\nwait 2000\n"
    for k in combo: t+=f"key {KEY[k]} down\n"
    t+="wait 1800\n"
    for k in combo: t+=f"key {KEY[k]} up\n"
    t+="wait 2500\n"
    open(d+'/t.txt','w').write(t)
    subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','app','build','host','monodrone_host'),d+'/t.txt',d],capture_output=True)
    w=wave.open(d+'/o.wav');n=w.getnframes();sw=w.getsampwidth();ch=w.getnchannels();fr=w.getframerate()
    a=array.array('h' if sw==2 else 'i');a.frombytes(w.readframes(n));full=2**(8*sw-1)
    st=fr//10;r=[]
    for i in range(0,n,st):
        seg=a[i*ch:(i+st)*ch]
        if len(seg)<50: break
        r.append(20*math.log10(math.sqrt(sum(x*x for x in seg)/len(seg))/full+1e-9))
    return (scene,combo,macros,r)
jobs=[]
scenes=[int(x) for x in sys.argv[1].split(',')]
mode=sys.argv[2]
macros=json.loads(sys.argv[3]) if len(sys.argv)>3 else []
for s in scenes:
    jobs.append((s,(),macros))
    if mode=='single': combos=[(i,) for i in range(12)]
    else: combos=list(itertools.combinations(range(12),2))
    for c in combos: jobs.append((s,c,macros))
with ThreadPoolExecutor(4) as ex: res=list(ex.map(run,jobs))
ctrl={s:r for s,c,m,r in res if c==()}
out=[]
for s,c,m,r in res:
    if c==(): continue
    k=ctrl[s]; n=min(len(r),len(k))
    d=[r[i]-k[i] for i in range(n)]
    out.append((s,c,round(sum(k[2:19])/17,1),round(max(d[20:40]),1),round(max(d[40:]),1),round(min(d[20:]),1)))
lim=float(sys.argv[4]) if len(sys.argv)>4 else -99
for s,c,b,du,af,lo in sorted(out,key=lambda x:-max(x[3],x[4])):
    if max(du,af)<lim: continue
    print(f"scene{s:>2} {'+'.join(NAME[i] for i in c):<20} base {b:6} during {du:+5} after {af:+5} lowest {lo:+6}")

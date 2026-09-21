import re,glob,html,json,sys
def lits(t,i):
    out=[];n=len(t)
    while True:
        while i<n and t[i] in ' \t\r\n': i+=1
        if t.startswith('//',i):
            i=t.index('\n',i);continue
        if i<n and t[i]=='"':
            j=i+1;buf=''
            while t[j]!='"':
                if t[j]=='\\': buf+=t[j:j+2];j+=2
                else: buf+=t[j];j+=1
            out.append(buf);i=j+1
        else: return out,i
def dec(s): return s.encode('latin1','backslashreplace').decode('unicode_escape') if False else re.sub(r'\\(.)',lambda m:{'n':'\n','t':'\t','"':'"','\\':'\\'}.get(m.group(1),m.group(1)),s)
def sources(demo):
    src={}
    for f in glob.glob(demo+'/*.cpp')+glob.glob(demo+'/*.h')+glob.glob(demo+'/*.mm'):
        t=open(f,encoding='utf8').read()
        for m in re.finditer(r'(?:\btr|QCoreApplication::translate\(\s*"[^"]*"\s*,)\s*\(\s*',t) if False else re.finditer(r'(?<![A-Za-z_])(?:tr\(|translate\(\s*"[^"]*"\s*,\s*)',t):
            l,_=lits(t,m.end())
            if l:
                s=dec(''.join(l))
                if re.search(r'[一-鿿]',s): src[s]=f
    return src
def tsmap(p):
    ts=open(p,encoding='utf8').read();d={}
    for m in re.finditer(r'<message>(.*?)</message>',ts,re.S):
        b=m.group(1);s=re.search(r'<source>(.*?)</source>',b,re.S);tr=re.search(r'<translation([^>]*)>(.*?)</translation>',b,re.S)
        if s: d[html.unescape(s.group(1))]=(html.unescape(tr.group(2)) if tr else '',tr.group(1) if tr else '')
    return d
if __name__=='__main__':
    demo=sys.argv[1]
    src=sources(demo);ts=tsmap(demo+'/i18n/imrtc_demo_en.ts')
    miss=[s for s in src if s not in ts or not ts[s][0] or 'unfinished' in ts[s][1]]
    print('源串',len(src),'缺/未译',len(miss))
    for s in miss: print(' -',repr(s)[:90],src[s].split('/')[-1])
    print('ts多余',[s[:30] for s in ts if s not in src][:10])

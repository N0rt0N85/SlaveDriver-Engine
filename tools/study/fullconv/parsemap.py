import re,sys,collections
path=sys.argv[1]
# GNU ld map: section lines " .bss           0x060xxxxx     0xSIZE build/stext/obj/FOO.o" then symbol lines "                0x0600xxxx                sym"
sec=None
entries=[]  # (sec, addr, size, obj)
syms=[]  # (sec, addr, name, obj)
cur_obj=None
lines=open(path,encoding='utf-8',errors='replace').read().splitlines()
pending=None
i=0
top=None
while i<len(lines):
    l=lines[i]
    m=re.match(r'^(\.[A-Za-z_.0-9]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s*$',l)
    if m: top=m.group(1); i+=1; continue
    m=re.match(r'^(\.[A-Za-z_.0-9]+)\s*$',l)
    if m: top=m.group(1); i+=1; continue
    m=re.match(r'^ (\.[A-Za-z_.0-9*]+)\s*$',l)
    if m:
        pending=m.group(1)
        # next line has addr size obj
        n=lines[i+1] if i+1<len(lines) else ''
        m2=re.match(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$',n)
        if m2:
            entries.append((top,pending,int(m2.group(1),16),int(m2.group(2),16),m2.group(3).strip()))
            cur_obj=m2.group(3).strip(); i+=2; continue
        i+=1; continue
    m=re.match(r'^ (\.[A-Za-z_.0-9*]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$',l)
    if m:
        entries.append((top,m.group(1),int(m.group(2),16),int(m.group(3),16),m.group(4).strip()))
        cur_obj=m.group(4).strip(); i+=1; continue
    m=re.match(r'^\s+0x([0-9a-f]+)\s+(\S+)\s*$',l)
    if m and cur_obj:
        syms.append((top,int(m.group(1),16),m.group(2),cur_obj))
    i+=1
mode=sys.argv[2] if len(sys.argv)>2 else 'obj'
if mode=='obj':
    agg=collections.defaultdict(lambda: collections.Counter())
    for top,sub,a,s,obj in entries:
        agg[obj][top]+=s
    for obj,c in sorted(agg.items(), key=lambda kv:-sum(kv[1].values())):
        print(f"{obj}\t"+"\t".join(f"{k}={v}" for k,v in sorted(c.items(), key=lambda kv: str(kv[0]))))
elif mode=='syms':
    # estimate symbol sizes by delta to next symbol within same section top
    filt=sys.argv[3] if len(sys.argv)>3 else None
    out=[]
    # sort by address
    bysec=collections.defaultdict(list)
    for top,a,n,o in syms: bysec[top].append((a,n,o))
    for top,lst in bysec.items():
        if filt and top!=filt: continue
        lst.sort()
        # section end
        ends=[a+s for t,sub,a,s,o in entries if t==top]
        secend=max(ends) if ends else 0
        for k,(a,n,o) in enumerate(lst):
            nxt=lst[k+1][0] if k+1<len(lst) else secend
            out.append((nxt-a,a,n,o,top))
    out.sort(reverse=True)
    for s,a,n,o,top in out[:int(sys.argv[4]) if len(sys.argv)>4 else 40]:
        print(f"{s:8d}\t0x{a:08x}\t{n}\t{o}\t{top}")
elif mode=='secs':
    for top,sub,a,s,obj in entries:
        print(f"{top}\t{sub}\t0x{a:08x}\t{s}\t{obj}")

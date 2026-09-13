import re, os, sys, collections
ROOT = r"C:/Users/pcico/Projects/SlaveDriver-Engine/refs/build/jfduke3d/src"
GAME = ["game.c","actors.c","player.c","sector.c","premap.c","gamedef.c","sounds.c","rts.c","menues.c","global.c","config.c","osdcmds.c","grpscan.c","duke3d.h","funct.h"]
def strip(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    return s
src = {f: strip(open(os.path.join(ROOT,f), encoding='latin-1').read()) for f in GAME}
CATS = {
 "physique/collision": ["clipmove","pushmove","getzrange","hitscan","cansee","neartag","updatesector","updatesectorz","inside","clipinsidebox","clipinsideboxline"],
 "geometrie pure": ["lintersect","rintersect","rotatepoint","getangle","ksqrt","krand","nextsectorneighborz","getflorzofslope","getceilzofslope","getzsofslope","alignflorslope","alignceilslope","sectorofwall","lastwall","loopnumofsector"],
 "listes sprites (structure)": ["setsprite","setspritez","insertsprite","deletesprite","changespritesect","changespritestat","initspritelists","dragpoint","setfirstwall"],
 "rendu 3D": ["drawrooms","drawmasks","drawmapview","preparemirror","completemirror","setviewtotile","setviewback","setrollangle","setview","setaspect","clearview","clearallviews","nextpage","flushperms","setgamemode","getrendermode","setrendermode","setvgapalette","setbrightness","setpalettefade","makepalookup","plotpixel","getpixel","showframe"],
 "rendu 2D": ["rotatesprite","drawline256","printext256"],
 "donnees ART/MAP": ["loadboard","loadpics","loadtile","allocatepermanenttile","copytilepiece","squarerotatetile","qloadkvx","loadmaphack","saveboard","invalidatetile"],
 "fichiers/cache (cache1d)": ["kopen4load","kread","klseek","kclose","kfilelength","ktell","kgetc","initgroupfile","uninitgroupfile","kdfread","kdfwrite","dfread","dfwrite","allocache","suckcache","agecache","initcache","addsearchpath","findfrompath","klistpath","klistfree"],
 "baselayer (OS)": ["handleevents","getticks","inittimer","uninittimer","sampletimer","initinput","uninitinput","readmousexy","readmousebstatus","grabmouse","bgetchar","bkbhit","bflushchars","bgetkey","bflushkeys","setvideomode","checkvideomode","getvalidmodes","initengine","uninitengine","preinitengine","buildprintf","buildputs","initprintf","debugprintf","wm_msgbox","wm_ynbox","wm_setapptitle"],
 "reseau (mmulti)": ["sendpacket","getpacket","initmultiplayers","uninitmultiplayers","initmultiplayerscycle","sendlogon","sendlogoff","getoutputcirclesize","setsocket","flushpackets","genericmultifunction"],
 "OSD/console": ["OSD_Printf","OSD_Draw","OSD_RegisterFunction","OSD_HandleChar","OSD_HandleKey","OSD_Init","OSD_SetFunctions","OSD_SetParameters","OSD_Dispatch","OSD_ResizeDisplay","OSD_CaptureInput","OSD_ShowDisplay","OSD_SetVersionString","OSD_GetCols","OSD_GetRows","OSD_GetTextMode","OSD_SetTextMode","OSD_Puts"],
}
DATA = ["sector","wall","sprite","tsprite","spritesortcnt","headspritesect","nextspritesect","prevspritesect","headspritestat","nextspritestat","prevspritestat","tilesizx","tilesizy","picanm","picsiz","waloff","walock","numsectors","numwalls","numtiles","totalclock","sintable","palette","palookup","numpalookups","visibility","parallaxvisibility","parallaxtype","parallaxyoffs","parallaxyscale","pskyoff","pskybits","xdim","ydim","windowx1","windowy1","windowx2","windowy2","yxaspect","viewingrange","show2dsector","show2dwall","show2dsprite","gotpic","gotsector","automapping","randomseed","numframes","startumost","startdmost","ylookup","spriteext","guniqhudid","showinvisibility","curpalette","palfadedelta","usemodels","usevoxels","tiletovox"]
PRAG = ["mulscale","dmulscale","tmulscale","divscale","scale","klabs","ksgn","sqr","clearbuf","clearbufbyte","copybuf","copybufbyte","copybufreverse","qinterpolatedown16","qinterpolatedown16short","krecip","swapshort","swaplong","swapchar","umin","umax","kmin","kmax","sgn","min","max"]

def count_fn(name):
    per = {}
    for f,s in src.items():
        n = len(re.findall(r'(?<![\w.>])'+re.escape(name)+r'\s*\(', s))
        if n: per[f]=n
    return per
def count_data(name):
    per = {}
    for f,s in src.items():
        n = len(re.findall(r'(?<![\w.>])'+re.escape(name)+r'\s*\[', s))
        if n: per[f]=n
    return per
def count_word(name):
    per = {}
    for f,s in src.items():
        n = len(re.findall(r'(?<![\w.>])'+re.escape(name)+r'(?![\w])', s))
        if n: per[f]=n
    return per
def count_prag(base):
    per = {}
    for f,s in src.items():
        n = len(re.findall(r'(?<![\w.>])'+re.escape(base)+r'[0-9]*\s*\(', s))
        if n: per[f]=n
    return per

def fmt(per):
    return ", ".join(f"{k}:{v}" for k,v in sorted(per.items(), key=lambda x:-x[1]))

print("## FONCTIONS (appels dans le code jeu)")
grand = collections.Counter()
for cat, fns in CATS.items():
    print(f"\n### {cat}")
    tot=0
    for fn in fns:
        per = count_fn(fn)
        n = sum(per.values()); tot+=n
        if n: print(f"{fn}: {n} | {fmt(per)}")
    grand[cat]=tot
    print(f"-- total {cat}: {tot}")
print("\n## TOTAUX PAR CATEGORIE"); 
for k,v in grand.items(): print(f"{k}: {v}")
print("\n## DONNEES (acces tableau/variable)")
for d in DATA:
    per = count_data(d) if d not in ("spritesortcnt","numsectors","numwalls","numtiles","totalclock","visibility","parallaxvisibility","parallaxtype","parallaxyoffs","parallaxyscale","pskybits","xdim","ydim","windowx1","windowy1","windowx2","windowy2","yxaspect","viewingrange","automapping","randomseed","numframes","guniqhudid","showinvisibility","palfadedelta","usemodels","usevoxels","numpalookups") else count_word(d)
    n=sum(per.values())
    if n: print(f"{d}: {n} | {fmt(per)}")
print("\n## PRAGMAS (macros fixed-point BUILDLIC)")
ptot=0
for p in PRAG:
    per = count_prag(p)
    n=sum(per.values()); ptot+=n
    if n: print(f"{p}*: {n} | {fmt(per)}")
print(f"-- total pragmas: {ptot}")
print("\n## Cadence")
for w in ["TICSPERFRAME","TICRATE","MOVEFIFOSIZ","THISISNOTREALLYATIC","totalclock","ototalclock","lockclock","gametic"]:
    per=count_word(w); print(f"{w}: {sum(per.values())} | {fmt(per)}")

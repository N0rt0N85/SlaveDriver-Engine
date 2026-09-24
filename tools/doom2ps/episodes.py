#!/usr/bin/env python3
"""episodes.py -- les episodes du disque, lus dans le .cfg, ecrits en tables C.

Le Makefile fabrique $(BUILD)/doom_episodes.h avec ce script, comme il fabrique gameparams.h :
game/doom/DOOM.H l'inclut et DOOM_GAME.C, DOOM_HUD.C et PAUSE.C y prennent leurs tables. Rien
de tout cela n'est plus ecrit dans le C -- c'est ce qui permet de se faire SON disque sans
toucher au moteur : un .cfg, un WAD, `make iso`.

Les cles, dans params/<jeu>.cfg (n = 1, 2, 3... sans trou) :

    TITLE_LOGO        le lump du WAD que l'ecran titre et l'ecran de chargement affichent
    TITLE_LOGO_ROWS   ses lignes d'ecran gardees a partir du haut (0 = tout le lump)
    TITLE_LOGO_Y      la ligne d'ecran ou il commence
    EPISODEn_NAME     ce que NOUVELLE PARTIE affiche
    EPISODEn_MAPS     les cartes de la voie NORMALE, dans l'ordre : la sortie de chacune mene a
                      la suivante, la derniere termine l'episode
    EPISODEn_SECRET   les detours, par triplets <depuis> <carte secrete> <retour> ; les cartes
                      secretes sont ajoutees a la suite des normales
    EPISODEn_TITLES   ce que le HUD ecrit, une par carte, separees par | et dans l'ordre FINAL
                      (les normales puis les secretes) ; le nom de la carte est mis devant
    EPISODEn_PAR      le temps de reference en secondes, une par carte, meme ordre
    EPISODEn_BOSS     les patrons, par paires <carte> <MT_...> [nombre] ; vide = aucun

L'ordre final des niveaux est celui des `MAPS` puis celui des cartes secretes, episode apres
episode -- et c'est cet INDEX que le moteur manipule partout. Une sauvegarde le porte, donc elle
porte aussi le nom de sa carte (DOOM_SAVE.C) : deux disques n'ont pas la meme liste.

Usage : episodes.py <cfg> <out.h>
        episodes.py --maps <cfg>      les cartes, une par ligne (pour le convertisseur)
        episodes.py --get KEY <cfg>   une valeur (pour le Makefile)
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
if os.path.join(ROOT, "tools") not in sys.path:
    sys.path.insert(0, os.path.join(ROOT, "tools"))
import gameparams                                       # noqa: E402

DEFAULT_LOGO = "M_DOOM"


class CfgError(Exception):
    pass


def _cfg(path):
    return dict(gameparams.read(path))


def episodes(path):
    """-> [ {name, maps, titles, par, boss, secret} ], les cartes deja dans leur ordre final."""
    cfg = _cfg(path)
    out = []
    n = 1
    while ("EPISODE%d_MAPS" % n) in cfg:
        p = "EPISODE%d_" % n
        maps = cfg[p + "MAPS"].split()
        if not maps:
            raise CfgError("%sMAPS est vide" % p)
        # les detours : <depuis> <carte secrete> <retour>, les cartes secretes a la suite
        sec = cfg.get(p + "SECRET", "").split()
        if len(sec) % 3:
            raise CfgError("%sSECRET veut des triplets <depuis> <secrete> <retour>, pas %d mots"
                           % (p, len(sec)))
        detours = [tuple(sec[i:i + 3]) for i in range(0, len(sec), 3)]
        order = list(maps) + [d[1] for d in detours]
        if len(set(order)) != len(order):
            raise CfgError("%s: une carte est citee deux fois (%s)" % (p, " ".join(order)))
        for frm, smap, back in detours:
            for m in (frm, back):
                if m not in order:
                    raise CfgError("%sSECRET cite %s, qui n'est dans aucune liste" % (p, m))
        titles = [t.strip() for t in cfg.get(p + "TITLES", "").split("|")] if cfg.get(p + "TITLES") else []
        par = cfg.get(p + "PAR", "").split()
        boss = cfg.get(p + "BOSS", "").split()
        for what, got in (("TITLES", titles), ("PAR", par)):
            if got and len(got) != len(order):
                raise CfgError("%s%s donne %d valeurs pour %d cartes"
                               % (p, what, len(got), len(order)))
        out.append(dict(name=cfg.get(p + "NAME", "EPISODE %d" % n), maps=maps,
                        order=order, detours=detours, titles=titles, par=par, boss=boss))
        n += 1
    if not out:
        raise CfgError("%s ne declare aucun EPISODE1_MAPS" % path)
    return out


def tables(path):
    """-> dict des tables, index GLOBAL (episode apres episode)."""
    eps = episodes(path)
    order, first = [], []
    for e in eps:
        first.append(len(order))
        order += e["order"]
    idx = {m: i for i, m in enumerate(order)}
    nxt = [-1] * len(order)
    secret = [-1] * len(order)
    titles = [""] * len(order)
    par = [0] * len(order)
    bossmt = ["-1"] * len(order)
    bosscount = [0] * len(order)
    for e, base in zip(eps, first):
        # la voie normale : chacune mene a la suivante, la derniere termine l'episode
        for i, m in enumerate(e["maps"]):
            nxt[idx[m]] = idx[e["maps"][i + 1]] if i + 1 < len(e["maps"]) else -1
        # un detour : <depuis> prend une sortie secrete vers <secrete>, qui ressort sur <retour>
        for frm, smap, back in e["detours"]:
            nxt[idx[smap]] = idx[back]
            secret[idx[frm]] = idx[smap]
        for i, m in enumerate(e["order"]):
            g = base + i
            titles[g] = "%s: %s" % (m, e["titles"][i]) if e["titles"] else m
            if e["par"]:
                par[g] = int(e["par"][i])
        b = e["boss"]
        i = 0
        while i < len(b):
            if b[i] not in idx:
                raise CfgError("BOSS cite %s, qui n'est dans aucune liste" % b[i])
            g = idx[b[i]]
            bossmt[g] = b[i + 1]
            cnt, step = 1, 2
            if i + 2 < len(b) and b[i + 2].isdigit():
                cnt, step = int(b[i + 2]), 3
            bosscount[g] = cnt
            i += step
    # une carte secrete non citee par un detour n'a pas de sortie : elle terminerait la partie
    for i, m in enumerate(order):
        if secret[i] < 0:
            secret[i] = nxt[i]
    cfg = _cfg(path)
    return dict(eps=eps, order=order, first=first, next=nxt, secret=secret,
                titles=titles, par=par, bossmt=bossmt, bosscount=bosscount,
                logo=cfg.get("TITLE_LOGO", DEFAULT_LOGO),
                logo_rows=int(cfg.get("TITLE_LOGO_ROWS", 0)),
                logo_y=int(cfg["TITLE_LOGO_Y"]) if "TITLE_LOGO_Y" in cfg else None)


def header(path):
    t = tables(path)
    q = lambda s: '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')     # noqa: E731
    L = ["/* doom_episodes.h -- generated from %s by the Makefile (tools/doom2ps/episodes.py)."
         % os.path.basename(path),
         "   Do not edit: the episodes, their maps and their order are the .cfg's. */",
         "#ifndef DOOM_EPISODES_H", "#define DOOM_EPISODES_H",
         "#define DOOM_NMLEVELS   %d" % len(t["order"]),
         "#define DOOM_NMEPISODES %d" % len(t["eps"]),
         "#define DOOM_EP_LOGO    %s" % q(t["logo"]),
         "#define DOOM_EP_FILES   %s" % ",".join(q("+%s.LEV" % m) for m in t["order"]),
         "#define DOOM_EP_NEXT    %s" % ",".join(str(v) for v in t["next"]),
         "#define DOOM_EP_SECRET  %s" % ",".join(str(v) for v in t["secret"]),
         "#define DOOM_EP_TITLES  %s" % ",".join(q(s) for s in t["titles"]),
         "#define DOOM_EP_PAR     %s" % ",".join(str(v) for v in t["par"]),
         "#define DOOM_EP_BOSSMT  %s" % ",".join(t["bossmt"]),
         "#define DOOM_EP_BOSSCNT %s" % ",".join(str(v) for v in t["bosscount"]),
         "#define DOOM_EP_NAMES   %s" % ",".join(q(e["name"]) for e in t["eps"]),
         "#define DOOM_EP_FIRST   %s" % ",".join(str(v) for v in t["first"]),
         "#endif"]
    return "\n".join(L) + "\n"


def main(argv):
    if len(argv) == 3 and argv[1] == "--maps":
        for m in tables(argv[2])["order"]:
            print(m)
        return 0
    if len(argv) == 4 and argv[1] == "--get":
        print(tables(argv[3]).get(argv[2], ""))
        return 0
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    cfg, out = argv[1], argv[2]
    text = header(cfg)
    if os.path.exists(out) and open(out, encoding="utf-8").read() == text:
        return 0                                        # ne pas retoucher la date: tout en depend
    tmp = out + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, out)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except CfgError as e:
        print("episodes.py: %s" % e, file=sys.stderr)
        sys.exit(1)

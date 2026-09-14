#!/usr/bin/env python3
"""sim_player.py -- PC simulator of the Doom player integrator and of the psprite machine
(game/doom/DOOM_PLAYER.C, DOOM_WEAPON.C), SPEC_PLAYER section 6 tasks 1 and 3.

Replays, in the engine's 16.16 integer arithmetic (fixMul = 64-bit product >> 16):
  * 35 tics of running forward: mom_{n+1} = mom_n * 0xE800 + 0x32 * 2048, asymptote
    1.5625 / (1 - 0.90625) = 16.67 u/tic (Doom p_user.c P_Thrust + p_mobj.c FRICTION);
  * the per-frame velocity written to camera->vel (mom * 38229) and read back (* 112347);
  * the pistol state cycle over doomStates[] parsed from game/doom/DOOM_TABLES.C with the
    fire button held: tics between two A_FirePistol = 14 (S_PISTOL1 4 + S_PISTOL2 6 +
    S_PISTOL3 4; A_ReFire in S_PISTOL4 restarts the attack on entry).

Usage:
    python tools/doom2ps/sim_player.py [--tables game/doom/DOOM_TABLES.C] [--tics 35]
Exit status 1 when a check fails.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FRICTION = 0xE800
STOPSPEED = 0x1000
MAXMOVE = 30 << 16
FORWARDMOVE = [0x19, 0x32]
VEL_SCALE = 38229
VEL_INV = 112347


def fixmul(a, b):
    """SH-2 dmuls.l + xtrct: floor((a*b) / 2^16) on the 64-bit product."""
    return (a * b) >> 16


def sim_run(tics, run=1, cos=1 << 16):
    """Forward thrust every tic on flat ground; returns the list of |mom| in u/tic (float)."""
    mom = 0
    out = []
    for _ in range(tics):
        # tic start: read back from the engine velocity of the last tic (DOOM_PLAYER.C doomMoveTic)
        vel = fixmul(mom, VEL_SCALE)
        mom = fixmul(vel, VEL_INV)
        # P_XYMovement tail (previous move): friction (a command is present: never stops)
        mom = fixmul(mom, FRICTION)
        # P_MovePlayer: thrust
        mom += fixmul(FORWARDMOVE[run] * 2048, cos)
        mom = max(-MAXMOVE, min(MAXMOVE, mom))
        out.append(mom / 65536.0)
    return out


STATE_RE = re.compile(r"\{\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\},\s*/\*\s*(\d+)\s+(\S+)\s*\*/")


def load_states(path):
    """doomStates[] of DOOM_TABLES.C: index -> (sprite, frame, tics, nextstate, action, flags, name)."""
    states = {}
    names = {}
    with open(path, encoding="latin-1") as f:
        text = f.read()
    start = text.index("doomStates[NUMSTATES]")
    for m in STATE_RE.finditer(text, start):
        idx = int(m.group(7))
        states[idx] = tuple(int(m.group(i)) for i in range(1, 7)) + (m.group(8),)
        names[m.group(8)] = idx
        if m.group(8) == "S_PLAY":
            break
    return states, names


def sim_pistol(states, names, tics=200):
    """P_SetPsprite / P_MovePsprites with the fire button held from the ready state; returns
    the tic numbers at which A_FirePistol runs."""
    fires = []
    tic = [0]
    psp = {"state": 0, "tics": 0}

    def set_psprite(stnum):
        while True:
            if not stnum:
                psp["state"] = 0
                psp["tics"] = -1
                return
            spr, frame, st_tics, nxt, action, flags, name = states[stnum]
            psp["state"] = stnum
            psp["tics"] = st_tics
            if name == "S_PISTOL2":            # A_FirePistol
                fires.append(tic[0])
            elif name == "S_PISTOL":           # A_WeaponReady: fire held -> P_FireWeapon
                set_psprite(names["S_PISTOL1"])
                return
            elif name == "S_PISTOL4":          # A_ReFire: fire held -> P_FireWeapon
                set_psprite(names["S_PISTOL1"])
                return
            stnum = states[psp["state"]][3]
            if psp["tics"]:
                return

    set_psprite(names["S_PISTOL"])
    for _ in range(tics):
        tic[0] += 1
        if psp["state"] and psp["tics"] != -1:
            psp["tics"] -= 1
            if psp["tics"] == 0:
                set_psprite(states[psp["state"]][3])
    return fires


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tables", default=os.path.join(ROOT, "game", "doom", "DOOM_TABLES.C"))
    ap.add_argument("--tics", type=int, default=35)
    args = ap.parse_args()
    ok = True

    speeds = sim_run(args.tics)
    print("run forward, u/tic per tic:")
    print("  " + " ".join("%.2f" % s for s in speeds))
    asym = FORWARDMOVE[1] * 2048 / 65536.0 / (1 - FRICTION / 65536.0)
    print("  asymptote %.2f u/tic (Doom 16.67); tic %d = %.2f (%.1f%%)" %
          (asym, args.tics, speeds[-1], 100.0 * speeds[-1] / asym))
    if abs(asym - 16.67) > 0.01 or speeds[-1] < 0.95 * asym:
        ok = False
    walk = sim_run(args.tics, run=0)
    print("  walk asymptote %.2f u/tic (Doom 8.33), tic %d = %.2f" % (
        FORWARDMOVE[0] * 2048 / 65536.0 / (1 - FRICTION / 65536.0), args.tics, walk[-1]))
    # frame scale: 60 Hz velocity of one tic of running at the asymptote = 16.67 * 35/60
    vel = fixmul(int(asym * 65536), VEL_SCALE) / 65536.0
    print("  camera->vel at the asymptote = %.3f u/frame (x 60/35 = %.2f)" % (vel, vel * 60 / 35))

    states, names = load_states(args.tables)
    fires = sim_pistol(states, names)
    gaps = [b - a for a, b in zip(fires, fires[1:])]
    print("pistol, fire held: A_FirePistol at tics %s; intervals %s" % (fires[:6], gaps[:5]))
    if not gaps or any(g != 14 for g in gaps):
        ok = False
    print("OK" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

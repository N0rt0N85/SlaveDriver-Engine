#!/bin/bash
cd "C:/Users/pcico/Projects/Mimas/core"
PLAY="p_*.c g_game.c s_sound.c p_saveg.c am_map.c st_stuff.c hu_stuff.c"
echo "== sector_t fields (playsim files: count of lines with ->field or .field) =="
for f in floorheight ceilingheight floorpic ceilingpic lightlevel special tag soundtraversed soundtarget blockbox soundorg validcount thinglist specialdata linecount lines; do
  n=$(grep -E "(->|\.)$f\b" $PLAY 2>/dev/null | wc -l); r=$(grep -E "(->|\.)$f\b" r_*.c 2>/dev/null | wc -l); echo "  $f: playsim=$n renderer=$r"; done
echo "== line_t =="
for f in v1i v2i v1 v2 dx dy flags special tag sidenum bbox bbox16 slope slopetype frontsector backsector validcount specialdata; do
  n=$(grep -E "(->|\.)$f\b" $PLAY 2>/dev/null | wc -l); r=$(grep -E "(->|\.)$f\b" r_*.c 2>/dev/null | wc -l); echo "  $f: playsim=$n renderer=$r"; done
echo "== side_t =="
for f in textureoffset rowoffset toptexture bottomtexture midtexture seci sector; do
  n=$(grep -E "(->|\.)$f\b" $PLAY 2>/dev/null | wc -l); r=$(grep -E "(->|\.)$f\b" r_*.c 2>/dev/null | wc -l); echo "  $f: playsim=$n renderer=$r"; done
echo "== seg_t / subsector_t / node_t in playsim =="
grep -l -E "\bseg_t\b|\bsegs\b" $PLAY 2>/dev/null | tr '\n' ' '; echo
grep -n -E "\bsegs\b|seg_t" $PLAY 2>/dev/null | head -8
grep -n -E "\bnodes\b|node_t" $PLAY 2>/dev/null | head -8
grep -n -E "subsectors\b|subsector_t|->subsector\b" $PLAY 2>/dev/null | wc -l
echo "== R_PointInSubsector users in playsim =="; grep -n "R_PointInSubsector" $PLAY 2>/dev/null | head
echo "== blockmap/reject users =="; grep -l -E "\bblockmap|blocklinks|rejectmatrix" $PLAY r_*.c 2>/dev/null | tr '\n' ' '; echo

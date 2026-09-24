#define GOODEND 329084902
#define BADEND  928473932
#define SUPERGOODEND 184748923

/* GCC14: INIT tells MAIN which system clock to ask the SMPC for.  Word 0x02ffff8, beside the
   ending token at 0x02ffffc; mem area 0 allocates upward from 0x200000 (UTIL.C areaEnd[0]).
   MAIN must read it BEFORE the clock change: low work RAM is DRAM and the SMPC wipes it. */
#define CLK352_ADDR  0x02ffff8
#define CLK352_TOKEN 0x33353200

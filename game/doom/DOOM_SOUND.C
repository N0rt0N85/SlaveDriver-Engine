/* DOOM_SOUND.C -- sfx_* -> engine sound index, and the four play helpers.
 * Contract section 3 (static list in STATIC.DAT, dynamic list in the .LEV indexed by sfxenum_t),
 * SPEC_PLAYER section 4.1 (the ONE definition; SPEC_RUNTIME section 8 only calls these).
 *
 * Static sounds: doomStaticSfx[20] (generated, contract order PISTOL SHOTGN PUNCH ... RLAUNC)
 * live in group ST_JOHN of STATIC.DAT => index = level_staticSoundMap[ST_JOHN] + n.
 * Dynamic sounds: level_objectSoundMap[sfx] (227 shorts, indexed by sfxenum_t for Doom,
 * -1000 when absent, already shifted by nmStaticSounds in loadDynamicSounds SOUND.C:243-249)
 * => still < 0 when absent.  Complete. */
#include "util.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "sruins.h"
#include "sound.h"
#include "doom.h"

int doom_sfxIndex(int sfx)
{int n;
 assert(sfx>=0 && sfx<NUMSFX);
 for (n=0;n<DOOM_NMSTATICSFX;n++)
    if (doomStaticSfx[n]==sfx)
       return level_staticSoundMap[ST_JOHN]+n;
 n=level_objectSoundMap[sfx];
 return (n<0)?-1:n;
}

/* positional one-shot from a sprite; s == NULL (pickups, S_StartSound(NULL,...)) => unpositioned */
void doom_sound(Sprite *s,int sfx)
{int idx=doom_sfxIndex(sfx);
 if (idx<0)
    return;
 if (s)
    spriteMakeSound(s,idx);
 else
    playSound(0,idx);
}

/* source = the player object (so stopAllSound((int)player) can silence it) */
void doom_playerSound(int sfx)
{int idx=doom_sfxIndex(sfx);
 if (idx<0)
    return;
 playSound((int)player,idx);
}

/* positional one-shot at an arbitrary point (impact puffs, explosions) */
void doom_posSound(Object *src,MthXyz *pos,int sfx)
{int idx=doom_sfxIndex(sfx);
 assert(pos);
 if (idx<0)
    return;
 posMakeSound((int)src,pos,idx);
}

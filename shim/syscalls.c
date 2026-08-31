/* GCC14: newlib syscall override.  -specs=nosys.specs pulls libnosys, whose _sbrk hands out memory
   upward from `end` -- exactly the address the game's own allocator uses as the base of its
   mem_malloc(1,...) area (UTIL.C: mem2Start=(int)&end).  No reachable code path allocates through
   newlib today (sprintf is integer-only, abort() is no longer synthesized since
   -fno-delete-null-pointer-checks), so an accidental allocation must FAIL instead of silently
   corrupting the game heap.  This object precedes -lnosys in the link, so it wins. */
#include <errno.h>

void *_sbrk(int incr)
{
 (void)incr;
 errno=ENOMEM;
 return (void *)-1;
}

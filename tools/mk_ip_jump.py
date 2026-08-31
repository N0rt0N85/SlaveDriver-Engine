#!/usr/bin/env python3
"""mk_ip_jump.py <SRL IP.BIN> <out.bin> -- make a "jump-only" Saturn IP from SRL's generic (SGL) IP.

Keeps the header (0x000-0x0FF, area "JTUE"), SEGA's security code (0x100-0xDFF, byte-identical
to the one on the retail PowerSlave disc) and the four area-code entries (0xE00-0xE7F, each a
`bra` over its text, chained to the boot program at 0xE80).  The SGL boot program at 0xE80 (system
init: VDP/SCU/sound-RAM clear, vblank vectors, cache purge, then jump) is replaced by what the
retail PowerSlave IP has right after its single area code (offset 0xE20 there):

    d0 01      mov.l @(1,pc),r0     ; r0 = 0x06004000
    40 2b      jmp   @r0
    00 09      nop
    00 00      (pad)
    06 00 40 00  .long 0x06004000

so the game's own megaInit() is the first system init to run, exactly as on the retail disc.
Header field 0xE0 (IP size) is set to the end of the stub (0xE8C), the retail convention (0xE2C).
Accepts MSYS paths (/c/...) because the Makefile runs it from MSYS make with Windows Python."""
import sys

def winpath(p):
    if len(p) > 3 and p[0] == '/' and p[2] == '/' and p[1].isalpha():
        return p[1].upper() + ':' + p[2:]
    return p

src, dst = winpath(sys.argv[1]), winpath(sys.argv[2])
ip = bytearray(open(src, 'rb').read())
assert ip[:16] == b'SEGA SEGASATURN ', 'not a Saturn IP'
assert ip[0xE00:0xE04] == bytes.fromhex('a00e0009') and ip[0xE60:0xE64] == bytes.fromhex('a00e0009'), \
    'expected 4 area-code entries at 0xE00-0xE7F (SRL/SGL IP.BIN)'
stub = bytes.fromhex('d001402b00090000') + (0x06004000).to_bytes(4, 'big')
ip[0xE80:] = stub + bytes(len(ip) - 0xE80 - len(stub))
ip[0xE0:0xE4] = (0xE80 + len(stub)).to_bytes(4, 'big')
open(dst, 'wb').write(ip)
print('%s: %d bytes, boot program = jmp 0x06004000 (IP size field 0x%X)' % (dst, len(ip), 0xE80 + len(stub)))

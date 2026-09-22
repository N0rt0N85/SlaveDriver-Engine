#!/usr/bin/env python3
"""iso2bin.py -- ISO 9660 (MODE1/2048) -> raw MODE1/2352 image + CUE, per ECMA-130.

`xorrisofs` can only emit 2048-byte sectors: the DATA, without the header or the error-correction
codes a CD actually carries.  An emulator that mounts the image as a filesystem reads it, but it is
not a CD track: no burner and no real drive does anything with it.  The .bin/.cue pair below is the
complete track.

MODE1 sector (ECMA-130 SS14-15):
    sync 12 | MSF BCD + mode 4 | data 2048 | EDC 4 | zeroes 8 | P parity 172 | Q parity 104

TRAP: P covers bytes 12..2075 (header + data + EDC + zeroes), Q covers 12..2247 -- so Q reads the
bytes P has just written, and must be computed AFTER it.  Swapping them yields an image that still
mounts (the EDC is right) but whose error correction is wrong.

VALIDATED against a PRESSED DISC: take the 2048 data bytes of 404 sectors spread over
`refs/iso/Duke Nukem 3D (USA) (Track 01).bin`, rebuild the whole sector, and all 2352 bytes match
every time -- sync, header, EDC, P and Q.  Comparing against a pair we made ourselves would only
have proved we agree with ourselves.

CLONECD (automatic, as soon as there is one audio track): the same disc a second time, as the
`.ccd`/`.img`/`.sub` triple of tools/ccd.py, next to the .bin/.cue.  Both are delivered: an emulator
and the testers read the .cue, but an ODE mounts a .cue as ONE data track and its CD-DA never
reaches the console's TOC -- a Saturn plays our music only from the CloneCD form (2026-09-22).

AUDIO TRACKS (optional, after the .cue): each .wav becomes CD-DA track 02, 03... in the order given.
A track is its own file next to the .bin (`<name>-02.bin`), converted once and kept while it is newer
than its .wav: a rebuild rewrites the data track and the .cue, never the tens of MB of audio.  Laid
out as a pressed disc is (the Redump form of refs/iso/Powerslave (USA)): 2 s of silence at the head
of the file = INDEX 00, the music from INDEX 01, raw 16-bit little-endian stereo at 44.1 kHz, padded
to whole 2352-byte sectors.  No PREGAP directive: Ymir #146 maps it wrong (saturn-refs knowledge,
EMULATORS_AND_HW.md, "CD-DA / pregap du .cue").

usage: python tools/iso2bin.py input.iso output.bin [output.cue [audio.wav ...]]
"""
import ccd                              # tools/, next to this file: sys.path[0] when it is the script
import os
import sys
import wave

SYNC = b"\x00" + b"\xff" * 10 + b"\x00"

# EDC: CRC-32 with the reflected polynomial 0x8001801B (ECMA-130 annex A).
_edc_t = []
for i in range(256):
    e = i
    for _ in range(8):
        e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
    _edc_t.append(e)


def edc(buf):
    c = 0
    for b in buf:
        c = (c >> 8) ^ _edc_t[(c ^ b) & 0xFF]
    return c & 0xFFFFFFFF


# GF(2^8) field of the P/Q Reed-Solomon code, polynomial 0x11D (ECMA-130 S13).
_f = [((i << 1) ^ (0x11D if i & 0x80 else 0)) & 0xFF for i in range(256)]
_b = [0] * 256
for i in range(256):
    _b[i ^ _f[i]] = i


def _pq(src, major_count, minor_count, major_mult, minor_inc):
    """P parity (86, 24, 2, 86) or Q parity (52, 43, 86, 88): one sweep, four constants."""
    size = major_count * minor_count
    out = bytearray(major_count * 2)
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        a = b = 0
        for _ in range(minor_count):
            t = src[index]
            index += minor_inc
            if index >= size:
                index -= size
            a ^= t
            b ^= t
            a = _f[a]
        a = _b[_f[a] ^ b]
        out[major] = a
        out[major + major_count] = a ^ b
    return bytes(out)


def bcd(n):
    return ((n // 10) << 4) | (n % 10)


def sector(lba, data2048):
    t = lba + 150                       # LBA 0 sits at 00:02:00 (2 s of pre-gap)
    s = bytearray(2352)
    s[0:12] = SYNC
    s[12:16] = bytes((bcd(t // 4500), bcd((t // 75) % 60), bcd(t % 75), 1))
    s[16:2064] = data2048
    s[2064:2068] = edc(s[0:2064]).to_bytes(4, "little")
    s[2076:2248] = _pq(s[12:2076], 86, 24, 2, 86)     # P first...
    s[2248:2352] = _pq(s[12:2248], 52, 43, 86, 88)    # ...then Q, which re-reads P's bytes
    return bytes(s)


def convert(src, dst, postgap=0):
    """-> sector count (of the ISO, the post-gap left out).  Atomic write: a .bin truncated by a
    Ctrl-C looks like a valid disc and burns without complaint."""
    n = os.path.getsize(src)
    if n % 2048:
        raise SystemExit(f"{src}: {n} bytes, not a multiple of 2048 (this is not an ISO)")
    tmp = dst + ".tmp"
    with open(src, "rb") as f, open(tmp, "wb") as g:
        for lba in range(n // 2048):
            g.write(sector(lba, f.read(2048)))
        for lba in range(n // 2048, n // 2048 + postgap):
            g.write(sector(lba, bytes(2048)))
    os.replace(tmp, dst)
    return n // 2048


PREGAP = 150                            # sectors: 2 s of silence, INDEX 00 of an audio track
# A data track followed by audio ends on 2 s of empty MODE1 sectors, inside track 01: the
# pressed Powerslave (USA) track 01 does (sectors 82365-82514, rebuilt here byte for byte).  Our
# first disc with music, without them, was refused by the console ('CD non reconnu', 2026-09-22)
# and boots with them -- that they were the cause is not settled (a track file may have been missing).
POSTGAP = 150


def audio_track(wav, dst):
    """.wav -> raw CD-DA track file (pregap + music, whole sectors).  Skipped while dst is newer
    than wav.  -> sector count of the music (INDEX 01 on)."""
    with wave.open(wav, "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (2, 2, 44100):
            raise SystemExit(f"{wav}: CD-DA is 44.1 kHz 16-bit stereo, this is "
                             f"{w.getframerate()} Hz {8 * w.getsampwidth()}-bit x{w.getnchannels()}")
        n = w.getnframes() * 4
        music = -(-n // 2352)
        if (os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(wav)
                and os.path.getsize(dst) == (PREGAP + music) * 2352):
            return music
        tmp = dst + ".tmp"
        with open(tmp, "wb") as g:
            g.write(bytes(PREGAP * 2352))
            left = n
            while left:
                b = w.readframes(1 << 16)       # WAV data is already little-endian, as a .bin wants
                if not b:
                    raise SystemExit(f"{wav}: {left} bytes short of its header's length")
                g.write(b[:left])
                left -= min(len(b), left)
            g.write(bytes(music * 2352 - n))
        os.replace(tmp, dst)
        print(f"  AUDIO: {os.path.basename(dst)} <- {os.path.basename(wav)} "
              f"({music} sectors, {music / 75:.0f} s)")
        return music


def write_cue(cue, bin_path, audio=()):
    """Track 01 = the data; then one FILE per audio track, named relative to the .cue (Ymir only
    loads a FILE from the .cue's own folder)."""
    lines = ['FILE "%s" BINARY' % os.path.basename(bin_path),
             '  TRACK 01 MODE1/2352', '    INDEX 01 00:00:00']
    for t, path in enumerate(audio, 2):
        lines += ['FILE "%s" BINARY' % os.path.basename(path),
                  '  TRACK %02d AUDIO' % t, '    INDEX 00 00:00:00', '    INDEX 01 00:02:00']
    tmp = cue + ".tmp"
    with open(tmp, "wb") as c:
        c.write(("\r\n".join(lines) + "\r\n").encode())
    os.replace(tmp, cue)


def clonecd(base, data, audio):
    """The same disc as a CloneCD triple (tools/ccd.py), from the files the .cue already names.

    The 2 s before an audio track belong to the track BEFORE it in this form -- the data track
    already ends on them (POSTGAP), so each track file joins the image without its own PREGAP and
    track n starts where the previous one ended.  That is the layout the console read (2026-09-22:
    track 02 in the TOC, the music plays); a second gap would put INDEX 01 two seconds late."""
    tracks = [(0, 1)]
    lba = os.path.getsize(data) // 2352
    tmp = base + ".img.tmp"
    with open(tmp, "wb") as g:
        for src, skip in [(data, 0)] + [(a, PREGAP * 2352) for a in audio]:
            with open(src, "rb") as f:
                f.seek(skip)
                while True:
                    b = f.read(1 << 20)
                    if not b:
                        break
                    g.write(b)
            if skip:
                tracks.append((lba, 0))
                lba += os.path.getsize(src) // 2352 - PREGAP
    os.replace(tmp, base + ".img")
    ccd.write(base + ".ccd", ccd.ccd(tracks, lba).encode())
    ccd.write(base + ".sub", ccd.sub(tracks, lba))
    print(f"  CCD: {os.path.basename(base)}.ccd/.img/.sub ({len(tracks)} tracks, {lba} sectors)")


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if len(argv) < 2:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    src, dst = argv[0], argv[1]
    n = convert(src, dst, POSTGAP if argv[3:] else 0)
    tracks = []
    for t, wav in enumerate(argv[3:], 2):
        tracks.append("%s-%02d.bin" % (os.path.splitext(dst)[0], t))
        audio_track(wav, tracks[-1])
    if len(argv) > 2:
        write_cue(argv[2], dst, tracks)
    if tracks:
        clonecd(os.path.splitext(dst)[0], dst, tracks)
    print(f"  DISC: {os.path.basename(dst)} ({os.path.getsize(dst) // 2352} sectors, "
          f"{os.path.getsize(dst)} bytes, MODE1/2352)"
          + (f" + {os.path.basename(argv[2])}" if len(argv) > 2 else "")
          + (f" + {len(tracks)} audio track(s)" if tracks else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

/* VEW2NOTE.C - CF-VEW212: key ONE OPL4 wave voice, and hear it.
 *
 * The first brick of a native DOS wavetable renderer.  Everything larger
 * stands on this: if a single voice can be given a wave number, a pitch, a
 * level, a pan and a key-on, and it comes out of the jack, then the voice
 * layer is right and the rest is bookkeeping.
 *
 * It also settles a standing question in the same run.  The YMF278B has
 * THREE digital outputs (datasheet p6) and each voice picks one:
 *      DO0  FM only          (FM channels with CHC/CHD set)
 *      DO1  wave only        (wave voices with CH = 1)
 *      DO2  FM + wave mixed  (FM with CHA/CHB, wave voices with CH = 0)
 * We do not know which of those the CF-VEW212 actually wires to the codec's
 * line input.  /CH=1 moves this voice to DO1; if it goes silent, only DO2
 * is connected, and our renderer must keep every voice on CH=0.
 *
 * WAVE HEADERS LOAD THEMSELVES.  Per the datasheet, writing the wave table
 * number makes the chip fetch that wave's 12-byte header from the YRW801 -
 * start/loop/end and the default envelope - so the minimum to make a sound
 * is: number, pitch, level, pan, key on.  The envelope switches here are
 * overrides for when the ROM default is not what we want.
 *
 * PITCH.  Playback rate is 2^OCT * (1 + FNUM/1024) relative to the wave's
 * own recorded rate, and the YRW801 header carries NO root-key field - the
 * root is driver knowledge, not ROM knowledge.  So /NOTE is only meaningful
 * once we know a wave's root: it is computed against /ROOT, which defaults
 * to 60 and is a guess until measured.  /OCT and /FNUM set the registers
 * raw and are the honest way to explore a new wave.
 *
 * SAFETY: I/O-space device registers only.  No PCIC, no attribute space,
 * no COR, no codec.  Leaves NEW2 as it found it and always keys the voice
 * off before exiting, including on a bad wave number.
 *
 * Usage: VEW2NOTE [/V=n] [/WAVE=n] [/NOTE=n] [/ROOT=n] [/OCT=n] [/FNUM=n]
 *                 [/TL=n] [/PAN=n] [/CH=n] [/MS=n] [/MIX=n] [/BASE=388] [/?]
 *   /V=n      voice 0-23            (default 0)
 *   /WAVE=n   wave table number 0-511 (default 0)
 *   /NOTE=n   MIDI note 0-127       (default 60, needs /ROOT to mean much)
 *   /ROOT=n   the wave's root note  (default 60 - a guess, see above)
 *   /OCT=n    raw octave -8..7      (overrides /NOTE)
 *   /FNUM=n   raw F-number 0-1023   (overrides /NOTE)
 *   /TL=n     total level 0-127, 0 = loudest (default 0)
 *   /PAN=n    panpot 0-15           (default 0)
 *   /CH=n     0 = DO2 mixed (default), 1 = DO1 wave-only
 *   /MS=n     hold time in ms       (default 1500)
 *   /MIX=n    also set F8/F9 mix fields to n (0-7, 3 dB steps, 0 = 0 dB)
 *
 * Build: C:\WATCOM\BLD.BAT VEW2NOTE
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <conio.h>
#include <dos.h>

static unsigned BASE = 0x388;
#define FMA1  (BASE + 2)
#define FMD1  (BASE + 3)
#define WADDR (BASE + 4)
#define WDATA (BASE + 5)

static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)

static unsigned char wv_get(unsigned char reg)
{
    outp(WADDR, reg); MS(1);
    return (unsigned char)inp(WDATA);
}
static void wv_put(unsigned char reg, unsigned char val)
{
    outp(WADDR, reg); MS(1);
    outp(WDATA, val); MS(1);
}
static void new2(int on)
{
    outp(FMA1, 0x05); MS(1);
    outp(FMD1, on ? 0x03 : 0x00); MS(1);
}

/* semitones -> (octave, fnum) for rate 2^oct * (1 + fnum/1024).
 * Done in integer steps: 1024 * 2^(s/12) via a 12-entry table, then the
 * octave is however many doublings came out of it. */
static const unsigned semi[12] = {          /* 1024 * 2^(n/12), n = 0..11  */
    1024, 1085, 1149, 1218, 1290, 1367,
    1448, 1534, 1625, 1722, 1825, 1933
};
static void pitch_of(int semitones, int *oct, unsigned *fnum)
{
    int o = 0, s = semitones;
    while (s < 0)  { s += 12; o--; }
    while (s >= 12){ s -= 12; o++; }
    *fnum = semi[s] - 1024;                 /* 0..909, inside the 10 bits  */
    *oct  = o;
}

static void usage(void)
{
    printf("VEW2NOTE - key one OPL4 wave voice\r\n");
    printf("  /V=n voice 0-23   /WAVE=n 0-511   /NOTE=n   /ROOT=n\r\n");
    printf("  /OCT=n /FNUM=n raw pitch (override /NOTE)\r\n");
    printf("  /TL=n 0-127 (0=loudest)  /PAN=n 0-15  /MS=n hold\r\n");
    printf("  /CH=0 DO2 mixed (default), /CH=1 DO1 wave-only\r\n");
    printf("  /MIX=n set F8/F9 fields (0-7, 3 dB steps)\r\n");
    printf("Needs the card on COR index 23h with the OPL4 at 388-38D.\r\n");
}

int main(int argc, char **argv)
{
    int v = 0, note = 60, root = 60, tl = 0, pan = 0, ch = 0, mix = -1;
    int oct = 99, opened = 0, semis;
    long wave = 0, fnum = -1, ms = 1500;
    unsigned f;
    unsigned char id;

    {
        int i;
        for (i = 1; i < argc; i++) {
            char *p = argv[i];
            if (p[0] != '/' && p[0] != '-') continue;
            if (p[1] == '?')                       { usage(); return 0; }
            if (!strnicmp(p + 1, "WAVE", 4)) wave = strtol(p + 5 + (p[5] == '='), 0, 0);
            else if (!strnicmp(p + 1, "NOTE", 4)) note = (int)strtol(p + 5 + (p[5] == '='), 0, 0);
            else if (!strnicmp(p + 1, "ROOT", 4)) root = (int)strtol(p + 5 + (p[5] == '='), 0, 0);
            else if (!strnicmp(p + 1, "FNUM", 4)) fnum = strtol(p + 5 + (p[5] == '='), 0, 0);
            else if (!strnicmp(p + 1, "BASE", 4)) BASE = (unsigned)strtol(p + 5 + (p[5] == '='), 0, 16);
            else if (!strnicmp(p + 1, "OCT", 3))  oct = (int)strtol(p + 4 + (p[4] == '='), 0, 0);
            else if (!strnicmp(p + 1, "PAN", 3))  pan = (int)strtol(p + 4 + (p[4] == '='), 0, 0);
            else if (!strnicmp(p + 1, "MIX", 3))  mix = (int)strtol(p + 4 + (p[4] == '='), 0, 0);
            else if (!strnicmp(p + 1, "TL", 2))   tl  = (int)strtol(p + 3 + (p[3] == '='), 0, 0);
            else if (!strnicmp(p + 1, "MS", 2))   ms  = strtol(p + 3 + (p[3] == '='), 0, 0);
            else if (!strnicmp(p + 1, "CH", 2))   ch  = (int)strtol(p + 3 + (p[3] == '='), 0, 0);
            else if (p[1] == 'V' || p[1] == 'v')  v   = (int)strtol(p + 2 + (p[2] == '='), 0, 0);
        }
    }
    if (v < 0 || v > 23)        { printf("voice must be 0-23\r\n");   return 1; }
    if (wave < 0 || wave > 511) { printf("wave must be 0-511\r\n");   return 1; }
    if (tl  < 0)  tl  = 0;   if (tl  > 127) tl  = 127;
    if (pan < 0)  pan = 0;   if (pan > 15)  pan = 15;
    if (ms  < 1)  ms  = 1;   if (ms  > 20000L) ms = 20000L;
    ch = ch ? 1 : 0;

    printf("VEW2NOTE - base %03X, voice %d, wave %ld\r\n", BASE, v, wave);

    /* Always open the gate - a closed bus floats, and it has floated to
     * 20h, which is indistinguishable from the real Device ID. */
    new2(1); opened = 1;
    id = wv_get(0x02);
    if ((id & 0xF0) != 0x20) {
        printf("DevID %02X: no YMF278B answering - is the card on index 23h\r\n", id);
        printf("with the OPL4 window at 388-38D?  Nothing played.\r\n");
        if (opened) new2(0);
        return 1;
    }
    wv_put(0x02, 0x00);                     /* sound generation, not memory */

    /* --- pitch ------------------------------------------------------- */
    if (fnum >= 0 || oct != 99) {
        if (fnum < 0)   fnum = 0;
        if (oct == 99)  oct  = 0;
        f = (unsigned)(fnum & 0x3FF);
        printf("pitch: raw OCT %d FNUM %u\r\n", oct, f);
    } else {
        semis = note - root;
        pitch_of(semis, &oct, &f);
        printf("pitch: note %d vs root %d = %+d semitones -> OCT %d FNUM %u\r\n",
               note, root, semis, oct, f);
    }
    if (oct < -8) oct = -8;
    if (oct >  7) oct =  7;

    /* --- the voice.  Writing 08H+v makes the chip load the wave's own
     * 12-byte header from the YRW801, so the envelope comes for free. --- */
    /* 20h carries wave number bit 8 and must be latched BEFORE the 08h
     * write - that write triggers the header fetch, and a late bit 8
     * makes waves 256+ load the header and sample of wave-256. */
    wv_put((unsigned char)(0x20 + v),
           (unsigned char)(((f & 0x7F) << 1) | ((wave >> 8) & 1)));
    wv_put((unsigned char)(0x08 + v), (unsigned char)(wave & 0xFF));
    wv_put((unsigned char)(0x38 + v),
           (unsigned char)(((oct & 0x0F) << 4) | ((f >> 7) & 0x07)));
    wv_put((unsigned char)(0x50 + v), (unsigned char)((tl << 1) | 1));  /* LD=1 */

    if (mix >= 0) {
        unsigned char m = (unsigned char)(((mix & 7) << 3) | (mix & 7));
        wv_put(0xF8, m);
        wv_put(0xF9, m);
        printf("mix: F8 and F9 <- %02X (both fields %d = -%d dB)\r\n", m, mix & 7, (mix & 7) * 3);
    }

    printf("key on: voice %d, TL %d, pan %d, CH %d (%s) for %ld ms\r\n",
           v, tl, pan, ch, ch ? "DO1 wave-only" : "DO2 mixed", ms);
    wv_put((unsigned char)(0x68 + v),
           (unsigned char)(0x80 | (ch << 4) | (pan & 0x0F)));   /* KEY ON */
    while (ms > 0) { MS(100); ms -= 100; }
    wv_put((unsigned char)(0x68 + v), (unsigned char)((ch << 4) | (pan & 0x0F)));

    printf("key off.%s\r\n", ch ? "  If that was SILENT, DO1 is not wired -"
                                  " re-run without /CH=1." : "");
    if (opened) new2(0);
    return 0;
}

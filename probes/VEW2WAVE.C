/* VEW2WAVE.C - CF-VEW212: dump the OPL4 (YMF278B) WAVE register file.
 *
 * VEW2DUMP only does raw port reads of 388-38F, which cannot see the wave
 * section at all: those registers live behind the index/data pair at
 * base+4 / base+5 (38C/38D).  This walks that file so we can DIFF the
 * chip's state across the vendor driver's "OPL4 Synthesizer Control"
 * settings -- PCM vs FM vs Both.  That switch happens on the fly, so its
 * whole effect is a handful of register writes, and the mix-control pair
 * F8 (FM level) / F9 (PCM level) is the prime suspect.
 *
 * Intended use: set a synthesis mode in Windows, EXIT Windows, run this,
 * repeat for the other modes, diff the dumps.  Redirect to a file:
 *     VEW2WAVE > WAVEPCM.TXT
 *
 * THE NEW2 GATE.  The wave register file only answers when the OPL3-side
 * NEW2 bit is set (FM bank 1 register 05h, via 38A/38B).  With it clear
 * every wave register reads back the same constant -- a uniform dump is
 * this gate, not a dead chip.  So:
 *   - By DEFAULT this tool touches nothing and reports whether the wave
 *     section is already accessible.  If it is, whatever left NEW2 set
 *     did so, and that is itself a finding worth having.
 *   - /NEW opens the gate (38A<-05, 38B<-03), dumps, then restores compat
 *     mode (NEW=0) the way VEW2TRY does -- OPL2-era games go silent if
 *     NEW is left on.  Note the restore is to a KNOWN-SAFE state, not
 *     necessarily the state found, because these bits are write-only.
 *
 * SAFETY: no card configuration is touched -- no PCIC, no attribute
 * space, no COR, no codec.  Without /NEW the only writes are to the wave
 * ADDRESS latch at base+4, unavoidable when reading an indexed file and
 * holding no state of its own.
 *
 * Register 0x06 is SKIPPED by default: it is the sample-memory DATA port,
 * and reading it starts a YRW801 read cycle and AUTO-INCREMENTS the memory
 * address pointer.  Harmless against a mask ROM, but it perturbs chip
 * state, so it is opt-in via /MEM.
 *   Per the YMF278B datasheet (register table, wave synthesis): 03H/04H/05H
 *   are memory address A21-A16 / A15-A8 / A7-A0 and 06H is the data port.
 *   An intermediate build of this tool skipped 07H instead, on a guess made
 *   before the datasheet was to hand.  07H is reserved.
 *
 * Usage: VEW2WAVE [/NEW] [/BASE=388] [/MEM] [/RAW] [/?]
 *
 * Build: C:\WATCOM\BLD.BAT VEW2WAVE
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

static unsigned BASE  = 0x388;
#define FMA0  (BASE + 0)          /* FM address bank 0 / status            */
#define FMA1  (BASE + 2)          /* FM address bank 1                     */
#define FMD1  (BASE + 3)          /* FM data   bank 1                      */
#define WADDR (BASE + 4)          /* wave register address latch           */
#define WDATA (BASE + 5)          /* wave register data                    */

static void iod(unsigned long n){ while (n--) (void)inp(0x80); }   /* ~1us */
#define MS(x) iod((unsigned long)(x) * 1000UL)

/* Address-then-data with the same 1 ms settle VEW2TRY's proven DevID read
 * uses.  The YMF278B only needs ~2.6 us, but proven beats fast here. */
static unsigned char wv_get(unsigned char reg)
{
    outp(WADDR, reg); MS(1);
    return (unsigned char)inp(WDATA);
}

static void new2(int on)
{
    outp(FMA1, 0x05); MS(1);
    outp(FMD1, on ? 0x03 : 0x00); MS(1);      /* bit0 NEW, bit1 NEW2       */
}

static unsigned char regs[256];
static int  have[256];

static void usage(void)
{
    printf("VEW2WAVE - OPL4 (YMF278B) wave register dump\r\n");
    printf("  /NEW       open the NEW2 gate first (writes 38A/38B), then\r\n");
    printf("             restore compat mode NEW=0 afterwards\r\n");
    printf("  /BASE=hex  OPL4 base (default 388, wave pair at base+4/+5)\r\n");
    printf("  /MEM       also read reg 06 (sample-memory data port -\r\n");
    printf("             reading it auto-increments the address pointer)\r\n");
    printf("  /RAW       hex dump only\r\n");
    printf("Redirect to a file to diff runs:  VEW2WAVE > WAVEPCM.TXT\r\n");
}

int main(int argc, char **argv)
{
    int i, n, do_mem = 0, raw = 0, opennew = 0, uniform;
    unsigned char id, f8, f9, r80, r68, r38, first;
    unsigned tone;

    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '/' && a[0] != '-') continue;
        if (a[1] == '?')                 { usage(); return 0; }
        if (!strnicmp(a + 1, "NEW",  3)) opennew = 1;
        if (!strnicmp(a + 1, "MEM",  3)) do_mem  = 1;
        if (!strnicmp(a + 1, "RAW",  3)) raw     = 1;
        if (!strnicmp(a + 1, "BASE", 4)) sscanf(a + 5 + (a[5] == '='), "%x", &BASE);
    }

    printf("VEW2WAVE - CF-VEW212 OPL4 wave register dump\r\n");
    printf("base %03X: FM %03X-%03X, wave addr %03X data %03X\r\n",
           BASE, BASE, BASE + 3, WADDR, WDATA);
    printf("OPL status (%03X) = %02X%s\r\n", FMA0, (unsigned char)inp(FMA0),
           (unsigned char)inp(FMA0) == 0xFF ? "   <-- FF: FM not decoding either!" : "");

    /* --- is the gate already open? ------------------------------------- */
    id = wv_get(0x02);
    printf("DevID (wave reg 02) = %02X -> %s\r\n", id,
           (id & 0xF0) == 0x20 ? "YMF278B, wave section ACCESSIBLE (NEW2 already set)"
                               : "wave section NOT answering");

    if ((id & 0xF0) != 0x20) {
        if (!opennew) {
            printf("\r\nThe NEW2 gate is closed, so the wave file cannot be read.\r\n");
            printf("Re-run with /NEW to open it (writes %03X/%03X, restores NEW=0).\r\n",
                   FMA1, FMD1);
            printf("NOTE for the mode diff: NEW2 being CLOSED here is itself data -\r\n");
            printf("it means nothing left the wave section enabled.\r\n");
            return 1;
        }
        printf("Opening the NEW2 gate (/NEW)...\r\n");
        new2(1);
        id = wv_get(0x02);
        printf("DevID after NEW2 = %02X -> %s\r\n", id,
               (id & 0xF0) == 0x20 ? "YMF278B WAVE ALIVE" : "still not answering");
    } else if (opennew) {
        printf("(/NEW given but the gate was already open - not touching it.)\r\n");
        opennew = 0;
    }

    /* --- the file ------------------------------------------------------ */
    for (n = 0; n < 256; n++) {
        if (n == 0x06 && !do_mem) { have[n] = 0; continue; }
        regs[n] = wv_get((unsigned char)n);
        have[n] = 1;
    }

    if (opennew) { new2(0); printf("(NEW2 gate closed again, compat mode NEW=0 restored.)\r\n"); }

    /* A register file that reads one constant everywhere is not a register
     * file -- say so rather than letting it be mistaken for real data. */
    first = regs[0]; uniform = 1;
    for (n = 1; n < 256; n++) if (have[n] && regs[n] != first) { uniform = 0; break; }

    printf("\r\n");
    for (n = 0; n < 256; n += 16) {
        printf("wave %02X:", n);
        for (i = 0; i < 16; i++) {
            if (have[n + i]) printf(" %02X", regs[n + i]);
            else             printf(" --");
        }
        printf("\r\n");
    }
    if (!do_mem) printf("(reg 06 skipped - sample-memory data port; /MEM to include)\r\n");

    if (uniform) {
        printf("\r\n*** EVERY REGISTER READ %02X - THIS IS NOT A REGISTER FILE. ***\r\n", first);
        printf("The wave section is not responding; treat nothing below as real.\r\n");
        return 1;
    }
    if (raw) { printf("\r\ndone.\r\n"); return 0; }

    /* --- the registers this tool exists for ---------------------------- *
     * F8 / F9 are the OPL4's output mix controls: two 3-bit fields each,
     * one per output pin, 0 = loudest.  The FM path powers up ATTENUATED
     * (F8 = 0x1B, i.e. 3 and 3) while the PCM path powers up at 0.  The
     * Windows driver's PCM/FM/Both selector almost certainly lands here. */
    f8 = regs[0xF8];
    f9 = regs[0xF9];
    printf("\r\nMIX CONTROL into DO2 (3 dB per step, 0 = 0 dB):\r\n");
    printf("  F8 FM  = %02X   FM-L %d (-%d dB), FM-R %d (-%d dB)%s\r\n",
           f8, f8 & 7, (f8 & 7) * 3, (f8 >> 3) & 7, ((f8 >> 3) & 7) * 3,
           (f8 == 0x1B) ? "   [power-on default]" : "");
    printf("  F9 PCM = %02X   PCM-L %d (-%d dB), PCM-R %d (-%d dB)%s\r\n",
           f9, f9 & 7, (f9 & 7) * 3, (f9 >> 3) & 7, ((f9 >> 3) & 7) * 3,
           (f9 == 0x00) ? "   [power-on default]" : "");

    /* --- the 24 PCM voices --------------------------------------------- *
     * Per the YMF278B register table (wave synthesis), the banks are:
     *   08H+n  wave table number bits 7:0
     *   20H+n  F-NUM f6..f0 in 7:1, wave table number bit 8 in bit 0
     *   50H+n  total level in 7:1, level-direct in bit 0
     *   68H+n  KEY ON 7 | DAMP 6 | LFO RES 5 | CH 4 | panpot 3:0
     * An earlier build of this tool read these a whole bank out (tone from
     * 20H, TL from 68H, key/pan from 80H) and every per-voice number it
     * printed was wrong.  CH is the interesting bit here: it picks the
     * OUTPUT PIN - 0 = DO2 (mixed with FM), 1 = DO1 (wave only).           */
    printf("\r\nPCM VOICES (tone = wave table number, CH = output pin):\r\n");
    printf("  v  tone  TL  key  CH pan     v  tone  TL  key  CH pan\r\n");
    for (n = 0; n < 12; n++) {
        for (i = 0; i < 2; i++) {
            int c = n + i * 12;
            r38  = regs[0x20 + c];
            r68  = regs[0x50 + c];
            r80  = regs[0x68 + c];
            tone = (unsigned)regs[0x08 + c] | ((unsigned)(r38 & 1) << 8);
            printf(" %2d  %4u %3u  %3s  %s %2d  ", c, tone, r68 >> 1,
                   (r80 & 0x80) ? "ON" : "off",
                   (r80 & 0x10) ? "DO1" : "DO2", r80 & 0x0F);
        }
        printf("\r\n");
    }

    printf("\r\ndone.\r\n");
    return 0;
}

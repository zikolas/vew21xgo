/* VEWPIO.C - Panasonic CF-VEW211 / CS4231A: does DMA-less PIO playback work?
 * Port of the SCP-55's CSPIO2 probe (where the verdict was NEGATIVE - PRDY
 * never asserted behind Roland's ASIC). Same codec here, different glue:
 * the VEW211 is a purpose-built WSS card, so PIO is how its period drivers
 * must have fed it. This settles whether the FIFO handshake reaches the bus.
 *
 * Per the CS4231A datasheet, PIO playback is: poll the Status register
 * (base+2) for PRDY (bit1 = ready for next sample) - and that same Status
 * read commits the previously-written sample to the FIFO - then write the
 * next sample to the Playback Data Register (base+3).
 *
 * HARD ~1.5s wall-clock bound checked EVERY iteration so it can never hang.
 * Diagnostic ladder:
 *   1. setup readback   - did PPIO+PEN latch in I9?
 *   2. prime+poll       - is the DAC clock draining the FIFO at all?
 *   3. fast-feed + PUR  - are samples even LANDING in the FIFO?
 * Plays a ~333Hz square (8-bit unsigned mono, 8kHz) - audible if PIO works.
 *
 * Codec at 0x534: IAR/IDR/status/PDR = 0x534/0x535/0x536/0x537.
 * NOTE: power-cycles the socket for a clean codec reset - re-run VEW21XGO
 * (with your usual switches) after the probe to restore the full config.
 * Build: C:\WATCOM\BLD VEWPIO
 */
#include <stdio.h>
#include <conio.h>
#include <i86.h>

#define PCIC 0x3E0
static unsigned pidx = PCIC, soff = 0;
static unsigned char rd(unsigned r){ outp(pidx, soff + r); return (unsigned char)inp(pidx + 1); }
static void          wr(unsigned r, unsigned v){ outp(pidx, soff + r); outp(pidx + 1, v); }
static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)

static unsigned g_manf=0, g_card=0;
static int g_cisdead = 0;
static void read_cis(unsigned seg)
{
    unsigned char __far *p = (unsigned char __far *)MK_FP(seg, 0);
    unsigned off = 0; int g, same = 1;
    unsigned char fill = p[0];
    g_manf = g_card = 0; g_cisdead = 0;
    for (g = 1; g < 16; g++) if (p[g * 2] != fill) { same = 0; break; }
    if (same) { g_cisdead = 1; return; }        /* dead fill wall = our broken unit */
    for (g = 0; g < 64; g++) {
        unsigned char code = p[off], link;
        if (code == 0xFF) break;
        if (code == 0x00) { off += 2; continue; }
        link = p[off + 2];
        if (code == 0x20) { g_manf=(unsigned)p[off+4]|((unsigned)p[off+6]<<8);
                            g_card=(unsigned)p[off+8]|((unsigned)p[off+10]<<8); }
        if (link == 0xFF) break;
        off += ((unsigned)link + 2) * 2;
        if (off >= 0x3000) break;
    }
}

#define IAR 0x534
#define IDR 0x535
#define SR  0x536
#define PDR 0x537
/* wait until the codec stops returning 0x80 on IAR (busy/resync/INIT) */
static void ci_wait(void){ unsigned long i; for(i=0;i<400000UL;i++) if(!(inp(IAR)&0x80)) return; }
static void ci_put(unsigned char idx, unsigned char v){ ci_wait(); outp(IAR, idx); iod(200); outp(IDR, v); iod(200); }
static unsigned char ci_get(unsigned char idx){ ci_wait(); outp(IAR, idx); iod(200); return (unsigned char)inp(IDR); }

int main(void)
{
    unsigned memseg = 0xD000;
    unsigned char __far *cor;
    unsigned long __far *bios = (unsigned long __far *)MK_FP(0x40, 0x6C);
    unsigned start, stop, woff;
    unsigned long t0, now, samples = 0, paced = 0, under = 0, t;
    unsigned char sr, srseen = 0, lvl = 0x40;
    int half = 12;

    pidx = PCIC; soff = 0;
    if ((rd(0x00)&0xC0)!=0x80) { printf("no 82365\n"); return 1; }
    if ((rd(0x01)&0x0C)!=0x0C) { printf("no card\n"); return 1; }
    wr(0x02,0x00); MS(120);            /* power-cycle -> clean CS4231 power-on reset */
    wr(0x02,0x95); MS(30);
    if (!(rd(0x01)&0x40)) { printf("no power\n"); wr(0x02,0); return 1; }
    wr(0x03,0x40); MS(10);
    for (t = 0; t < 600000UL; t++) if (rd(0x01) & 0x20) break;   /* READY before any card read */
    start=memseg>>8; stop=(memseg>>8)+3; woff=((unsigned)(0-(memseg>>8))&0x3FFF)|0x4000;
    wr(0x10,start&0xFF); wr(0x11,(start>>8)&0x3F);
    wr(0x12,stop&0xFF);  wr(0x13,(stop>>8)&0x3F);
    wr(0x14,woff&0xFF);  wr(0x15,(woff>>8)&0xFF);
    wr(0x06,0x01); MS(5);
    read_cis(memseg);
    if (!g_cisdead && (g_manf!=0x0032 || g_card!=0x0001)) {
        printf("not a CF-VEW211 (%04X/%04X, CIS alive)\n",g_manf,g_card);
        wr(0x06,0);wr(0x03,0);wr(0x02,0); return 1;
    }
    printf("CF-VEW211 socket 0 (%s)\n", g_cisdead ? "dead CIS, as expected" : "intact CIS");
    cor = (unsigned char __far *)MK_FP(memseg, 0x0200); *cor = 0x20;  /* idx 0x20 = 0x530 */
    MS(5);
    wr(0x08,0x30); wr(0x09,0x05);                   /* I/O win0 = 0x530..0x539 */
    wr(0x0A,0x39); wr(0x0B,0x05);
    wr(0x07,0x00); wr(0x03,0x60); wr(0x06,0x41); MS(10);

    /* Enter MCE; set format (I8) and interface config (I9) together.
     * I9 = PPIO(0x40) | PEN(0x01) = 0x41, no calibration.  PPIO is THE bit that
     * puts playback in PIO mode - without it PEN waits for a DMA handshake
     * (PDRQ/PDAK) that never comes.  I9's upper bits are writable ONLY under MCE. */
    ci_wait();
    outp(IAR, 0x40); iod(200);                              /* MCE on (index 0) */
    outp(IAR, 0x48); iod(200); outp(IDR, 0x00); iod(200);   /* I8 = XTAL1 8kHz 8-bit mono */
    outp(IAR, 0x49); iod(200); outp(IDR, 0x40); iod(200);   /* I9 = PPIO (NO PEN yet) */
    outp(IAR, 0x00); iod(200);                              /* MCE off */
    ci_wait();                                              /* clock resync / settle */
    ci_put(0x06, 0x00); ci_put(0x07, 0x00);                 /* un-mute L/R DAC */
    /* load playback count: Lower Base first, then UPPER Base (writing Upper
     * loads the Current Count register). Then enable PEN on-the-fly. */
    ci_put(0x0F, 0xFE); ci_put(0x0E, 0xFF);                 /* count base = 0xFFFE */
    ci_put(0x09, 0x41);                                     /* PEN=1 (PPIO retained) */
    MS(50);
    /* confirm PPIO+PEN took (expect I9=41) */
    printf("setup: IAR=%02X  I9=%02X  I11=%02X\n",
           (unsigned char)inp(IAR), ci_get(0x09), ci_get(0x0B));

    /* DIAGNOSTIC: prime the FIFO with 20 samples, then poll status ~8000x with
     * NO writes. If the DAC clock is draining the FIFO, PRDY (ready for data)
     * will assert once the FIFO empties. This isolates "is the DAC consuming?"
     * from my write cadence. */
    { unsigned long pr = 0, se = 0, in = 0; int k;
      for (k = 0; k < 20; k++) { outp(PDR, 0xFF); (void)inp(SR); }
      for (k = 0; k < 8000; k++) { outp(SR, 0); sr = (unsigned char)inp(SR);   /* clear INT, then read */
                                   if (sr & 0x02) pr++; if (sr & 0x10) se++; if (sr & 0x01) in++; }
      printf("prime+poll: PRDY-hi=%lu SER-hi=%lu INT-hi=%lu  I11=%02X\n",
             pr, se, in, ci_get(0x0B)); }

    /* TRANSFER-vs-ROUTING test: feed the FIFO as FAST as possible (no PRDY wait,
     * no delay) - way faster than the 8kHz drain - then check the underrun flag.
     * PUR clear => samples ARE reaching the FIFO (transfer works, silence=routing).
     * PUR set   => samples are NOT landing in the FIFO (transfer broken). */
    printf("CS4231A PIO fast-feed @ %03X: ~1.5s...\n", IAR);
    t0 = *bios;
    for (;;) {
        now = *bios;
        if ((now - t0) >= 28UL) break;         /* HARD ~1.5s bound */
        outp(PDR, lvl);                        /* write sample */
        sr = (unsigned char)inp(SR);           /* read status = commit to FIFO */
        srseen |= sr;
        if (sr & 0x02) paced++;
        samples++;
        if (--half <= 0) { half = 12; lvl ^= 0x80; }   /* 0x40 <-> 0xC0 */
    }
    under = ci_get(0x0B);                       /* I11: bit6 PUR = playback underrun */

    printf("samples=%lu  PRDY-seen=%lu  srOR=%02X  I11end=%02X\n",
           samples, paced, srseen, (unsigned char)under);
    if (under & 0x40)
        printf("VERDICT: PUR still set - fast feed did NOT fill the FIFO; PIO transfer not landing.\n");
    else
        printf("VERDICT: PUR CLEAR - samples reached the FIFO; transfer WORKS (silence => output routing).\n");

    ci_put(0x09, 0x00);                        /* stop */
    ci_put(0x06, 0x80); ci_put(0x07, 0x80);    /* mute */
    printf("done - re-run VEW21XGO to restore the normal config.\n");
    return 0;
}

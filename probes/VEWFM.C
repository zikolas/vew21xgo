/* VEWFM.C - CF-VEW211: FM-volume hunt, round 3 - with everything we know.
 *
 * Round 1+2 (VEWVND) swept the vendor registers and write-only ports one at
 * a time and heard nothing. Since then we learned: (1) attr 0x204 bit0 is
 * the EEPROM COMMIT STROBE - NEVER swept here; (2) this card's CS4231A-KQ
 * silently drops fast register writes, so every earlier "no effect" is
 * suspect - all writes here are millisecond-paced and (where readable)
 * verified; (3) a routing switch is only audible if the codec's inputs are
 * open to receive what it routes - so this probe first un-mutes EVERY
 * codec input (DAC/Aux1/Aux2/LINE/MONO, MODE2), then holds a sustained FM
 * note and walks:
 *
 *   phase A: for each 0x208 value 0..15 - slow audible ramp of 0x206
 *            (0..127 on bits 7..1), ~2.5s per ramp, phase shown on screen
 *   phase B: each write-only port 0x530..0x533 ramped 0..255
 *
 * The listener notes which on-screen phase (if any) changes the note's
 * volume, timbre or routing. Everything is restored at the end.
 *
 * Requires the card enabled (VEW21XGO first). Build: C:\WATCOM\BLD VEWFM
 */
#include <stdio.h>
#include <conio.h>
#include <i86.h>

#define PCIC   0x3E0
#define MEMSEG 0xD000
static unsigned pidx = PCIC, soff = 0;
static unsigned char rd(unsigned r){ outp(pidx, soff + r); return (unsigned char)inp(pidx + 1); }
static void          wr(unsigned r, unsigned v){ outp(pidx, soff + r); outp(pidx + 1, v); }
static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)
static unsigned char __far *AW;

#define IAR 0x534
#define IDR 0x535
static void ci_wait(void){ unsigned long i; for(i=0;i<400000UL;i++) if(!(inp(IAR)&0x80)) return; }
/* paced + verified codec write (the round-3 discipline) */
static void ci_put(unsigned char idx, unsigned char v)
{
    int t;
    for (t = 0; t < 5; t++) {
        ci_wait(); outp(IAR, idx); MS(1); outp(IDR, v); MS(1);
        ci_wait(); outp(IAR, idx); MS(1);
        if ((unsigned char)inp(IDR) == v) return;
    }
}
static unsigned char ci_get(unsigned char idx){ ci_wait(); outp(IAR, idx); MS(1); return (unsigned char)inp(IDR); }

/* paced + verified attribute-register write */
static void aw_put(unsigned off, unsigned char v)
{
    int t;
    for (t = 0; t < 5; t++) { AW[off] = v; MS(1); if (AW[off] == v) return; }
}

#define FMA 0x388
static void opl(unsigned char r, unsigned char v){ outp(FMA,r); iod(60); outp(FMA+1,v); iod(180); }
static void note(int on)
{
    static unsigned char rv[] = { 0x20,0x21,0x40,0x18,0x60,0xF0,0x80,0x77,
                                  0x23,0x21,0x43,0x00,0x63,0xF0,0x83,0x77,
                                  0xA0,0x98,0xC0,0x30 };
    int i;
    if (!on) { opl(0xB0, 0x11); return; }
    for (i = 0; i < (int)sizeof(rv); i += 2) opl(rv[i], rv[i+1]);
    opl(0xB0, 0x31);
}

int main(void)
{
    int r208, v, p;
    unsigned port;

    pidx = PCIC; soff = 0;
    AW = (unsigned char __far *)MK_FP(MEMSEG, 0);
    if ((rd(0x00) & 0xC0) != 0x80) { printf("no 82365\n"); return 1; }
    if ((unsigned char)inp(IAR) == 0xFF) { printf("no codec - run VEW21XGO first\n"); return 1; }

    /* attribute window for the vendor regs (0x204 NEVER touched: strobe!) */
    {
        unsigned woff = ((unsigned)(0 - (MEMSEG >> 8)) & 0x3FFF) | 0x4000;
        wr(0x10, (MEMSEG >> 8) & 0xFF); wr(0x11, 0x00);
        wr(0x12, ((MEMSEG >> 8) + 3) & 0xFF); wr(0x13, 0x00);
        wr(0x14, woff & 0xFF); wr(0x15, (woff >> 8) & 0xFF);
        wr(0x06, rd(0x06) | 0x01); iod(500);
    }

    printf("VEWFM - FM volume hunt round 3 (paced+verified writes, codec wide open)\n");
    printf("Listen to the held FM note; note the phase shown when ANYTHING changes.\n\n");

    /* open every codec input so a routing switch becomes audible */
    ci_put(0x0C, 0x40);                          /* MODE2 */
    ci_put(0x06, 0x18); ci_put(0x07, 0x18);      /* DAC -36dB (PCM reference level) */
    ci_put(0x02, 0x08); ci_put(0x03, 0x08);      /* Aux1 0dB un-muted */
    ci_put(0x04, 0x08); ci_put(0x05, 0x08);      /* Aux2 0dB un-muted */
    ci_put(0x12, 0x08); ci_put(0x13, 0x08);      /* LINE 0dB un-muted (MODE2) */
    ci_put(0x1A, 0x00);                          /* MONO in un-muted, 0dB, no bypass */
    printf("codec inputs opened (Aux1/Aux2/LINE/MONO live - some hiss is expected)\n");

    note(1);
    printf("FM note ON.\n\n");

    /* phase A: 0x208 x 0x206 matrix */
    for (r208 = 0; r208 < 16; r208++) {
        aw_put(0x208, (unsigned char)r208);
        printf("phase A: [208]=%X, ramping [206] 0->127...\n", r208);
        for (v = 0; v <= 127; v += 4) { aw_put(0x206, (unsigned char)(v << 1)); MS(22); }
        aw_put(0x206, 0x00);
    }
    aw_put(0x208, 0x00);

    /* phase B: write-only ports (paced; unverifiable by nature) */
    for (p = 0; p < 4; p++) {
        port = 0x530 + p;
        printf("phase B: ramping port %03X 0->255...\n", port);
        for (v = 0; v <= 255; v += 8) { outp(port, v); MS(22); }
        outp(port, 0x00); MS(2);
    }

    note(0);
    /* restore the enabler's default mixer policy */
    ci_put(0x02, 0x88); ci_put(0x03, 0x88);
    ci_put(0x04, 0x88); ci_put(0x05, 0x88);
    ci_put(0x12, 0x88); ci_put(0x13, 0x88);
    ci_put(0x1A, 0xC0);
    wr(0x06, rd(0x06) & ~0x01);
    printf("\ndone - all restored (vendor regs 0, ports 0, codec inputs muted).\n");
    printf("Report which phase (if any) changed the note.\n");
    return 0;
}

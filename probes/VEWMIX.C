/* VEWMIX.C - CF-VEW211 / CS4231A interactive DOS mixer (diagnostic TUI).
 *
 * Run VEW21XGO first (card enabled, codec at 0x534). VEWMIX switches the
 * codec to MODE2 so the LINE (I18/I19) and MONO (I26) controls exist, then
 * lets you drive every analog path into the output mix by hand:
 *
 *   PCM    - DAC attenuator            I6/I7   0..-94.5dB + mute
 *   AUX1   - Aux input 1               I2/I3   +12..-34.5dB + mute
 *   AUX2   - Aux input 2               I4/I5   +12..-34.5dB + mute
 *   LINE   - Line input                I18/I19 +12..-34.5dB + mute
 *   MONO   - Mono input attenuator     I26 b0-3 (~3dB/step) + mute (MIM)
 *   M-BYP  - Mono bypass toggle        I26 b5: mono in -> out, SKIPS mixer
 *   M-OUT  - Mono output mute toggle   I26 b6 (MOM)
 *
 * Keys: LEFT/RIGHT select channel - UP/DOWN volume - M mute
 *       F  toggle a sustained FM test note (hold it while you sweep!)
 *       T  play C:\TADA.WAV (via C:\VEWPLAY.EXE, PCM path)
 *       Q/Esc  quit (keys the FM note off, leaves mixer as you set it)
 *
 * The value row shows the RAW register read-back (hex) after every write,
 * so a register that ignores writes is immediately visible.
 *
 * Build: C:\WATCOM\BLD VEWMIX
 */
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <i86.h>

#define IAR 0x534
#define IDR 0x535
static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
static void ci_wait(void){ unsigned long i; for(i=0;i<400000UL;i++) if(!(inp(IAR)&0x80)) return; }
static void ci_put(unsigned char x, unsigned char v){ ci_wait(); outp(IAR,x); iod(200); outp(IDR,v); iod(200); }
static unsigned char ci_get(unsigned char x){ ci_wait(); outp(IAR,x); iod(200); return (unsigned char)inp(IDR); }

/* ---- OPL test note ---- */
#define FMA 0x388
static int note_on = 0;
static void opl(unsigned char r, unsigned char v){ outp(FMA,r); iod(60); outp(FMA+1,v); iod(180); }
static void fm_toggle(void)
{
    static unsigned char rv[] = { 0x20,0x21,0x40,0x18,0x60,0xF0,0x80,0x77,
                                  0x23,0x21,0x43,0x00,0x63,0xF0,0x83,0x77,
                                  0xA0,0x98,0xC0,0x30 };
    int i;
    if (note_on) { opl(0xB0,0x11); note_on = 0; }
    else { for (i=0;i<(int)sizeof(rv);i+=2) opl(rv[i],rv[i+1]); opl(0xB0,0x31); note_on = 1; }
}

/* ---- direct text screen ---- */
static unsigned short __far *SCR;
static void put(int r,int c,char ch,unsigned char at){ SCR[r*80+c]=((unsigned short)at<<8)|(unsigned char)ch; }
static void text(int r,int c,const char *s,unsigned char at){ while(*s) put(r,c++,*s++,at); }
static void clr(void){ int i; for(i=0;i<80*25;i++) SCR[i]=0x0720; }

#define NCH 7
static const char *names[NCH] = { "PCM", "AUX1", "AUX2", "LINE", "MONO", "M-BYP", "M-OUT" };
/* stereo-pair channels: index register pairs and attenuation field masks */
static const unsigned char regl[4] = { 0x06, 0x02, 0x04, 0x12 };
static const unsigned char regr[4] = { 0x07, 0x03, 0x05, 0x13 };
static const unsigned char amax[5] = { 63, 31, 31, 31, 15 };

/* adjust channel: dir +1 = louder, -1 = quieter, 0 = toggle mute/bit */
static void adjust(int ch, int dir)
{
    unsigned char l, v, m;
    if (ch <= 3) {
        l = ci_get(regl[ch]); m = (unsigned char)(l & 0x80);
        v = (unsigned char)(l & amax[ch]);
        if (dir > 0 && v > 0) v--;
        else if (dir < 0 && v < amax[ch]) v++;
        else if (dir == 0) m ^= 0x80;
        ci_put(regl[ch], (unsigned char)(m | v));
        ci_put(regr[ch], (unsigned char)(m | v));
    } else {
        l = ci_get(0x1A);                               /* I26 mono control */
        if (ch == 4) {
            m = (unsigned char)(l & 0xF0); v = (unsigned char)(l & 0x0F);
            if (dir > 0 && v > 0) v--;
            else if (dir < 0 && v < 15) v++;
            else if (dir == 0) m ^= 0x80;               /* MIM */
            ci_put(0x1A, (unsigned char)(m | v));
        } else if (ch == 5) ci_put(0x1A, (unsigned char)(l ^ 0x20));   /* MBY */
        else                ci_put(0x1A, (unsigned char)(l ^ 0x40));   /* MOM */
    }
}

static void draw(int sel)
{
    static char buf[32];
    int i, x, h, fill;
    unsigned char l, r, v, i26;
    text(1, 3, "VEWMIX - CF-VEW211 / CS4231A mixer (MODE2)", 0x0F);
    i26 = ci_get(0x1A);
    sprintf(buf, "I26=%02X  FM note: %s ", i26, note_on ? "ON " : "off");
    text(1, 55, buf, 0x07);
    for (i = 0; i < NCH; i++) {
        unsigned char at = (i == sel) ? 0x70 : 0x07;
        x = 3 + i * 11;
        if (i <= 3) {
            l = ci_get(regl[i]); r = ci_get(regr[i]);
            v = (unsigned char)(l & amax[i]);
            sprintf(buf, "%02X/%02X", l, r);
            text(3, x, buf, 0x08);
            if (i == 0) sprintf(buf, "-%2u.%udB", v*15/10, (v*15)%10);
            else { int t = (8 - (int)v) * 15;
                   sprintf(buf, "%c%2d.%ddB", t < 0 ? '-' : '+', abs(t)/10, abs(t)%10); }
            text(4, x, (l & 0x80) ? " MUTED " : buf, (l & 0x80) ? 0x0C : 0x0A);
            fill = (int)(amax[i] - v) * 12 / amax[i];
        } else if (i == 4) {
            v = (unsigned char)(i26 & 0x0F);
            sprintf(buf, "I26=%02X", i26); text(3, x, buf, 0x08);
            sprintf(buf, "-%2udB ", v * 3);
            text(4, x, (i26 & 0x80) ? " MUTED " : buf, (i26 & 0x80) ? 0x0C : 0x0A);
            fill = (int)(15 - v) * 12 / 15;
        } else {
            unsigned char on = (unsigned char)(i26 & (i == 5 ? 0x20 : 0x40));
            text(3, x, "      ", 0x08);
            if (i == 5) text(4, x, on ? "BYPASS!" : "  off  ", on ? 0x0C : 0x0A);
            else        text(4, x, on ? " MUTED " : "  on   ", on ? 0x0C : 0x0A);
            fill = on ? 12 : 0;
        }
        for (h = 0; h < 12; h++)
            text(17 - h, x + 1, h < fill ? "\xDB\xDB\xDB" : " \xFA ", (i >= 5) ? 0x0E : 0x0B);
        sprintf(buf, i == sel ? ">%-5s<" : " %-5s ", names[i]);
        text(19, x - 1, buf, at);
    }
    text(21, 3, "MONO carries the FM synth (suspected).  M-BYP skips ALL volume controls.", 0x08);
    text(23, 3, "LEFT/RIGHT select   UP/DOWN volume   M mute   F fm-note   T tada   Q quit", 0x07);
}

int main(void)
{
    int sel = 0, k;
    union REGS rr;
    if ((unsigned char)inp(IAR) == 0xFF) { printf("no codec at %03X - run VEW21XGO first\n", IAR); return 1; }
    ci_put(0x0C, 0x40);                                  /* MODE2 on */
    if (!(ci_get(0x0C) & 0x40)) { printf("MODE2 did not latch - not a CS4231?\n"); return 1; }
    SCR = (unsigned short __far *)MK_FP(0xB800, 0);
    clr();
    for (;;) {
        draw(sel);
        k = getch();
        if (k == 0 || k == 0xE0) {
            k = getch();
            if      (k == 0x4B) sel = (sel + NCH - 1) % NCH;   /* left */
            else if (k == 0x4D) sel = (sel + 1) % NCH;         /* right */
            else if (k == 0x48) adjust(sel, +1);               /* up */
            else if (k == 0x50) adjust(sel, -1);               /* down */
        } else {
            if (k >= 'a' && k <= 'z') k -= 32;
            if      (k == 'Q' || k == 27) break;
            else if (k == 'M') adjust(sel, 0);
            else if (k == 'F') fm_toggle();
            else if (k == 'T') { system("C:\\VEWPLAY.EXE C:\\TADA.WAV >NUL"); clr(); }
        }
    }
    if (note_on) opl(0xB0, 0x11);                        /* never leave a note hanging */
    rr.w.ax = 0x0003; int86(0x10, &rr, &rr);             /* clean text mode */
    return 0;
}

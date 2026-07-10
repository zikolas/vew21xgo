/* VEWXCTL.C v2 - CF-VEW211: interactive cross-product explorer for the
 * remaining FM-volume candidates - everything live at once while an FM
 * note holds, so COMBINATIONS are finally testable by ear:
 *
 *   XCTL  - CS4231A external-control pins (I10 bits 6/7, 4 states; they
 *           drive card logic such as the on-board TC4W66F analog switch)
 *   206   - undeclared vendor register, 7 writable bits (0..127)
 *   208   - undeclared vendor register, 4 writable bits (0..15)
 *   AUX1 / AUX2 / LINE - codec inputs (volume + mute), so any state that
 *           ROUTES the FM into the codec is immediately controllable
 *
 * (attr 0x204 is the EEPROM COMMIT STROBE and is deliberately absent.)
 * All writes are millisecond-paced and verified - the CS4231A-KQ drops
 * fast writes. Everything is restored on quit.
 *
 * Keys: LEFT/RIGHT select - UP/DOWN value - 0-7 toggle raw bit (206/208)
 *       M mute (Aux/Line) - F FM note on/off - T play TADA - Q quit
 *
 * Requires the card enabled (VEW21XGO first). Build: C:\WATCOM\BLD VEWXCTL
 */
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
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
static void aw_put(unsigned off, unsigned char v)
{
    int t;
    for (t = 0; t < 5; t++) { AW[off] = v; MS(1); if (AW[off] == v) return; }
}

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

#define NCH 6
/* channel kinds */
#define K_XCTL 0
#define K_A206 1
#define K_A208 2
#define K_CPAIR 3                                /* codec stereo pair with mute */
static struct { int kind; unsigned char rl, rr; int vmax; const char *name; }
ch[NCH] = {
    { K_XCTL,  0x0A, 0,    3,   "XCTL " },
    { K_A206,  0,    0,    127, " 206 " },
    { K_A208,  0,    0,    15,  " 208 " },
    { K_CPAIR, 0x02, 0x03, 31,  "AUX1 " },
    { K_CPAIR, 0x04, 0x05, 31,  "AUX2 " },
    { K_CPAIR, 0x12, 0x13, 31,  "LINE " }
};
static int xctl = 0;                             /* 0..3 -> I10 = state<<6 */

static int rval(int i)
{
    switch (ch[i].kind) {
        case K_XCTL:  return xctl;
        case K_A206:  return (AW[0x206] & 0xFE) >> 1;
        case K_A208:  return AW[0x208] & 0x0F;
        default:      return ch[i].vmax - (ci_get(ch[i].rl) & 0x1F);  /* loudness (0=quiet) */
    }
}
static void wval(int i, int v)
{
    unsigned char m;
    if (v < 0) v = 0;
    if (v > ch[i].vmax) v = ch[i].vmax;
    switch (ch[i].kind) {
        case K_XCTL:  xctl = v; ci_put(0x0A, (unsigned char)(v << 6)); break;
        case K_A206:  aw_put(0x206, (unsigned char)(v << 1)); break;
        case K_A208:  aw_put(0x208, (unsigned char)v); break;
        default:
            m = (unsigned char)(ci_get(ch[i].rl) & 0x80);
            ci_put(ch[i].rl, (unsigned char)(m | (ch[i].vmax - v)));
            ci_put(ch[i].rr, (unsigned char)(m | (ch[i].vmax - v)));
    }
}
static void mute_toggle(int i)
{
    unsigned char l;
    if (ch[i].kind != K_CPAIR) return;
    l = ci_get(ch[i].rl);
    ci_put(ch[i].rl, (unsigned char)(l ^ 0x80));
    ci_put(ch[i].rr, (unsigned char)((ci_get(ch[i].rr) & 0x7F) | ((l ^ 0x80) & 0x80)));
}

static void draw(int sel)
{
    static char buf[64];
    static const char *xn[4] = { " off/off ", " XCTL0   ", " XCTL1   ", " X0+X1   " };
    int i, h, fill, x;
    text(1, 3, "VEWXCTL v2 - FM-volume cross-product explorer (204=strobe: EXCLUDED)", 0x0F);
    sprintf(buf, "I10=%02X [206]=%02X [208]=%02X   FM note: %s ",
            ci_get(0x0A), AW[0x206], AW[0x208], note_on ? "ON " : "off");
    text(2, 3, buf, 0x08);
    for (i = 0; i < NCH; i++) {
        unsigned char at = (i == sel) ? 0x70 : 0x07;
        int v = rval(i);
        x = 4 + i * 12;
        if (ch[i].kind == K_XCTL) text(4, x, xn[v & 3], 0x0A);
        else if (ch[i].kind == K_CPAIR) {
            unsigned char l = ci_get(ch[i].rl);
            sprintf(buf, (l & 0x80) ? "  MUTED  " : "  %2d/31  ", v);
            text(4, x, buf, (l & 0x80) ? 0x0C : 0x0A);
        } else { sprintf(buf, " %3d/%-3d ", v, ch[i].vmax); text(4, x, buf, 0x0A); }
        fill = ch[i].vmax ? v * 12 / ch[i].vmax : 0;
        for (h = 0; h < 12; h++)
            text(17 - h, x + 3, h < fill ? "\xDB\xDB\xDB" : " \xFA ", 0x0B);
        sprintf(buf, i == sel ? ">%s<" : " %s ", ch[i].name);
        text(19, x + 1, buf, at);
    }
    text(21, 3, "Hold F-note, set an XCTL state, then sweep 206/208/AUX1 - combos are live.", 0x08);
    text(23, 3, "LEFT/RIGHT sel  UP/DOWN value  0-7 bit(206/208)  M mute  F note  T tada  Q quit", 0x07);
}

int main(void)
{
    int sel = 1, k;
    unsigned char en06;
    union REGS rr;

    pidx = PCIC; soff = 0;
    AW = (unsigned char __far *)MK_FP(MEMSEG, 0);
    if ((rd(0x00) & 0xC0) != 0x80) { printf("no 82365\n"); return 1; }
    if ((unsigned char)inp(IAR) == 0xFF) { printf("no codec at 534 - run VEW21XGO first\n"); return 1; }

    /* attr window (0x204 never touched) */
    en06 = rd(0x06);
    {
        unsigned woff = ((unsigned)(0 - (MEMSEG >> 8)) & 0x3FFF) | 0x4000;
        wr(0x10, (MEMSEG >> 8) & 0xFF); wr(0x11, 0x00);
        wr(0x12, ((MEMSEG >> 8) + 3) & 0xFF); wr(0x13, 0x00);
        wr(0x14, woff & 0xFF); wr(0x15, (woff >> 8) & 0xFF);
        wr(0x06, en06 | 0x01); iod(500);
    }

    ci_put(0x0C, 0x40);                          /* MODE2 for LINE regs */
    ci_put(0x02, 0x08); ci_put(0x03, 0x08);      /* Aux1 open 0dB */
    ci_put(0x04, 0x88); ci_put(0x05, 0x88);      /* Aux2 muted (M to open) */
    ci_put(0x12, 0x88); ci_put(0x13, 0x88);      /* LINE muted (M to open) */

    SCR = (unsigned short __far *)MK_FP(0xB800, 0);
    clr();
    for (;;) {
        draw(sel);
        k = getch();
        if (k == 0 || k == 0xE0) {
            k = getch();
            if      (k == 0x4B) sel = (sel + NCH - 1) % NCH;
            else if (k == 0x4D) sel = (sel + 1) % NCH;
            else if (k == 0x48) wval(sel, rval(sel) + 1);
            else if (k == 0x50) wval(sel, rval(sel) - 1);
        } else {
            if (k >= 'a' && k <= 'z') k -= 32;
            if      (k == 'Q' || k == 27) break;
            else if (k == 'F') fm_toggle();
            else if (k == 'M') mute_toggle(sel);
            else if (k >= '0' && k <= '7') {
                if (ch[sel].kind == K_A206) aw_put(0x206, (unsigned char)(AW[0x206] ^ (1 << (k - '0'))));
                else if (ch[sel].kind == K_A208) aw_put(0x208, (unsigned char)(AW[0x208] ^ (1 << (k - '0'))));
            }
            else if (k == 'T') { system("C:\\VEWPLAY.EXE C:\\TADA.WAV >NUL"); clr(); }
        }
    }
    if (note_on) opl(0xB0, 0x11);
    ci_put(0x0A, 0x00);
    aw_put(0x206, 0x00); aw_put(0x208, 0x00);
    ci_put(0x02, 0x88); ci_put(0x03, 0x88);
    ci_put(0x04, 0x88); ci_put(0x05, 0x88);
    ci_put(0x12, 0x88); ci_put(0x13, 0x88);
    wr(0x06, en06);
    rr.w.ax = 0x0003; int86(0x10, &rr, &rr);
    printf("VEWXCTL: restored (XCTL 0, [206]=0, [208]=0, Aux/Line muted).\n");
    return 0;
}

/* VEWVND.C v2 - CF-VEW211: interactive explorer for the MEI ASIC's unknown
 * controls: the three UNDECLARED vendor config registers found by VEWASIC
 * plus the four WRITE-ONLY(?) I/O ports in front of the codec:
 *
 *   attr 0x204 - 1 writable bit  (bit0)
 *   attr 0x206 - 7 writable bits (bits 7..1)
 *   attr 0x208 - 4 writable bits (bits 3..0)
 *   I/O 0x530..0x533 - read 0x00, nothing read-latches: possibly write-only
 *                      control ports (columns show live read + last write)
 *
 * v1 finding (listening test, 2026-07-09): sweeping 204/206/208 has NO audible effect
 * on the FM note or PCM - they are not the FM volume. The I/O ports are the
 * remaining candidates, hence v2.
 *
 * Run VEW21XGO first. Hold the FM note with F and sweep / bit-toggle each
 * candidate; report anything audible.
 *
 * Keys: LEFT/RIGHT select - UP/DOWN step value - 0-7 toggle raw bit
 *       Z zero - X max - F FM note on/off - T play TADA - Q quit
 *       (quit keys the note off; final values are printed for the record)
 *
 * Build: C:\WATCOM\BLD VEWVND
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

static unsigned char __far *AW;

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
/* off, writable mask, value shift, value max, io? (io = write-only, shadowed) */
static struct { unsigned off; unsigned char mask; int shift; int vmax; int io; const char *name; }
regs[NCH] = {
    { 0x204, 0x01, 0, 1,   0, " 204 " },
    { 0x206, 0xFE, 1, 127, 0, " 206 " },
    { 0x208, 0x0F, 0, 15,  0, " 208 " },
    { 0x530, 0xFF, 0, 255, 1, "530io" },
    { 0x531, 0xFF, 0, 255, 1, "531io" },
    { 0x532, 0xFF, 0, 255, 1, "532io" },
    { 0x533, 0xFF, 0, 255, 1, "533io" }
};
static unsigned char shad[NCH];                    /* last value written to io ports */

static unsigned char rreg(int i){ return regs[i].io ? shad[i] : AW[regs[i].off]; }
static unsigned char rlive(int i){ return regs[i].io ? (unsigned char)inp(regs[i].off) : AW[regs[i].off]; }
static void wreg(int i, unsigned char v)
{
    if (regs[i].io) { outp(regs[i].off, v); shad[i] = v; }
    else AW[regs[i].off] = v;
    iod(400);
}
static int  rval(int i){ return (rreg(i) & regs[i].mask) >> regs[i].shift; }
static void wval(int i, int v)
{
    if (v < 0) v = 0;
    if (v > regs[i].vmax) v = regs[i].vmax;
    wreg(i, (unsigned char)((rreg(i) & ~regs[i].mask) | ((v << regs[i].shift) & regs[i].mask)));
}

static void draw(int sel)
{
    static char buf[64];
    int i, h, fill, x;
    text(1, 3, "VEWVND v2 - MEI ASIC unknown controls (vendor regs + write-only ports)", 0x0F);
    sprintf(buf, "COR=%02X CCSR=%02X   FM note: %s ", AW[0x200], AW[0x202], note_on ? "ON " : "off");
    text(2, 3, buf, 0x08);
    for (i = 0; i < NCH; i++) {
        unsigned char at = (i == sel) ? 0x70 : 0x07;
        x = 3 + i * 11;
        sprintf(buf, "rd=%02X", rlive(i));
        text(4, x + 1, buf, 0x08);
        sprintf(buf, regs[i].io ? "wr=%02X" : "     ", shad[i]);
        text(5, x + 1, buf, 0x08);
        sprintf(buf, "%3d", rval(i));
        text(6, x + 2, buf, 0x0A);
        fill = regs[i].vmax ? rval(i) * 11 / regs[i].vmax : 0;
        for (h = 0; h < 11; h++)
            text(18 - h, x + 2, h < fill ? "\xDB\xDB\xDB" : " \xFA ", 0x0B);
        sprintf(buf, i == sel ? ">%s<" : " %s ", regs[i].name);
        text(20, x + 1, buf, at);
    }
    text(22, 3, "204/206/208: no audible effect (v1 result). Now try the io columns.", 0x08);
    text(23, 3, "LEFT/RIGHT sel  UP/DOWN value  0-7 bit  Z zero  X max  F note  T tada  Q quit", 0x07);
}

int main(void)
{
    int sel = 3, k, i;                             /* start on 0x530 */
    unsigned start, stop, woff;
    unsigned char en06;
    union REGS rr;

    pidx = PCIC; soff = 0;
    AW = (unsigned char __far *)MK_FP(MEMSEG, 0);
    if ((rd(0x00) & 0xC0) != 0x80) { printf("no 82365\n"); return 1; }
    if ((unsigned char)inp(0x534) == 0xFF) { printf("no codec at 534 - run VEW21XGO first\n"); return 1; }
    for (i = 0; i < NCH; i++) shad[i] = 0;

    /* map attribute window at D000 (kept only while we run) */
    en06 = rd(0x06);
    start = MEMSEG >> 8; stop = (MEMSEG >> 8) + 3;
    woff  = ((unsigned)(0 - (MEMSEG >> 8)) & 0x3FFF) | 0x4000;
    wr(0x10, start & 0xFF); wr(0x11, 0x00);
    wr(0x12, stop  & 0xFF); wr(0x13, 0x00);
    wr(0x14, woff  & 0xFF); wr(0x15, (woff >> 8) & 0xFF);
    wr(0x06, en06 | 0x01); iod(500);

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
            else if (k == 'Z') wreg(sel, (unsigned char)(rreg(sel) & ~regs[sel].mask));
            else if (k == 'X') wval(sel, regs[sel].vmax);
            else if (k >= '0' && k <= '7')
                wreg(sel, (unsigned char)(rreg(sel) ^ (1 << (k - '0'))));
            else if (k == 'T') { system("C:\\VEWPLAY.EXE C:\\TADA.WAV >NUL"); clr(); }
        }
    }
    if (note_on) opl(0xB0, 0x11);
    {
        unsigned char f[NCH];
        for (i = 0; i < NCH; i++) f[i] = rreg(i);
        wr(0x06, en06);
        rr.w.ax = 0x0003; int86(0x10, &rr, &rr);
        printf("VEWVND final: [204]=%02X [206]=%02X [208]=%02X  io wr: [530]=%02X [531]=%02X [532]=%02X [533]=%02X\n",
               f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
    }
    return 0;
}

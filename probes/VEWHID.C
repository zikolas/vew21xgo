/* VEWHID.C - CF-VEW211: interactive explorer for the HIDDEN I/O register
 * pair at codec base+8/+9.
 *
 * Discovery (2026-07-10/11): the MEI ASIC only decodes base+8/+9 while the
 * undeclared vendor registers hold the COMBINATION [206]=0x38 AND [208]=0x05
 * (the values the period vendor driver programs; either register alone
 * re-locks the decode). Unlocked, +8 reads 0xA?: upper nibble fixed ID 0xA,
 * low nibble = 3 latching control bits (0,2,3; bit1 never reads back -
 * possibly a strobe). +9 reads 0xBC so far. Function unknown - that's what
 * this tool is for.
 *
 * VEWHID arms the lock on start (verified), then exposes +8 and +9 live:
 *   LEFT/RIGHT select - UP/DOWN value - 0-7 toggle raw bit - F FM note
 *   T play TADA - Q quit (restores +8=0 and RE-LOCKS 206/208 to 0)
 *
 * All writes paced+verified where readable. Requires VEW21XGO first.
 * Build: C:\WATCOM\BLD VEWHID
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

#define BASE 0x530
#define H8 (BASE + 8)
#define H9 (BASE + 9)

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

/* ---- screen ---- */
static unsigned short __far *SCR;
static void put(int r,int c,char ch,unsigned char at){ SCR[r*80+c]=((unsigned short)at<<8)|(unsigned char)ch; }
static void text(int r,int c,const char *s,unsigned char at){ while(*s) put(r,c++,*s++,at); }
static void clr(void){ int i; for(i=0;i<80*25;i++) SCR[i]=0x0720; }

static unsigned char shad8 = 0, shad9 = 0;       /* last written values */

static void draw(int sel)
{
    static char buf[64];
    int i, h, fill, x;
    unsigned char live8 = (unsigned char)inp(H8), live9 = (unsigned char)inp(H9);
    text(1, 3, "VEWHID - hidden register pair at base+8/+9 (combination-locked bank)", 0x0F);
    sprintf(buf, "lock: [206]=%02X [208]=%02X %s   live: +8=%02X +9=%02X   FM note: %s ",
            AW[0x206], AW[0x208],
            (live8 != 0xFF) ? "UNLOCKED" : "LOCKED!!", live8, live9,
            note_on ? "ON " : "off");
    text(2, 3, buf, 0x08);
    for (i = 0; i < 2; i++) {
        unsigned char at = (i == sel) ? 0x70 : 0x07;
        unsigned char shad = i ? shad9 : shad8;
        unsigned char live = i ? live9 : live8;
        x = 12 + i * 24;
        sprintf(buf, "wr=%02X rd=%02X", shad, live);
        text(4, x, buf, 0x0A);
        fill = shad * 12 / 255;
        for (h = 0; h < 12; h++)
            text(17 - h, x + 3, h < fill ? "\xDB\xDB\xDB\xDB" : "  \xFA " , 0x0B);
        sprintf(buf, i == sel ? ">  +%d  <" : "   +%d   ", 8 + i);
        text(19, x + 1, buf, at);
    }
    text(21, 3, "+8 low nibble: bits 0/2/3 latch, bit1 = write-only(?strobe). +9 = unknown.", 0x08);
    text(23, 3, "LEFT/RIGHT sel  UP/DOWN value  0-7 toggle bit  F note  T tada  Q quit", 0x07);
}

int main(void)
{
    int sel = 0, k;
    unsigned char en06;
    union REGS rr;

    pidx = PCIC; soff = 0;
    AW = (unsigned char __far *)MK_FP(MEMSEG, 0);
    if ((rd(0x00) & 0xC0) != 0x80) { printf("no 82365\n"); return 1; }
    if ((unsigned char)inp(BASE + 4) == 0xFF) { printf("no codec - run VEW21XGO first\n"); return 1; }

    en06 = rd(0x06);
    {
        unsigned woff = ((unsigned)(0 - (MEMSEG >> 8)) & 0x3FFF) | 0x4000;
        wr(0x10, (MEMSEG >> 8) & 0xFF); wr(0x11, 0x00);
        wr(0x12, ((MEMSEG >> 8) + 3) & 0xFF); wr(0x13, 0x00);
        wr(0x14, woff & 0xFF); wr(0x15, (woff >> 8) & 0xFF);
        wr(0x06, en06 | 0x01); iod(500);
    }
    aw_put(0x206, 0x38); aw_put(0x208, 0x05);    /* arm the combination lock */
    if ((unsigned char)inp(H8) == 0xFF) {
        printf("unlock FAILED (+8 reads FF) - check card state.\n");
        wr(0x06, en06);
        return 2;
    }

    SCR = (unsigned short __far *)MK_FP(0xB800, 0);
    clr();
    for (;;) {
        draw(sel);
        k = getch();
        if (k == 0 || k == 0xE0) {
            k = getch();
            if      (k == 0x4B || k == 0x4D) sel ^= 1;
            else if (k == 0x48) { if (sel) { shad9++; outp(H9, shad9); } else { shad8++; outp(H8, shad8); } MS(1); }
            else if (k == 0x50) { if (sel) { shad9--; outp(H9, shad9); } else { shad8--; outp(H8, shad8); } MS(1); }
        } else {
            if (k >= 'a' && k <= 'z') k -= 32;
            if      (k == 'Q' || k == 27) break;
            else if (k == 'F') fm_toggle();
            else if (k >= '0' && k <= '7') {
                if (sel) { shad9 ^= (unsigned char)(1 << (k - '0')); outp(H9, shad9); }
                else     { shad8 ^= (unsigned char)(1 << (k - '0')); outp(H8, shad8); }
                MS(1);
            }
            else if (k == 'T') { system("C:\\VEWPLAY.EXE C:\\TADA.WAV >NUL"); clr(); }
        }
    }
    if (note_on) opl(0xB0, 0x11);
    outp(H8, 0x00); MS(1); outp(H9, 0x00); MS(1);
    aw_put(0x206, 0x00); aw_put(0x208, 0x00);    /* re-lock */
    wr(0x06, en06);
    rr.w.ax = 0x0003; int86(0x10, &rr, &rr);
    printf("VEWHID: +8/+9 zeroed, bank re-locked, window restored.\n");
    return 0;
}

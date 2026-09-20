/* JSCDUMP.C 1.0 - snapshot socket 0 as another enabler (vendor, CS) left it.
 * Reads PCIC regs with interrupts off around each index/data pair so a
 * resident Card Services poller cannot race the index register. Dumps the
 * I/O windows' ports, the codec registers if INIT is clear, an OPL presence
 * check without timers, and attribute 000-1FF + 400-4FF through an enabled
 * attribute window or, failing that, a borrowed free window at D000.
 * Never touches power, interface mode, IRQ routing or the I/O windows.
 * Build: C:\WATCOM\BLD.BAT JSCDUMP     Run: JSCDUMP [/S n] [/NOCODEC]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>

#define PCIC 0x3E0

static unsigned sockoff = 0;
static unsigned seg = 0xD000;

static unsigned char rd(unsigned char i)
{
    unsigned char v;
    _disable();
    outp(PCIC, i + sockoff); v = (unsigned char)inp(PCIC + 1);
    _enable();
    return v;
}
static void wr(unsigned char i, unsigned char v)
{
    _disable();
    outp(PCIC, i + sockoff); outp(PCIC + 1, v);
    _enable();
}
static void dly(unsigned n){ while (n--) inp(0x80); }
static unsigned char memb(unsigned off){ return *(unsigned char __far *)MK_FP(seg, off); }

static void dumpports(unsigned base, int n)
{
    int i;
    printf("  %03X:", base);
    for (i = 0; i < n; i++) printf(" %02X", inp(base + i));
    printf("\n");
}

static void codec(unsigned base)
{
    unsigned char b, misc, idx0;
    int i;
    printf("  codec at %03X: 533=%02X", base, inp(base + 3));
    b = inp(base + 4);
    printf(" IAR=%02X", b);
    if (b == 0xFF || (b & 0x80)) { printf(" - %s\n", b == 0xFF ? "floats" : "INIT set"); return; }
    idx0 = b & 0x1F;
    printf("\n  I0-I15:");
    for (i = 0; i < 16; i++) { outp(base + 4, (unsigned char)i); printf(" %02X", inp(base + 5)); }
    outp(base + 4, 0x0C); misc = inp(base + 5);
    printf("\n  I12 MISC=%02X id nibble %X", misc, misc & 0x0F);
    if ((misc & 0x0F) == 0x0A) {
        outp(base + 4, 0x0C); outp(base + 5, misc | 0x40);
        printf("\n  I16-I31:");
        for (i = 16; i < 32; i++) { outp(base + 4, (unsigned char)i); printf(" %02X", inp(base + 5)); }
        outp(base + 4, 25); b = inp(base + 5);
        printf("\n  I25 VERSION=%02X", b);
        outp(base + 4, 0x0C); outp(base + 5, misc);
    }
    outp(base + 4, idx0);
    printf("\n");
}

static void opl(unsigned base)
{
    unsigned char s0 = inp(base), s1;
    outp(base, 0x04); dly(10); outp(base + 1, 0x60); dly(30);
    outp(base, 0x04); dly(10); outp(base + 1, 0x80); dly(30);
    s1 = inp(base);
    printf("  OPL at %03X: status %02X -> %02X after timer mask+reset = %s\n", base, s0, s1,
           (s1 & 0xE0) == 0 ? "OPL present" : "nothing answers");
}

static void attrdump(unsigned from, unsigned to)
{
    unsigned off;
    for (off = from; off < to; off += 2) {
        if ((off & 0x1E) == 0) printf("\n  %03X:", off);
        printf(" %02X", memb(off));
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    unsigned char r[0x40], sv[6];
    unsigned i, n, iolo[2], iohi[2];
    int nocodec = 0, attrwin = -1, freewin = -1;

    for (i = 1; i < (unsigned)argc; i++) {
        if (!stricmp(argv[i], "/NOCODEC")) nocodec = 1;
        else if (!stricmp(argv[i], "/S") && i + 1 < (unsigned)argc) sockoff = (atoi(argv[++i]) & 1) * 0x40;
        else { printf("usage: JSCDUMP [/S n] [/NOCODEC]\n"); return 1; }
    }
    printf("JSCDUMP 1.0 - PCIC 3E0 bank %02X\n", sockoff);
    if ((rd(0x00) & 0xC0) != 0x80) { printf("no 82365-class PCIC (id %02X)\n", rd(0x00)); return 1; }
    for (i = 0; i < 0x40; i++) r[i] = rd((unsigned char)i);
    printf("PCIC regs:");
    for (i = 0; i < 0x40; i++) { if ((i & 15) == 0) printf("\n  %02X:", i); printf(" %02X", r[i]); }
    printf("\n");
    printf("status %02X: card %s, power %s, ready %d\n", r[1], (r[1] & 0x0C) == 0x0C ? "in" : "OUT",
           (r[1] & 0x40) ? "on" : "off", (r[1] >> 5) & 1);
    printf("power 02=%02X: Vcc %s, Vpp1 %d Vpp2 %d (0=off 1=Vcc 2=12V)\n", r[2],
           (r[2] & 0x18) == 0x10 ? "5V" : (r[2] & 0x18) == 0x18 ? "3.3V" : "?", r[2] & 3, (r[2] >> 2) & 3);
    printf("intctl 03=%02X: %s interface, IRQ %d, reset %s\n", r[3], (r[3] & 0x20) ? "I/O" : "memory",
           r[3] & 0x0F, (r[3] & 0x40) ? "released" : "ASSERTED");
    printf("winenable 06=%02X ioctl 07=%02X (win0 %s%s, win1 %s%s)\n", r[6], r[7],
           (r[7] & 1) ? "16-bit" : "8-bit", (r[7] & 2) ? "+IOIS16" : "",
           (r[7] & 0x10) ? "16-bit" : "8-bit", (r[7] & 0x20) ? "+IOIS16" : "");
    for (n = 0; n < 2; n++) {
        iolo[n] = r[8 + n * 4] | (r[9 + n * 4] << 8);
        iohi[n] = r[10 + n * 4] | (r[11 + n * 4] << 8);
        printf("I/O win%u: %03X-%03X %s\n", n, iolo[n], iohi[n], (r[6] & (0x40 << n)) ? "ENABLED" : "off");
    }
    for (n = 0; n < 5; n++) {
        unsigned b = 0x10 + n * 8;
        unsigned start = r[b] | ((r[b + 1] & 0x0F) << 8), stop = r[b + 2] | ((r[b + 3] & 0x0F) << 8);
        unsigned off = r[b + 4] | ((r[b + 5] & 0x3F) << 8);
        int en = (r[6] >> n) & 1;
        printf("mem win%u: sys %03X000-%03XFFF card off %04X000 %s%s %s\n", n, start, stop, off,
               (r[b + 5] & 0x40) ? "ATTR" : "common", (r[b + 5] & 0x80) ? " WP" : "", en ? "ENABLED" : "off");
        if (en && (r[b + 5] & 0x40) && attrwin < 0 && ((start + off) & 0x3FFF) == 0) { attrwin = (int)n; seg = start << 8; }
        if (!en && freewin < 0) freewin = (int)n;
    }
    for (n = 0; n < 2; n++) {
        if (!(r[6] & (0x40 << n))) continue;
        printf("ports of I/O win%u:\n", n);
        for (i = iolo[n]; i <= iohi[n] && i < iolo[n] + 64; i += 16)
            dumpports(i, (iohi[n] - i + 1) < 16 ? (int)(iohi[n] - i + 1) : 16);
        if (!nocodec && iohi[n] >= iolo[n] + 9 && (inp(iolo[n] + 3) & 0x3F) == 0x04) codec(iolo[n]);
        if (iolo[n] == 0x388 || iolo[n] == 0x380) opl(iolo[n]);
        if (iolo[n] == 0x530 && iohi[n] >= 0x53F) { printf("  53C as FM?"); opl(0x53C); }
        if (iolo[n] == 0x530 && iohi[n] >= 0x54F) { printf("  540 as FM?"); opl(0x540); }
    }
    if (attrwin >= 0) {
        printf("attribute space via enabled window %d at %04X:\n", attrwin, seg);
    } else if (freewin >= 0) {
        unsigned b = 0x10 + freewin * 8, start = seg >> 8, stop = start + 3;
        unsigned woff = ((unsigned)(0 - start) & 0x3FFF) | 0x4000;
        printf("no attribute window enabled; borrowing free window %d at %04X:\n", freewin, seg);
        for (i = 0; i < 6; i++) sv[i] = rd((unsigned char)(b + i));
        wr((unsigned char)(b + 0), start & 0xFF); wr((unsigned char)(b + 1), (start >> 8) & 0x0F);
        wr((unsigned char)(b + 2), stop & 0xFF);  wr((unsigned char)(b + 3), (stop >> 8) & 0x0F);
        wr((unsigned char)(b + 4), woff & 0xFF);  wr((unsigned char)(b + 5), (woff >> 8) & 0xFF);
        wr(0x06, r[6] | (1 << freewin));
        dly(20000);
    } else {
        printf("no window available for attribute space\n");
        return 0;
    }
    printf("  CIS 000-0BE:"); attrdump(0x000, 0x0C0);
    printf("  1E0-1FE:"); attrdump(0x1E0, 0x200);
    printf("  400-4FE:"); attrdump(0x400, 0x500);
    if (attrwin < 0) {
        unsigned b = 0x10 + freewin * 8;
        wr(0x06, r[6]);
        for (i = 0; i < 6; i++) wr((unsigned char)(b + i), sv[i]);
        printf("window %d restored\n", freewin);
    }
    return 0;
}

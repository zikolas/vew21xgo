/* JSCPROBE.C 1.7 - probe for the Panasonic CF-JSC101 "Sound SCSI Card".
 * Socket off, Vcc on with RESET held ~330 ms, release, read the CIS head.
 * If it is the JSC101: program the four config register blocks at attr
 * 420h/440h/460h/480h, map I/O windows 530-54F and 388-38B, read the WSS
 * block, codec ID, OPL timer test, then clear the CORs and power off.
 * Card writes: the four blocks' COR/CCSR/IOBASE/IOLIMIT, codec index/MISC
 * (and I18/I19 with /UNMUTE), OPL timer registers, one optional byte at
 * 538h (/BANK), one optional attribute byte (/ATTR, 400-41E or 426-43E).
 * Build: C:\WATCOM\BLD.BAT JSCPROBE
 * Run:   JSCPROBE [/S n] [/VENDOR] [/COR=xx] [/CCSR=xx] [/IOBASE=xxxx]
 *                 [/COR1=xx] [/CCSR1=xx] [/COR2=xx] [/IOBASE2=xxxx]
 *                 [/COR3=xx] [/LIMIT3] [/IOCTL=xx] [/SR] [/WIN1=xxxx]
 *                 [/ATTR=ooo,vv] [/BANK=xx] [/UNMUTE] [/DING] [/SWEEP=lo-hi]
 *                 [/NOIO] [/HOLD] [/KEEP] [/OFF]
 * /OFF undoes a /KEEP: CORs cleared through the live window, windows off,
 * socket reset and powered down. /DING plays a 1 s AdLib test note on the OPL at 388 (carrier TL -18 dB).
 * /VENDOR = what SNSCDOSV.SYS left on 2026-09-20 (JSCDUMP): block0 COR 28
 * CCSR 08, block1 COR 20 CCSR 08, block2 COR 31 IOBASE 540 IOLIMIT 0F,
 * block3 COR 00 IOLIMIT 0F, PCIC ioctl 2B. /KEEP leaves it all configured
 * and powered so VEWPLAY can run afterwards. A sweep prints each index
 * BEFORE reading its ports so a bus stall leaves the culprit on screen.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

#define PCIC     0x3E0
#define SEG      0xD000
#define COR_ATTR 0x420
#define WSS      0x530
#define FM       0x388

static unsigned sockoff = 0;
static unsigned w1base = FM, w1len = 4;   /* second I/O window */
static const unsigned char jsc_head[11] = { 0x01,0x02,0x00,0xFF,0x17,0x02,0xD1,0xFF,0x11,0x04,0x94 };

static void wr(unsigned char i, unsigned char v){ outp(PCIC, i + sockoff); outp(PCIC + 1, v); }
static unsigned char rd(unsigned char i){ outp(PCIC, i + sockoff); return (unsigned char)inp(PCIC + 1); }
static void dly(unsigned n){ while (n--) inp(0x80); }
static unsigned long ticks(void){ return *(unsigned long __far *)MK_FP(0x40, 0x6C); }
static void wait_ticks(unsigned n){ unsigned long t0 = ticks(); while (ticks() - t0 < n) ; }
static unsigned char memb(unsigned off){ return *(unsigned char __far *)MK_FP(SEG, off); }
static void memwr(unsigned off, unsigned char v){ *(unsigned char __far *)MK_FP(SEG, off) = v; }
static unsigned char cis(unsigned i){ return memb(i * 2); }

static int cis_ok(void)
{
    int i;
    for (i = 0; i < 11; i++) if (cis(i) != jsc_head[i]) return 0;
    return 1;
}

static void status(const char *tag)
{
    unsigned char s = rd(0x01);
    printf("%-13s st=%02X cd=%s bvd=%d%d wp/iois16=%d rdy=%d pwr=%d\n", tag, s,
           (s & 0x0C) == 0x0C ? "in" : "--", s & 1, (s >> 1) & 1,
           (s >> 4) & 1, (s >> 5) & 1, (s >> 6) & 1);
}

static void dumpcis(const char *tag, unsigned from, int n)
{
    int i;
    printf("%-13s cis[%02X]:", tag, from);
    for (i = 0; i < n; i++) printf(" %02X", cis(from + i));
    printf("\n");
}

static void dumpblocks(const char *tag)
{
    unsigned b;
    printf("%-9s blocks:", tag);
    for (b = 0x420; b < 0x4A0; b += 0x20)
        printf("  %03X: COR %02X CCSR %02X IOB %02X%02X LIM %02X", b,
               memb(b), memb(b + 2), memb(b + 0x0C), memb(b + 0x0A), memb(b + 0x12));
    printf("\n");
}

static void setmemwin(int attr)
{
    unsigned start = SEG >> 8, stop = (SEG >> 8) + 3;
    unsigned woff = ((unsigned)(0 - (SEG >> 8)) & 0x3FFF) | (attr ? 0x4000 : 0);
    unsigned char en = rd(0x06);
    wr(0x06, en & ~1);
    wr(0x10, start & 0xFF); wr(0x11, (start >> 8) & 0x0F);
    wr(0x12, stop & 0xFF);  wr(0x13, (stop >> 8) & 0x0F);
    wr(0x14, woff & 0xFF);  wr(0x15, (woff >> 8) & 0xFF);
    wr(0x06, en | 1);
    dly(20000);
}

static int io_unsafe(unsigned lo, unsigned hi)
{
    return (lo <= 0x3FF && hi >= 0x3F8) || (lo <= 0x3E7 && hi >= 0x3E0);
}

static int setiowin(int w, unsigned lo, unsigned hi)
{
    unsigned char base = 0x08 + w * 4, en = rd(0x06), bit = w ? 0x80 : 0x40;
    if (io_unsafe(lo, hi)) { printf("REFUSED I/O window %03X-%03X\n", lo, hi); return 0; }
    wr(0x06, en & ~bit);
    wr(base + 0, lo & 0xFF); wr(base + 1, lo >> 8);
    wr(base + 2, hi & 0xFF); wr(base + 3, hi >> 8);
    wr(0x06, en | bit);
    return 1;
}

static void ioline(const char *tag)
{
    unsigned i;
    printf("%-9s 530:", tag);
    for (i = 0; i < 32; i++) printf(" %02X", inp(WSS + i));
    printf("\n%-9s %03X:", "", w1base);
    for (i = 0; i < w1len; i++) printf(" %02X", inp(w1base + i));
    printf("\n");
}

static void opl_probe(void)
{
    unsigned char s1, s2;
    outp(FM, 0x04); dly(10); outp(FM + 1, 0x60); dly(30);
    outp(FM, 0x04); dly(10); outp(FM + 1, 0x80); dly(30);
    s1 = inp(FM);
    outp(FM, 0x02); dly(10); outp(FM + 1, 0xFF); dly(30);
    outp(FM, 0x04); dly(10); outp(FM + 1, 0x21); dly(30);
    dly(2000);
    s2 = inp(FM);
    outp(FM, 0x04); dly(10); outp(FM + 1, 0x60); dly(30);
    outp(FM, 0x04); dly(10); outp(FM + 1, 0x80); dly(30);
    printf("OPL timer test: status %02X -> %02X = %s\n", s1, s2,
           ((s1 & 0xE0) == 0x00 && (s2 & 0xE0) == 0xC0)
               ? ((s1 & 0x06) == 0 ? "OPL3-class" : "OPL2-class") : "no OPL responds");
}

static void fmw(unsigned char reg, unsigned char v){ outp(FM, reg); dly(10); outp(FM + 1, v); dly(40); }
static void ding(void)
{
    fmw(0x01, 0x20); fmw(0xBD, 0x00);
    fmw(0x20, 0x01); fmw(0x40, 0x10); fmw(0x60, 0xF0); fmw(0x80, 0x77); fmw(0xE0, 0x00);
    fmw(0x23, 0x01); fmw(0x43, 0x18); fmw(0x63, 0xF0); fmw(0x83, 0x77); fmw(0xE3, 0x00);
    fmw(0xC0, 0x01);
    fmw(0xA0, 0x98); fmw(0xB0, 0x31);
    printf("FM note on (A-4 ish) ..."); fflush(stdout);
    wait_ticks(18);
    fmw(0xB0, 0x11);
    printf(" off\n");
}

static void ci_put(unsigned char idx, unsigned char v){ outp(WSS + 4, idx); dly(200); outp(WSS + 5, v); dly(200); }
static unsigned char ci_get(unsigned char idx){ outp(WSS + 4, idx); dly(200); return (unsigned char)inp(WSS + 5); }

static int wss_probe(int unmute)
{
    unsigned char b, misc;
    int i;
    b = inp(WSS + 3);
    printf("WSS 533=%02X: %s\n", b, (b & 0x3F) == 0x04 ? "WSS-class block answers" : "not the WSS signature");
    for (i = 0; i < 300 && (inp(WSS + 4) & 0x80); i++) dly(1000);
    b = inp(WSS + 4);
    printf("codec 534=%02X after %d ms%s\n", b, i, (b & 0x80) ? " (INIT still set)" : "");
    if (b == 0xFF || (b & 0x80)) { printf("codec not ready - no register reads attempted\n"); return 0; }
    printf("codec I0-I15:");
    for (i = 0; i < 16; i++) printf(" %02X", ci_get((unsigned char)i));
    printf("\n");
    misc = ci_get(0x0C);
    printf("I12 MISC=%02X id nibble %X (%s)\n", misc, misc & 0x0F,
           (misc & 0x0F) == 0x0A ? "CS4231 family" : (misc & 0x0F) == 0x09 ? "AD1848-class" : "unknown");
    if ((misc & 0x0F) == 0x0A) {
        ci_put(0x0C, misc | 0x40);
        if (unmute) {
            ci_put(18, 0x02); ci_put(19, 0x02);
            printf("line-in un-muted: I18=%02X I19=%02X\n", ci_get(18), ci_get(19));
        }
        printf("mode2 I16-I31:");
        for (i = 16; i < 32; i++) printf(" %02X", ci_get((unsigned char)i));
        printf("\n");
        b = ci_get(25);
        printf("I25 VERSION=%02X (&E7=%02X: 80=CS4231 A0=CS4231A A2=CS4232 B2=CS4232A 83/03=CS4236)\n", b, b & 0xE7);
        ci_put(0x0C, misc & ~0x40);
    }
    return 1;
}

static int hexarg(const char *a) { return (int)strtol(a, NULL, 16); }

int main(int argc, char **argv)
{
    unsigned char sv02, sv03, sv06, sv07, svwin[6], sviow[8];
    int i, hold = 0, keep = 0, noio = 0, configured = 0, sweep = 0, sr = 0, unmute = 0, want_ding = 0, off = 0;
    int cor0 = 0x20, ccsr0 = -1, iobase0 = -1;
    int cor1 = -1, ccsr1 = -1, cor2 = -1, iobase2 = -1, cor3 = -1, limit3 = 0;
    int ioctl = 0x00, bank = -1, attr_off = -1, attr_val = 0;
    int sw_lo = 0x20, sw_hi = 0x2F;
    char tag[12];

    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!stricmp(a, "/HOLD")) hold = 1;
        else if (!stricmp(a, "/KEEP")) keep = 1;
        else if (!stricmp(a, "/OFF")) off = 1;
        else if (!stricmp(a, "/NOIO")) noio = 1;
        else if (!stricmp(a, "/SR")) sr = 1;
        else if (!stricmp(a, "/UNMUTE")) unmute = 1;
        else if (!stricmp(a, "/DING")) want_ding = 1;
        else if (!stricmp(a, "/LIMIT3")) limit3 = 1;
        else if (!stricmp(a, "/VENDOR")) {
            cor0 = 0x28; ccsr0 = 0x08; cor1 = 0x20; ccsr1 = 0x08;
            cor2 = 0x31; iobase2 = 0x0540; cor3 = 0x00; limit3 = 1; ioctl = 0x2B;
        }
        else if (!strnicmp(a, "/SWEEP", 6)) {
            sweep = 1;
            if (a[6] == '=') {
                char *e;
                sw_lo = (int)strtol(a + 7, &e, 16) & 0x3F;
                sw_hi = (*e == '-') ? ((int)strtol(e + 1, NULL, 16) & 0x3F) : sw_lo;
            }
        }
        else if (!strnicmp(a, "/COR=", 5)) cor0 = hexarg(a + 5) & 0xFF;
        else if (!strnicmp(a, "/CCSR=", 6)) ccsr0 = hexarg(a + 6) & 0xFF;
        else if (!strnicmp(a, "/IOBASE=", 8)) iobase0 = hexarg(a + 8) & 0xFFFF;
        else if (!strnicmp(a, "/COR1=", 6)) cor1 = hexarg(a + 6) & 0xFF;
        else if (!strnicmp(a, "/CCSR1=", 7)) ccsr1 = hexarg(a + 7) & 0xFF;
        else if (!strnicmp(a, "/COR2=", 6)) cor2 = hexarg(a + 6) & 0xFF;
        else if (!strnicmp(a, "/IOBASE2=", 9)) iobase2 = hexarg(a + 9) & 0xFFFF;
        else if (!strnicmp(a, "/COR3=", 6)) cor3 = hexarg(a + 6) & 0xFF;
        else if (!strnicmp(a, "/IOCTL=", 7)) ioctl = hexarg(a + 7) & 0xFF;
        else if (!strnicmp(a, "/WIN1=", 6)) { w1base = hexarg(a + 6) & 0xFFF0; w1len = 16; }
        else if (!strnicmp(a, "/BANK=", 6)) bank = hexarg(a + 6) & 0xFF;
        else if (!strnicmp(a, "/ATTR=", 6)) {
            char *e;
            attr_off = (int)strtol(a + 6, &e, 16);
            attr_val = (*e == ',') ? (hexarg(e + 1) & 0xFF) : 0;
            if ((attr_off & 1) || attr_off < 0x400 || attr_off > 0x43E ||
                (attr_off >= 0x420 && attr_off <= 0x424)) {
                printf("/ATTR offset must be even, 400-41E or 426-43E (420-424 never)\n"); return 1;
            }
        }
        else if (!stricmp(a, "/S") && i + 1 < argc) sockoff = (atoi(argv[++i]) & 1) * 0x40;
        else { printf("unknown option %s - see the source header\n", a); return 1; }
    }
    printf("JSCPROBE 1.7 - PCIC 3E0 bank %02X, window D000, Vpp off, ioctl %02X, win1 %03X\n", sockoff, ioctl, w1base);
    printf("blocks to write: 420 COR %02X CCSR %d | 440 COR %d CCSR %d | 460 COR %d IOB %d | 480 COR %d LIM %d\n",
           cor0, ccsr0, cor1, ccsr1, cor2, iobase2, cor3, limit3);
    if ((rd(0x00) & 0xC0) != 0x80) { printf("no 82365-class PCIC (id %02X)\n", rd(0x00)); return 1; }
    sv02 = rd(0x02); sv03 = rd(0x03); sv06 = rd(0x06); sv07 = rd(0x07);
    for (i = 0; i < 6; i++) svwin[i] = rd(0x10 + i);
    for (i = 0; i < 8; i++) sviow[i] = rd(0x08 + i);
    printf("as found: 02=%02X 03=%02X 06=%02X 07=%02X\n", sv02, sv03, sv06, sv07);
    if ((rd(0x01) & 0x0C) != 0x0C) { printf("no card in this socket\n"); return 1; }
    if (off) {
        wr(0x06, sv06 & ~0xC0);                  /* I/O windows off first */
        if (sv06 & 1) {                          /* our /KEEP window still maps attribute space */
            memwr(COR_ATTR, 0x00); memwr(0x440, 0x00); memwr(0x460, 0x00); memwr(0x480, 0x00); dly(20000);
            printf("CORs cleared, 420 reads %02X\n", memb(COR_ATTR));
        }
        wr(0x06, 0x00); wr(0x07, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
        printf("socket 0 windows off, reset, powered down\n");
        return 0;
    }
    if (rd(0x01) & 0x40) { printf("socket already powered - another enabler owns it, refusing (/OFF to undo a /KEEP)\n"); return 1; }
    if (sv06 & 0xC1) { printf("window 0 / I/O windows already enabled (06=%02X), refusing\n", sv06); return 1; }

    wr(0x03, 0x00); wr(0x02, 0x00);
    wait_ticks(18);
    status("off");
    wr(0x02, 0xB0);                              /* Vcc 5V, Vpp off: the CIS declares no Vpp */
    wait_ticks(6);
    wr(0x03, 0x40);
    dly(20000);
    status("powered");
    setmemwin(1);
    wait_ticks(1);
    dumpcis("attr", 0, 16);
    if (!cis_ok()) { printf("not the CF-JSC101 CIS head - nothing configured\n"); goto out; }
    dumpblocks("cold");
    if (noio) goto out;

    if (sr) {
        memwr(COR_ATTR, 0x80); dly(20000);
        printf("SRESET pulse: COR reads %02X under reset\n", memb(COR_ATTR));
    }
    /* block 0 at 420: IOBASE/IOLIMIT first, then COR, then CCSR */
    if (iobase0 >= 0) {
        memwr(COR_ATTR + 0x0A, iobase0 & 0xFF); memwr(COR_ATTR + 0x0C, iobase0 >> 8); memwr(COR_ATTR + 0x12, 0x0F);
        dly(20000);
    }
    memwr(COR_ATTR, (unsigned char)cor0); dly(20000);
    configured = 1;
    if (ccsr0 >= 0) { memwr(COR_ATTR + 2, (unsigned char)ccsr0); dly(20000); }
    /* block 1 at 440 */
    if (cor1 >= 0)  { memwr(0x440, (unsigned char)cor1); dly(20000); }
    if (ccsr1 >= 0) { memwr(0x442, (unsigned char)ccsr1); dly(20000); }
    /* block 2 at 460: the vendor's SCSI block, IOBASE + IOLIMIT then COR */
    if (iobase2 >= 0) {
        memwr(0x46A, iobase2 & 0xFF); memwr(0x46C, iobase2 >> 8); memwr(0x472, 0x0F);
        dly(20000);
    }
    if (cor2 >= 0)  { memwr(0x460, (unsigned char)cor2); dly(20000); }
    /* block 3 at 480 */
    if (limit3)     { memwr(0x492, 0x0F); dly(20000); }
    if (cor3 >= 0)  { memwr(0x480, (unsigned char)cor3); dly(20000); }
    if (attr_off >= 0) {
        printf("attr %03X was %02X", attr_off, memb(attr_off));
        memwr(attr_off, (unsigned char)attr_val); dly(20000);
        printf(", <-%02X reads %02X\n", attr_val, memb(attr_off));
    }
    dumpblocks("config");
    wr(0x03, 0x60);
    dly(20000);
    wr(0x07, (unsigned char)ioctl);
    if (!setiowin(0, WSS, WSS + 0x1F)) goto out;
    if (!setiowin(1, w1base, w1base + w1len - 1)) goto out;
    dly(20000);
    status("io mode");

    if (sweep) {
        int idx;
        printf("COR index sweep %02X-%02X on block 420, CIS head re-checked each step:\n", sw_lo, sw_hi);
        for (idx = sw_lo; idx <= sw_hi; idx++) {
            memwr(COR_ATTR, (unsigned char)idx);
            dly(50000);
            printf("idx %02X:%02X ", idx, memb(COR_ATTR)); fflush(stdout);
            printf("530=%02X ", inp(WSS));     fflush(stdout);
            printf("533=%02X ", inp(WSS + 3)); fflush(stdout);
            printf("534=%02X ", inp(WSS + 4)); fflush(stdout);
            printf("535=%02X ", inp(WSS + 5)); fflush(stdout);
            printf("388=%02X ", inp(FM));      fflush(stdout);
            printf("389=%02X\n", inp(FM + 1)); fflush(stdout);
            if (!cis_ok()) { printf("CIS HEAD CHANGED at idx %02X - HALT\n", idx); dumpcis("attr", 0, 16); break; }
        }
        goto out;
    }
    ioline("as config");
    if (bank >= 0) {
        outp(WSS + 8, (unsigned char)bank);
        wait_ticks(2);
        sprintf(tag, "538<-%02X", bank);
        ioline(tag);
    }
    wss_probe(unmute);
    if (w1base == FM) opl_probe();
    if (want_ding && w1base == FM) ding();
    ioline("end");
    printf("CIS head %s\n", cis_ok() ? "intact" : "CHANGED");
    dumpblocks("end");
    if (keep) { printf("/KEEP: card left configured and powered, windows live\n"); return 0; }

out:
    wr(0x06, rd(0x06) & ~0xC0);
    if (configured) {
        memwr(COR_ATTR, 0x00); memwr(0x440, 0x00); memwr(0x460, 0x00); memwr(0x480, 0x00); dly(20000);
        printf("CORs cleared, 420 reads %02X\n", memb(COR_ATTR));
    }
    wr(0x06, rd(0x06) & ~1);
    for (i = 0; i < 6; i++) wr(0x10 + i, svwin[i]);
    for (i = 0; i < 8; i++) wr(0x08 + i, sviow[i]);
    wr(0x07, sv07);
    if (hold) { wr(0x03, 0x40); printf("/HOLD: socket left powered, memory mode\n"); }
    else { wr(0x03, 0x00); wr(0x02, 0x00); printf("socket powered down\n"); }
    wr(0x06, sv06);
    return 0;
}

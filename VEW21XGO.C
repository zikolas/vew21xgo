/* VEW21XGO.C - Panasonic CF-VEW211 PC Card sound DOS point enabler.
 * Copyright (c) 2026 zikolas. MIT License.
 *
 * Clean-room: built from a known-good CF-VEW211 CIS dump, the public Intel
 * 82365SL PCIC register set, and the public AD1848/CS4231 codec + OPL FM
 * programming models. No Card Services / Socket Services, and no vendor
 * driver code.
 *
 * WHY THIS EXISTS: this card's CIS storage is dead - the whole tuple
 * region reads one stuck fill byte (0x07 on every address), so no Card
 * Services stack or CIS-matching enabler can ever recognize it. The card
 * logic itself is fine: the config registers respond and both function
 * blocks work. VEW21XGO therefore carries the card's entire configuration
 * from a known-good dump and never needs the on-card CIS at all:
 *
 *   config registers at attribute 0x200 (COR + CCSR);
 *   COR index picks the codec base:
 *     idx 0x20 = I/O 0x530 (default)    idx 0x21 = 0xE80
 *     idx 0x22 = 0xF40                  idx 0x23 = 0x604
 *   plus FM (OPL3) at 0x388-0x38B in every configuration.
 *   IRQ, when routed, must be one of {7, 9, 10, 11} (card is level-mode
 *   only - its COR reads back with the LevlREQ bit pinned high).
 *
 * Layout at codec base B: WSS-style, AD1848/CS4231-family codec at
 * B+4..B+7 (IAR/IDR/status/PIO). The codec powers up muted, so VEW21XGO
 * un-mutes the DAC and Aux1/Aux2 inputs (the FM synth reaches the output
 * through an Aux mix input).
 *
 * Build (Open Watcom, 16-bit real mode):  C:\WATCOM\BLD VEW21XGO
 *
 * Usage: VEW21XGO [/IO=530] [/I=0] [/BEEP] [/S=0..7] [/W=D000] [/OFF]
 *   /IO=hex    codec base: 530 (default) / E80 / F40 / 604
 *   /I=dec     IRQ to route: 7, 9, 10 or 11 (default 0 = none)
 *   /BEEP      play a short FM test note after enabling
 *   /S=dec     socket 0..7 (default: auto-scan)
 *   /W=hex     attribute-window segment for the COR (default D000)
 *   /OFF       power the card's socket down and exit
 */
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <i86.h>

#define VEW21XGO_VER "1.0"
#define PCIC_BASE 0x3E0
#define MAX_SOCKET 7

#define VEW_MANF 0x0032         /* MANFID of an intact CF-VEW211 */
#define VEW_CARD 0x0001
#define VEW_COR  0x0200         /* config registers base (attribute space) */
#define FM_BASE  0x388          /* OPL3 FM, present in every configuration */

static unsigned pcic_idx = PCIC_BASE;
static unsigned sockoff  = 0;

static void select_socket(unsigned s)
{
    pcic_idx = PCIC_BASE + (s >> 1) * 2;
    sockoff  = (s & 1) ? 0x40 : 0x00;
}
static unsigned char rd(unsigned r){ outp(pcic_idx, sockoff + r); return (unsigned char)inp(pcic_idx + 1); }
static void          wr(unsigned r, unsigned v){ outp(pcic_idx, sockoff + r); outp(pcic_idx + 1, v); }
static int  controller_present(void){ return (rd(0x00) & 0xC0) == 0x80; }
static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)

/* ---- CIS ---- */
static unsigned g_manf = 0, g_card = 0;
static char     g_vers[80];
static int      g_cisdead = 0;
static unsigned char g_cisfill = 0;

static void read_cis(unsigned seg)
{
    unsigned char __far *p = (unsigned char __far *)MK_FP(seg, 0);
    unsigned off = 0; int g, vi = 0, m, same = 1;
    g_manf = g_card = 0; g_vers[0] = 0; g_cisdead = 0; g_cisfill = p[0];
    /* Damaged-CIS signature: the first 16 tuple bytes are one stuck fill
     * value (this unit reads 0x07 everywhere; a blank part would read
     * 0xFF or 0x00). A real CIS starts with a structured tuple chain. */
    for (g = 1; g < 16; g++) if (p[g * 2] != g_cisfill) { same = 0; break; }
    if (same) { g_cisdead = 1; return; }
    for (g = 0; g < 64; g++) {
        unsigned char code = p[off], link;
        if (code == 0xFF) break;
        if (code == 0x00) { off += 2; continue; }
        link = p[off + 2];
        if (code == 0x20) { g_manf = (unsigned)p[off+4] | ((unsigned)p[off+6]<<8);
                            g_card = (unsigned)p[off+8] | ((unsigned)p[off+10]<<8); }
        else if (code == 0x15) { for (m=2;m<link;m++){ unsigned char c=p[off+4+2*m];
                                 if (vi<79) g_vers[vi++]=c?(char)c:' '; } g_vers[vi]=0; }
        if (link == 0xFF) break;
        off += ((unsigned)link + 2) * 2;
        if (off >= 0x3000) break;
    }
    while (vi > 0 && g_vers[vi-1]==' ') g_vers[--vi]=0;
}
/* Ours if it's an intact CF-VEW211 by MANFID - or a card whose CIS is a
 * dead fill wall, which is exactly the broken unit this tool exists for. */
static int is_vew(void){ return (g_manf == VEW_MANF && g_card == VEW_CARD) || g_cisdead; }

/* Does host segment cseg..cseg+3 pages overlap an ENABLED memory window on any
 * present socket? (i.e. is another card already mapped there?)  Reads only, so
 * it disturbs nothing. Memory windows 0..4 = bits 0..4 of the Window-Enable reg
 * (0x06); each window's start/stop host page is in regs 0x10+w*8 .. +3. */
static int mem_win_overlaps(unsigned cseg)
{
    unsigned cs = cseg >> 8, ce = (cseg >> 8) + 3;      /* our probe's start/stop page (A19-A12) */
    unsigned chip, half, w;
    for (chip = 0; chip < 4; chip++) {
        pcic_idx = PCIC_BASE + chip * 2; sockoff = 0;
        if (!controller_present()) continue;
        for (half = 0; half < 2; half++) {
            unsigned char en;
            sockoff = half ? 0x40 : 0x00;
            en = rd(0x06);
            for (w = 0; w < 5; w++) {
                unsigned base = 0x10 + w * 8, ws, we;
                if (!(en & (1 << w))) continue;         /* window w not enabled */
                ws = ((unsigned)(rd(base+1) & 0x0F) << 8) | rd(base);
                we = ((unsigned)(rd(base+3) & 0x0F) << 8) | rd(base+2);
                if (cs <= we && ws <= ce) return 1;     /* ranges overlap */
            }
        }
    }
    return 0;
}

/* Pick a 16 KB host segment for the CIS probe that no other card has mapped, so
 * the scan won't collide with an in-use card's window. Prefers 'want' (the D000
 * default or a /W value); falls back through a list of commonly-free segments. */
static unsigned find_free_window(unsigned want)
{
    static unsigned cand[] = { 0xD000, 0xCC00, 0xD400, 0xD800, 0xDC00,
                               0xE000, 0xE400, 0xE800, 0xC800, 0 };
    int i;
    if (!mem_win_overlaps(want)) return want;
    for (i = 0; cand[i]; i++) if (!mem_win_overlaps(cand[i])) return cand[i];
    return want;                                        /* nothing free - use the default anyway */
}

/* Power a socket, map its attribute window, read the CIS. Returns 1 if the card
 * there is ours (left powered+mapped). If it's not, the socket is put back the
 * way we found it: a socket we powered up is powered back down, but a card that
 * was ALREADY enabled/in use is restored, not disturbed or powered off. */
static int probe_socket(unsigned memseg)
{
    unsigned start, stop, woff;
    unsigned char s03, s06, s10, s11, s12, s13, s14, s15;
    int was_on;
    if ((rd(0x01) & 0x0C) != 0x0C) return 0;             /* no card */
    was_on = (rd(0x01) & 0x40) != 0;                     /* power already active => a card here is already up/in use */
    /* save the socket regs we borrow for the CIS read, so an already-enabled
     * card can be put back exactly as we found it */
    s03 = rd(0x03); s06 = rd(0x06);
    s10 = rd(0x10); s11 = rd(0x11); s12 = rd(0x12);
    s13 = rd(0x13); s14 = rd(0x14); s15 = rd(0x15);
    if (!was_on) {                                       /* only power a socket we found off */
        wr(0x02, 0x95); MS(20);                          /* 5V */
        if (!(rd(0x01) & 0x40)) { wr(0x02, 0x00); return 0; }
    }
    wr(0x03, 0x40); MS(10);                              /* reset off, mem mode */
    /* never read card memory before the card raises READY */
    { unsigned long t; for (t = 0; t < 600000UL; t++) if (rd(0x01) & 0x20) break; }
    start = memseg >> 8; stop = (memseg >> 8) + 3;
    woff  = ((unsigned)(0 - (memseg >> 8)) & 0x3FFF) | 0x4000;
    wr(0x10, start & 0xFF); wr(0x11, (start >> 8) & 0x3F);
    wr(0x12, stop  & 0xFF); wr(0x13, (stop  >> 8) & 0x3F);
    wr(0x14, woff  & 0xFF); wr(0x15, (woff  >> 8) & 0xFF);
    wr(0x06, rd(0x06) | 0x01);                           /* enable mem win0 (keep other windows) */
    read_cis(memseg);
    if (is_vew()) return 1;                              /* our card: leave it powered + mapped */
    /* not our card - undo the probe without harming an in-use card */
    if (was_on) {                                        /* already-enabled card: restore exactly, DON'T power off */
        wr(0x10, s10); wr(0x11, s11); wr(0x12, s12); wr(0x13, s13);
        wr(0x14, s14); wr(0x15, s15); wr(0x06, s06); wr(0x03, s03);
    } else {                                             /* we powered it up just to peek: power it back down */
        wr(0x06, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
    }
    return 0;
}

/* ---- AD1848/CS4231-family codec at cbase = base+4 ---- */
static unsigned CBASE;
/* wait while the codec is busy (IAR reads 0x80 during power-on calibration /
 * resync). Generous: a cold-booted card is still calibrating for ~20ms. */
static void ccwait(void){ unsigned long i; for (i=0;i<400000UL;i++) if (!(inp(CBASE)&0x80)) return; }
static void ccput(unsigned char idx, unsigned char v){ ccwait(); outp(CBASE, idx); iod(200); outp(CBASE+1, v); iod(200); }
static unsigned char ccget(unsigned char idx){ ccwait(); outp(CBASE, idx); iod(200); return (unsigned char)inp(CBASE+1); }

/* ---- OPL FM at FM_BASE: 0x388 = address/status, 0x389 = data ---- */
static void opl_wr(unsigned char reg, unsigned char v)
{
    outp(FM_BASE, reg); iod(60);                        /* >= 3.3us settle */
    outp(FM_BASE + 1, v); iod(180);                     /* >= 23us settle */
}
/* classic timer-1 detection; returns 0 = absent, 2 = OPL2, 3 = OPL3 */
static int opl_detect(void)
{
    unsigned char s1, s2;
    opl_wr(0x04, 0x60); opl_wr(0x04, 0x80);             /* mask + reset timers */
    s1 = (unsigned char)inp(FM_BASE);
    opl_wr(0x02, 0xFF);                                 /* timer 1 preset */
    opl_wr(0x04, 0x21);                                 /* unmask + start timer 1 */
    MS(2);                                              /* >= 80us tick */
    s2 = (unsigned char)inp(FM_BASE);
    opl_wr(0x04, 0x60); opl_wr(0x04, 0x80);             /* clean up */
    if ((s2 & 0xE0) != 0xC0) return 0;                  /* timer never fired: no OPL */
    return ((s1 & 0x06) == 0) ? 3 : 2;                  /* OPL2 keeps timer status bits */
}
/* short sustained test note (~310 Hz) on FM channel 1, then key-off */
static void fm_beep(void)
{
    static unsigned char rv[] = {
        0x20,0x21, 0x40,0x18, 0x60,0xF0, 0x80,0x77,     /* modulator: mult 1, AD/SR */
        0x23,0x21, 0x43,0x00, 0x63,0xF0, 0x83,0x77,     /* carrier: full level */
        0xA0,0x98, 0xC0,0x30 };                         /* f-num low, L+R out enable */
    int i;
    for (i = 0; i < (int)sizeof(rv); i += 2) opl_wr(rv[i], rv[i+1]);
    opl_wr(0xB0, 0x31);                                 /* key on: block 4 */
    MS(400);
    opl_wr(0xB0, 0x11);                                 /* key off */
}

/* match "/NAME" or "/NAME=val" case-insensitively */
static int sw(const char *a, const char *name, char **val)
{
    int i;
    if (a[0] != '/' && a[0] != '-') return 0;
    for (i = 0; name[i]; i++) { char c = a[1+i]; if (c>='a'&&c<='z') c-=32; if (c != name[i]) return 0; }
    if (a[1+i] == '\0') { *val = 0; return 1; }
    if (a[1+i] == '=') { *val = (char *)a + 1 + i + 1; return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned base = 0x530, irq = 0, sock = 0, memseg = 0xD000;
    int off = 0, sgiven = 0, wgiven = 0, beep = 0, found = 0, any = 0, i;
    unsigned char coridx, corrb, cver;
    int codec_ok, oplv;
    unsigned char __far *cor;

    for (i = 1; i < argc; i++) {
        char *a = argv[i], *vp;
        if (a[0] != '/' && a[0] != '-') continue;
        if      (sw(a, "OFF",  &vp)) off = 1;
        else if (sw(a, "BEEP", &vp)) beep = 1;
        else if (sw(a, "IO",   &vp)) { if (vp) base = (unsigned)strtol(vp, 0, 16); }
        else if (sw(a, "I",    &vp)) { if (vp) irq = (unsigned)strtol(vp, 0, 10); }
        else if (sw(a, "S",    &vp)) { if (vp) { sock = (unsigned)strtol(vp, 0, 10); sgiven = 1; } }
        else if (sw(a, "W",    &vp)) { if (vp) { memseg = (unsigned)strtol(vp, 0, 16); wgiven = 1; } }
        else printf("Ignoring unknown switch: %s\n", a);
    }

    /* codec base <-> COR config index (from the known-good CIS) */
    switch (base) {
        case 0x530: coridx = 0x20; break;
        case 0xE80: coridx = 0x21; break;
        case 0xF40: coridx = 0x22; break;
        case 0x604: coridx = 0x23; break;
        default: printf("Bad /IO=%03X : use 530 / E80 / F40 / 604\n", base); return 2;
    }
    if (irq && irq != 7 && irq != 9 && irq != 10 && irq != 11) {
        printf("Bad /I=%u : this card can only use IRQ 7, 9, 10 or 11\n", irq); return 2;
    }
    if (sgiven && sock > MAX_SOCKET) { printf("Bad /S=%u : 0..%u\n", sock, MAX_SOCKET); return 2; }

    /* Unless the window was pinned with /W, pick a probe segment no other card
     * has mapped - so the CIS read won't collide with a card already using D000. */
    if (!wgiven) {
        unsigned freeseg = find_free_window(memseg);
        if (freeseg != memseg) { printf("Probe window %04X in use by another card; using %04X.\n", memseg, freeseg); memseg = freeseg; }
    }

    /* locate the card */
    if (sgiven) {
        select_socket(sock);
        if (!controller_present()) { printf("No PCIC for socket %u (port %03X)\n", sock, pcic_idx); return 1; }
        found = probe_socket(memseg);
    } else {
        unsigned chip, half;
        for (chip = 0; chip < 4 && !found; chip++) {
            pcic_idx = PCIC_BASE + chip * 2; sockoff = 0;
            if (!controller_present()) continue;
            any = 1;
            for (half = 0; half < 2; half++) {
                sock = chip * 2 + half; select_socket(sock);
                if (probe_socket(memseg)) { found = 1; break; }
            }
        }
        if (!any) { printf("No 82365-class PCIC found (3E0/3E2/3E4/3E6).\n"); return 1; }
    }
    if (!found) {
        if (g_manf) printf("Not the CF-VEW211 (found MANFID %04X/%04X \"%s\").\n", g_manf, g_card, g_vers);
        else        printf("No CF-VEW211 (or dead-CIS card) found in any socket.\n");
        return 3;
    }

    if (off) {
        wr(0x06, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
        printf("CF-VEW211 socket %u powered off.\n", sock);
        return 0;
    }

    /* COR (attribute window probe_socket left mapped). The card pins the
     * LevlREQ bit (0x40) on readback, so verify the index bits only. */
    cor = (unsigned char __far *)MK_FP(memseg, VEW_COR);
    *cor = coridx; MS(5);
    corrb = *cor;

    /* I/O window 0 = codec base..base+9 (8-bit), window 1 = OPL 0x388..0x38B */
    wr(0x08, base & 0xFF);        wr(0x09, (base >> 8) & 0xFF);
    wr(0x0A, (base + 9) & 0xFF);  wr(0x0B, ((base + 9) >> 8) & 0xFF);
    wr(0x0C, FM_BASE & 0xFF);     wr(0x0D, (FM_BASE >> 8) & 0xFF);
    wr(0x0E, (FM_BASE+3) & 0xFF); wr(0x0F, ((FM_BASE+3) >> 8) & 0xFF);
    wr(0x07, 0x00);                                     /* both windows 8-bit */
    wr(0x03, irq ? (0x60 | 0x10 | (irq & 0x0F)) : 0x60);/* I/O mode (+IRQ) */
    wr(0x06, 0xC0);                                     /* enable I/O win0+win1; mem win off */

    /* A cold-booted card was only just powered, so the codec is still running
     * its power-on calibration and reads 0x80 (busy). Wait it out before
     * touching the mixer, or the un-mute writes get dropped. */
    CBASE = base + 4;
    { unsigned long w; for (w = 0; w < 600000UL; w++) if (!(inp(CBASE) & 0x80)) break; }

    /* un-mute: the codec powers up muted, and the FM synth reaches the output
     * through an Aux mix input - so open DAC + Aux1 + Aux2 at 0dB */
    ccput(0x06, 0x00); ccput(0x07, 0x00);               /* L/R DAC out: unmute 0dB */
    ccput(0x02, 0x08); ccput(0x03, 0x08);               /* L/R Aux1: unmute 0dB */
    ccput(0x04, 0x08); ccput(0x05, 0x08);               /* L/R Aux2: unmute 0dB */

    /* verify the codec responds by reading back a mixer reg we just wrote */
    codec_ok = ((ccget(0x02) & 0x1F) == 0x08);
    cver = ccget(0x0C);                                 /* I12: chip ID (this unit: 0x8A) */

    oplv = opl_detect();
    if (beep && oplv) fm_beep();

    printf("VEW21XGO %s - Panasonic CF-VEW211: socket %u%s\n", VEW21XGO_VER, sock, sgiven ? "" : " (auto)");
    if (g_cisdead)
        printf("   CIS: DEAD (stuck %02X fill) - using built-in config table\n", g_cisfill);
    else
        printf("   CIS \"%s\"  MANFID %04X/%04X\n", g_vers, g_manf, g_card);
    printf("   COR @%03X idx %02X (readback %02X, %s)\n",
           VEW_COR, coridx, corrb, ((corrb & 0x3F) == coridx) ? "verified" : "MISMATCH");
    printf("   WSS codec @ %03X (%s, ID %02X)  FM: %s @ %03X\n",
           CBASE, codec_ok ? "responding" : "NOT responding", cver,
           oplv == 3 ? "OPL3" : (oplv == 2 ? "OPL2" : "NOT detected"), FM_BASE);
    if (irq) printf("   IRQ %u (level-mode card)\n", irq);
    else     printf("   no IRQ routed (/I=7|9|10|11 to route one)\n");
    printf("   mixer un-muted (DAC + Aux1/Aux2 at 0dB)\n");
    if (beep && oplv) printf("   FM test note sent\n");
    return 0;
}

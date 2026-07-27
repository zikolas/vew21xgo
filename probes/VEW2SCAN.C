/* VEW2SCAN 1.0 - PASSIVE attribute/I-O recon for the CF-VEW212 ASIC.
 *
 * The VEW212's MEI ASIC is NOT the VEW211's: the sweep this tool performs
 * showed its config register file is just COR (0x200) + CCSR (0x202) --
 * the 211's vendor registers at attr 0x204/0x206/0x208 do not exist here
 * (they read as floating bus).  Nothing learned on the 211 transfers
 * without proof, so this tool remaps the ASIC from zero -- READS ONLY.
 *
 * PASSIVE BY CONSTRUCTION: this program contains NO code path that writes
 * to the card.  The only OUT instructions target the PCIC itself (0x3E0/1)
 * to borrow one memory window, CISDUMP-style: program the window regs while
 * the window is DISABLED, enable, read, restore everything.  It never
 * touches PCIC power, reset, interface mode, IRQ or I/O window registers.
 *
 * Passes:
 *   1  dense attr dump 0x000-0x7FF (shadow, config file, and beyond)
 *   2  mirror map across the 16KB window (which address lines decode?)
 *   3  config region 0x200-0x23E isolated reads with driven-reference
 *      sandwich (distinguishes driven-00 from floating bus-hold)
 *   4  tick check: two spaced reads of the config region (live bits?)
 *   5  I/O reads 0x530-0x539 / 0x388-0x38B, three passes -- ONLY if the
 *      socket is already I/O-configured by an enabler (never configures)
 *
 * Usage:  VEW2SCAN [/S n]        (default: first socket with a card)
 * Build:  C:\WATCOM\BLD.BAT VEW2SCAN
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

#define PCIC 0x3E0
static unsigned sockoff;

static void wr(unsigned char i, unsigned char v){ outp(PCIC, i + sockoff); outp(PCIC + 1, v); }
static unsigned char rd(unsigned char i){ outp(PCIC, i + sockoff); return (unsigned char)inp(PCIC + 1); }
static void dly(unsigned n){ while (n--) inp(0x80); }              /* ~1us each */

static unsigned winseg;
/* attribute memory: dense CIS byte i lives at window offset i*2 */
static unsigned char densb(unsigned i){ return *(unsigned char __far *)MK_FP(winseg, i * 2); }
static unsigned char hostb(unsigned off){ return *(unsigned char __far *)MK_FP(winseg, off); }

/* --- polite window borrow (CISDUMP mapwin, verbatim discipline) ---------- */
static unsigned char sv06, svwin[6];
static int win_base, mapped;

static int free_memwin(unsigned char wen)
{
    int n;
    for (n = 0; n < 5; n++) if (!(wen & (1 << n))) return n;
    return 0;
}

static int mapwin(unsigned seg)
{
    unsigned start, stop, woff;
    int wn, i;
    if ((rd(0x01) & 0x0C) != 0x0C) return 0;    /* no card */
    if (!(rd(0x01) & 0x40)) return 0;           /* not powered: PASSIVE tool won't power it */

    sv06 = rd(0x06);
    wn = free_memwin(sv06);
    win_base = 0x10 + wn * 8;
    for (i = 0; i < 6; i++) svwin[i] = rd(win_base + i);

    start = seg >> 8; stop = (seg >> 8) + 3;
    woff  = ((unsigned)(0 - (seg >> 8)) & 0x3FFF) | 0x4000;   /* 0x4000 = attribute */
    /* window is disabled in reg 0x06 while we program it -- never edit live */
    wr(win_base + 0, start & 0xFF); wr(win_base + 1, (start >> 8) & 0x3F);
    wr(win_base + 2, stop  & 0xFF); wr(win_base + 3, (stop  >> 8) & 0x3F);
    wr(win_base + 4, woff  & 0xFF); wr(win_base + 5, (woff  >> 8) & 0xFF);
    wr(0x06, sv06 | (1 << wn));
    dly(20000);
    mapped = 1;
    return 1;
}

static void unmapwin(void)
{
    int i;
    if (!mapped) return;
    wr(0x06, sv06);                             /* disable ours first */
    for (i = 0; i < 6; i++) wr(win_base + i, svwin[i]);
    mapped = 0;
}

/* pick a window segment whose memory reads open-bus before mapping */
static unsigned pickseg(void)
{
    static unsigned cand[2] = { 0xD000, 0xCC00 };
    int c, i, clean;
    for (c = 0; c < 2; c++) {
        clean = 1;
        for (i = 0; i < 32; i++)
            if (*(unsigned char __far *)MK_FP(cand[c], i) != 0xFF) { clean = 0; break; }
        if (clean) return cand[c];
    }
    return 0;
}

/* --- passes -------------------------------------------------------------- */

static void pass1_dump(void)
{
    unsigned i, j;
    int mirr = 1;
    unsigned firstbad = 0xFFFF;
    printf("--- PASS 1: dense attr 0x000-0x7FF ---\r\n");
    for (i = 0; i < 0x800; i += 16) {
        printf("%04X:", i);
        for (j = 0; j < 16; j++) printf(" %02X", densb(i + j));
        printf("\r\n");
    }
    /* host odd-vs-even mirror check over the same span */
    for (i = 0; i < 0x1000; i += 2)
        if (hostb(i) != hostb(i + 1)) { mirr = 0; if (firstbad == 0xFFFF) firstbad = i; }
    if (mirr) printf("host odd-mirrors-even over 0x000-0xFFF: YES\r\n");
    else      printf("host odd-mirrors-even: NO, first diff at host 0x%04X (%02X/%02X)\r\n",
                     firstbad, hostb(firstbad), hostb(firstbad + 1));
}

static void pass2_mirrors(void)
{
    static unsigned offs[8] = { 0x0000, 0x0800, 0x1000, 0x1800, 0x2000, 0x2800, 0x3000, 0x3800 };
    unsigned char base[16], smp[16];
    unsigned o, j;
    int same;
    printf("--- PASS 2: mirror map (16 host bytes sampled per offset) ---\r\n");
    for (j = 0; j < 16; j++) base[j] = hostb(j);
    for (o = 0; o < 8; o++) {
        same = 1;
        for (j = 0; j < 16; j++) { smp[j] = hostb(offs[o] + j); if (smp[j] != base[j]) same = 0; }
        printf("host +0x%04X: %s |", offs[o], same ? "== base " : "DIFFERS ");
        for (j = 0; j < 8; j++) printf(" %02X", smp[j]);
        printf("\r\n");
    }
}

static void pass3_config(void)
{
    unsigned a;
    unsigned char v1, v2;
    printf("--- PASS 3: config region, driven-vs-floating ---\r\n");
    printf("(read attr 0x000 [drives 0x01] then target; read attr 0x008 [drives 0x17] then target)\r\n");
    for (a = 0x200; a <= 0x23E; a += 2) {
        (void)hostb(0);            /* drive bus with 0x01 */
        v1 = hostb(a);
        (void)hostb(8);            /* drive bus with 0x17 */
        v2 = hostb(a);
        printf("attr 0x%03X: %02X %02X  %s\r\n", a, v1, v2,
               (v1 == 0x01 && v2 == 0x17) ? "FLOATING (bus hold)" :
               (v1 == v2)                 ? "driven"              : "MIXED?");
    }
}

static void pass4_tick(void)
{
    unsigned char s1[32], s2[32];
    unsigned a, i;
    int changed = 0;
    printf("--- PASS 4: tick check (config region, two reads ~50ms apart) ---\r\n");
    for (i = 0, a = 0x200; a <= 0x23E; a += 2, i++) s1[i] = hostb(a);
    dly(50000);
    for (i = 0, a = 0x200; a <= 0x23E; a += 2, i++) s2[i] = hostb(a);
    for (i = 0, a = 0x200; a <= 0x23E; a += 2, i++)
        if (s1[i] != s2[i]) { printf("attr 0x%03X: %02X -> %02X\r\n", a, s1[i], s2[i]); changed = 1; }
    if (!changed) printf("no bits changed\r\n");
}

static void pass5_io(void)
{
    unsigned p, r;
    printf("--- PASS 5: I/O reads (socket already I/O-configured) ---\r\n");
    for (r = 0; r < 3; r++) {
        printf("pass %u: 530-539:", r + 1);
        for (p = 0x530; p <= 0x539; p++) printf(" %02X", (unsigned char)inp(p));
        printf("  388-38B:");
        for (p = 0x388; p <= 0x38B; p++) printf(" %02X", (unsigned char)inp(p));
        printf("\r\n");
        dly(30000);
    }
}

int main(int argc, char **argv)
{
    int s, only = -1;
    unsigned seg;
    for (s = 1; s < argc; s++)
        if ((argv[s][0] == '/' || argv[s][0] == '-') &&
            (argv[s][1] == 'S' || argv[s][1] == 's'))
            only = atoi(argv[s] + 2 + (argv[s][2] == '='));

    printf("VEW2SCAN 1.0 - passive VEW212 ASIC recon (READS ONLY)\r\n");
    for (s = 0; s < 2; s++) {
        if (only >= 0 && s != only) continue;
        sockoff = s ? 0x40 : 0x00;
        if ((rd(0x01) & 0x0C) != 0x0C) { printf("socket %d: no card\r\n", s); continue; }
        if (!(rd(0x01) & 0x40)) {
            printf("socket %d: card present but UNPOWERED - passive tool will not power it.\r\n"
                   "  (run the enabler or VEWCIS first, then re-run)\r\n", s);
            continue;
        }
        seg = pickseg();
        if (!seg) { printf("socket %d: no clean window segment (D000/CC00 busy)\r\n", s); continue; }
        winseg = seg;
        if (!mapwin(seg)) { printf("socket %d: window map failed\r\n", s); continue; }
        printf("=== socket %d, window %04X, PCIC ifc=%02X ===\r\n", s, seg, rd(0x03));
        pass1_dump();
        pass2_mirrors();
        pass3_config();
        pass4_tick();
        if (rd(0x03) & 0x20) pass5_io();
        else printf("--- PASS 5 skipped: socket not I/O-configured (memory mode) ---\r\n");
        unmapwin();
    }
    return 0;
}

/* VEWCIS.C - CF-VEW211 CIS repair tool: heal the CIS and NOTHING else.
 * Copyright (c) 2026 zikolas. MIT License.
 *
 * The minimal companion to VEW21XGO: finds the card, and if its CIS reads
 * as a dead fill wall (failed EEPROM load), injects the known-good 256-byte
 * CIS image into the ASIC's shadow RAM - then stops. It does NOT write the
 * COR, map I/O, route an IRQ or touch the mixer. The card is left powered,
 * un-configured, and self-describing, ready for whatever CIS-reading stack
 * you actually want to use (Card Services, EZ-Play, the period vendor
 * drivers, DTPL...). Without /BURN the injection is volatile: it survives
 * warm reboots (the card keeps power) but not power-down or eject.
 *
 * /BURN makes the repair PERMANENT. The MEI ASIC's config register at
 * attribute 0x204 (undeclared in the CIS) is a whole-shadow EEPROM commit
 * strobe: pulsing bit0 writes the entire 256-byte shadow back to the
 * on-card 93LC56 (isolated empirically with probes/VEWSTRB.C, 2026-07-10;
 * pure strobe semantics - no write-through needed). /BURN power-cycles the
 * socket to learn the TRUE EEPROM state, injects if dead, pulses the
 * strobe, then power-cycles again and verifies the EEPROM reloads the
 * pristine image by itself.
 *
 * Usage: VEWCIS [/BURN] [/S=0..7] [/W=D000]
 *   /BURN   permanently program the repaired CIS into the card's EEPROM
 *           (verified across a power cycle; skips if EEPROM already healthy)
 *   /S=dec  socket 0..7 (default: auto-scan)
 *   /W=hex  attribute-window segment (default D000, auto-relocates if busy)
 *
 * Build (Open Watcom, 16-bit real mode):  C:\WATCOM\BLD VEWCIS
 */
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <i86.h>

#define VEWCIS_VER "1.1"
#define PCIC_BASE 0x3E0
#define MAX_SOCKET 7
#define VEW_MANF 0x0032
#define VEW_CARD 0x0001
#define STROBE_OFF 0x204                /* attr addr: EEPROM commit strobe, bit0 */

/* Known-good CF-VEW211 CIS (256 bytes), from a healthy unit's DTPL dump. */
static const unsigned char cis_img[256] = {
    0x01,0x02,0x00,0xFF,0x17,0x02,0xD1,0xFF,0x15,0x64,0x04,0x01,0x4D,0x61,0x74,0x73,
    0x75,0x73,0x68,0x69,0x74,0x61,0x20,0x45,0x6C,0x65,0x63,0x74,0x72,0x69,0x63,0x20,
    0x49,0x6E,0x64,0x75,0x73,0x74,0x72,0x69,0x61,0x6C,0x20,0x43,0x6F,0x2E,0x2C,0x20,
    0x4C,0x74,0x64,0x2E,0x00,0x50,0x61,0x6E,0x61,0x73,0x6F,0x6E,0x69,0x63,0x20,0x53,
    0x6F,0x75,0x6E,0x64,0x20,0x43,0x61,0x72,0x64,0x00,0x43,0x46,0x2D,0x56,0x45,0x57,
    0x32,0x31,0x31,0x00,0x56,0x65,0x72,0x73,0x69,0x6F,0x6E,0x20,0x31,0x2E,0x31,0x20,
    0x41,0x70,0x6C,0x2E,0x20,0x32,0x35,0x2C,0x31,0x39,0x39,0x34,0x00,0xFF,0x1A,0x05,
    0x01,0x23,0x00,0x02,0x03,0x1B,0x14,0xE0,0x81,0x9D,0x11,0x55,0x1E,0xFC,0x23,0xAC,
    0x61,0x30,0x05,0x09,0x88,0x03,0x03,0x30,0x80,0x0E,0x08,0x1B,0x0A,0x21,0x08,0xAC,
    0x61,0x80,0x0E,0x09,0x88,0x03,0x03,0x1B,0x0A,0x22,0x08,0xAC,0x61,0x40,0x0F,0x09,
    0x88,0x03,0x03,0x1B,0x0A,0x23,0x08,0xAC,0x61,0x04,0x06,0x09,0x88,0x03,0x03,0x20,
    0x04,0x32,0x00,0x01,0x00,0x21,0x02,0xFF,0x00,0x10,0x05,0x47,0xFF,0xB9,0x00,0xC9,
    0x14,0x00,0xFF,0xC3,0x50,0x72,0x6F,0x64,0x75,0x63,0x74,0x69,0x6F,0x6E,0x20,0x44,
    0x61,0x74,0x65,0x3A,0xCB,0x07,0x02,0x0D,0x01,0x54,0x69,0x6D,0x65,0x3A,0x14,0x20,
    0x30,0x2B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

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
static int is_vew(void){ return (g_manf == VEW_MANF && g_card == VEW_CARD) || g_cisdead; }

/* is host segment cseg..cseg+3 pages covered by an enabled window anywhere? */
static int mem_win_overlaps(unsigned cseg)
{
    unsigned cs = cseg >> 8, ce = (cseg >> 8) + 3;
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
                if (!(en & (1 << w))) continue;
                ws = ((unsigned)(rd(base+1) & 0x0F) << 8) | rd(base);
                we = ((unsigned)(rd(base+3) & 0x0F) << 8) | rd(base+2);
                if (cs <= we && ws <= ce) return 1;
            }
        }
    }
    return 0;
}
static unsigned find_free_window(unsigned want)
{
    static unsigned cand[] = { 0xD000, 0xCC00, 0xD400, 0xD800, 0xDC00,
                               0xE000, 0xE400, 0xE800, 0xC800, 0 };
    int i;
    if (!mem_win_overlaps(want)) return want;
    for (i = 0; cand[i]; i++) if (!mem_win_overlaps(cand[i])) return cand[i];
    return want;
}

static void map_attr(unsigned memseg)
{
    unsigned woff = ((unsigned)(0 - (memseg >> 8)) & 0x3FFF) | 0x4000;
    wr(0x10, (memseg >> 8) & 0xFF); wr(0x11, (memseg >> 8 >> 8) & 0x3F);
    wr(0x12, ((memseg >> 8) + 3) & 0xFF); wr(0x13, 0x00);
    wr(0x14, woff & 0xFF); wr(0x15, (woff >> 8) & 0xFF);
    wr(0x06, rd(0x06) | 0x01); iod(500);
}
/* full socket power cycle; leaves attribute window mapped */
static int power_cycle(unsigned memseg)
{
    unsigned long t;
    wr(0x06, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
    MS(800);
    wr(0x02, 0x95); MS(30);
    if (!(rd(0x01) & 0x40)) return 0;
    wr(0x03, 0x40); MS(10);
    for (t = 0; t < 600000UL; t++) if (rd(0x01) & 0x20) break;
    map_attr(memseg);
    return 1;
}

/* power + map + read CIS; 1 = our card (left powered+mapped) */
static int probe_socket(unsigned memseg)
{
    unsigned char s03, s06, s10, s11, s12, s13, s14, s15;
    int was_on;
    unsigned long t;
    if ((rd(0x01) & 0x0C) != 0x0C) return 0;
    was_on = (rd(0x01) & 0x40) != 0;
    s03 = rd(0x03); s06 = rd(0x06);
    s10 = rd(0x10); s11 = rd(0x11); s12 = rd(0x12);
    s13 = rd(0x13); s14 = rd(0x14); s15 = rd(0x15);
    if (!was_on) {
        wr(0x02, 0x95); MS(20);
        if (!(rd(0x01) & 0x40)) { wr(0x02, 0x00); return 0; }
    }
    wr(0x03, s03 & 0x20 ? s03 : 0x40); MS(10);   /* deassert reset; keep I/O mode if in use */
    for (t = 0; t < 600000UL; t++) if (rd(0x01) & 0x20) break;
    map_attr(memseg);
    read_cis(memseg);
    if (is_vew()) return 1;
    if (was_on) {
        wr(0x10, s10); wr(0x11, s11); wr(0x12, s12); wr(0x13, s13);
        wr(0x14, s14); wr(0x15, s15); wr(0x06, s06); wr(0x03, s03);
    } else {
        wr(0x06, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
    }
    return 0;
}

/* inject the good image into the shadow; 1 = readback verified */
static int inject(unsigned memseg)
{
    unsigned char __far *a = (unsigned char __far *)MK_FP(memseg, 0);
    int i;
    for (i = 0; i < 256; i++) a[i * 2] = cis_img[i];
    for (i = 0; i < 256; i++) if (a[i * 2] != cis_img[i]) return 0;
    return 1;
}
/* pulse the EEPROM commit strobe (attr 0x204 bit0), then let the burn finish */
static void strobe(unsigned memseg)
{
    unsigned char __far *a = (unsigned char __far *)MK_FP(memseg, 0);
    a[STROBE_OFF] = 0x01; iod(400);
    a[STROBE_OFF] = 0x00; iod(400);
    MS(3000);
}
/* full compare of the (freshly loaded) shadow against the image */
static int eeprom_ok(unsigned memseg)
{
    unsigned char __far *a = (unsigned char __far *)MK_FP(memseg, 0);
    int i;
    for (i = 0; i < 256; i++) if (a[i * 2] != cis_img[i]) return 0;
    return 1;
}

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
    unsigned sock = 0, memseg = 0xD000;
    int sgiven = 0, wgiven = 0, burn = 0, found = 0, any = 0, i;
    unsigned char fill;

    for (i = 1; i < argc; i++) {
        char *arg = argv[i], *vp;
        if (arg[0] != '/' && arg[0] != '-') continue;
        if      (sw(arg, "BURN", &vp)) burn = 1;
        else if (sw(arg, "S", &vp)) { if (vp) { sock = (unsigned)strtol(vp, 0, 10); sgiven = 1; } }
        else if (sw(arg, "W", &vp)) { if (vp) { memseg = (unsigned)strtol(vp, 0, 16); wgiven = 1; } }
        else printf("Ignoring unknown switch: %s\n", arg);
    }
    if (sgiven && sock > MAX_SOCKET) { printf("Bad /S=%u : 0..%u\n", sock, MAX_SOCKET); return 2; }
    if (!wgiven) {
        unsigned freeseg = find_free_window(memseg);
        if (freeseg != memseg) { printf("Window %04X busy; using %04X.\n", memseg, freeseg); memseg = freeseg; }
    }

    if (sgiven) {
        select_socket(sock);
        if (!controller_present()) { printf("No PCIC for socket %u\n", sock); return 1; }
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
        if (!any) { printf("No 82365-class PCIC found.\n"); return 1; }
    }
    if (!found) {
        if (g_manf) printf("Not a CF-VEW211 (found MANFID %04X/%04X \"%s\").\n", g_manf, g_card, g_vers);
        else        printf("No CF-VEW211 (or dead-CIS card) found.\n");
        return 3;
    }

    printf("VEWCIS %s - socket %u%s\n", VEWCIS_VER, sock, sgiven ? "" : " (auto)");

    if (burn) {
        /* learn the TRUE EEPROM state (a warm shadow can mask a dead EEPROM) */
        printf("   /BURN: power-cycling to read the true EEPROM state...\n");
        if (!power_cycle(memseg)) { printf("   power cycle failed\n"); return 1; }
        read_cis(memseg);
        if (!g_cisdead && g_manf == VEW_MANF && g_card == VEW_CARD) {
            printf("   EEPROM already healthy (\"%s\") - nothing to burn.\n", g_vers);
            wr(0x06, rd(0x06) & ~0x01);
            return 0;
        }
        fill = g_cisfill;
        if (!inject(memseg)) { printf("   shadow injection FAILED - aborting.\n"); return 4; }
        printf("   shadow healed (was %s%02X) - pulsing commit strobe (attr 204.0)...\n",
               g_cisdead ? "dead fill " : "corrupt, first byte ", fill);
        strobe(memseg);
        if (!power_cycle(memseg)) { printf("   power cycle failed\n"); return 1; }
        read_cis(memseg);
        wr(0x06, rd(0x06) & ~0x01);
        if (eeprom_ok(memseg) || (!g_cisdead && g_manf == VEW_MANF && g_card == VEW_CARD)) {
            printf("   EEPROM PERMANENTLY REPAIRED + verified across power cycle.\n");
            printf("   Card self-describes from cold: \"%s\"\n", g_vers);
            return 0;
        }
        printf("   burn did NOT verify (CIS %s) - card still usable via injection.\n",
               g_cisdead ? "dead" : "unexpected");
        return 4;
    }

    if (!g_cisdead) {
        printf("   CIS already valid: \"%s\" - nothing to do.\n", g_vers);
        wr(0x06, rd(0x06) & ~0x01);
        return 0;
    }
    fill = g_cisfill;
    if (inject(memseg)) {
        read_cis(memseg);
        wr(0x06, rd(0x06) & ~0x01);
        printf("   CIS was dead (stuck %02X fill) - injected + verified.\n", fill);
        printf("   Card now self-describing: \"%s\"\n", g_vers);
        printf("   (volatile: re-run after power-down/eject, or use /BURN to make it permanent)\n");
        return 0;
    }
    wr(0x06, rd(0x06) & ~0x01);
    printf("   Injection FAILED verify (fill was %02X) - shadow not writable?\n", fill);
    return 4;
}

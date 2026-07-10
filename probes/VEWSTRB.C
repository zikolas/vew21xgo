/* VEWSTRB.C - CF-VEW211: find the MEI ASIC's commit-shadow-to-EEPROM strobe.
 *
 * Background: the VEWVND register sweeps (2026-07-09/10), performed while
 * the known-good CIS was resident in the shadow RAM, permanently programmed
 * the card's 93LC56 - proving an EEPROM write path exists among the
 * undocumented controls. This probe isolates WHICH one, safely:
 *
 *   per candidate: power-cycle & verify EEPROM image -> plant a tracer
 *   byte at shadow offset 0xFE (past the END tuple, invisible to parsers,
 *   value 0xA0+candidate so a hit self-identifies) -> exercise the
 *   candidate (set bit, rewrite tracer while set, clear bit - covers both
 *   write-through and strobe semantics) -> settle 3s (EEPROM burn time;
 *   never cut power mid-write) -> power-cycle -> tracer survived = FOUND.
 *
 * Safety: the shadow always holds the good CIS during testing, so firing
 * the strobe only ever burns a good image (+tracer). On a hit the probe
 * stops, clears the tracer, re-burns pristine content with the found
 * strobe, and verifies. Any unexpected shadow content aborts the run.
 *
 * Requires explicit /GO to run (prints this plan otherwise).
 * Build: C:\WATCOM\BLD VEWSTRB
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <i86.h>

#define PCIC   0x3E0
#define MEMSEG 0xD000
#define TRACER_OFF 0xFE                          /* image byte, past END tuple */

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

static unsigned pidx = PCIC, soff = 0;
static unsigned char rd(unsigned r){ outp(pidx, soff + r); return (unsigned char)inp(pidx + 1); }
static void          wr(unsigned r, unsigned v){ outp(pidx, soff + r); outp(pidx + 1, v); }
static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)
static unsigned char __far *AW;

/* candidates: attribute-register single bits + any-value port writes */
static struct { int is_port; unsigned off; unsigned char bit; const char *name; } cand[] = {
    { 0, 0x204, 0x01, "attr 204 bit0" },
    { 0, 0x206, 0x02, "attr 206 bit1" }, { 0, 0x206, 0x04, "attr 206 bit2" },
    { 0, 0x206, 0x08, "attr 206 bit3" }, { 0, 0x206, 0x10, "attr 206 bit4" },
    { 0, 0x206, 0x20, "attr 206 bit5" }, { 0, 0x206, 0x40, "attr 206 bit6" },
    { 0, 0x206, 0x80, "attr 206 bit7" },
    { 0, 0x208, 0x01, "attr 208 bit0" }, { 0, 0x208, 0x02, "attr 208 bit1" },
    { 0, 0x208, 0x04, "attr 208 bit2" }, { 0, 0x208, 0x08, "attr 208 bit3" },
    { 1, 0x530, 0xFF, "port 530 write" }, { 1, 0x531, 0xFF, "port 531 write" },
    { 1, 0x532, 0xFF, "port 532 write" }, { 1, 0x533, 0xFF, "port 533 write" }
};
#define NCAND (sizeof(cand)/sizeof(cand[0]))

static void map_attr(void)
{
    unsigned woff = ((unsigned)(0 - (MEMSEG >> 8)) & 0x3FFF) | 0x4000;
    wr(0x10, (MEMSEG >> 8) & 0xFF); wr(0x11, 0x00);
    wr(0x12, ((MEMSEG >> 8) + 3) & 0xFF); wr(0x13, 0x00);
    wr(0x14, woff & 0xFF); wr(0x15, (woff >> 8) & 0xFF);
    wr(0x06, 0x01); iod(500);
}
/* full power cycle; returns 1 when card is back up with attr mapped */
static int power_cycle(void)
{
    unsigned long t;
    wr(0x06, 0x00); wr(0x03, 0x00); wr(0x02, 0x00);
    MS(800);
    wr(0x02, 0x95); MS(30);
    if (!(rd(0x01) & 0x40)) return 0;
    wr(0x03, 0x40); MS(10);
    for (t = 0; t < 600000UL; t++) if (rd(0x01) & 0x20) break;
    map_attr();
    return 1;
}
/* compare shadow against image; tolerate (and return) the tracer byte */
static int shadow_check(unsigned char *tracer)
{
    unsigned i; int bad = 0;
    *tracer = AW[TRACER_OFF * 2];
    for (i = 0; i < 256; i++) {
        if (i == TRACER_OFF) continue;
        if (AW[i * 2] != cis_img[i]) { bad++; if (bad < 4)
            printf("    MISMATCH at %02X: %02X != %02X\n", i, AW[i*2], cis_img[i]); }
    }
    return bad == 0;
}
/* configure card (COR idx 0x20) so the I/O ports exist, for port candidates */
static void config_io(void)
{
    AW[0x200] = 0x20; MS(5);
    wr(0x08, 0x30); wr(0x09, 0x05); wr(0x0A, 0x39); wr(0x0B, 0x05);
    wr(0x07, 0x00); wr(0x03, 0x60); wr(0x06, 0xC1); iod(500);
}

int main(int argc, char **argv)
{
    int i, go = 0, found = -1;
    unsigned char tr;

    for (i = 1; i < argc; i++)
        if (argv[i][0]=='/' && (argv[i][1]=='G'||argv[i][1]=='g')) go = 1;
    if (!go) {
        printf("VEWSTRB - EEPROM commit-strobe finder for the CF-VEW211.\n\n");
        printf("Per candidate (%u total): power-cycle + verify EEPROM image, plant a\n", (unsigned)NCAND);
        printf("tracer byte at shadow offset FE, exercise the candidate (write-through\n");
        printf("AND strobe semantics), settle 3s, power-cycle: tracer survived = found.\n");
        printf("Shadow always holds the good CIS, so a hit only re-burns a good image.\n");
        printf("On a hit: stops, clears tracer, re-burns pristine, verifies.\n\n");
        printf("Run with /GO to start.\n");
        return 0;
    }

    pidx = PCIC; soff = 0;
    AW = (unsigned char __far *)MK_FP(MEMSEG, 0);
    if ((rd(0x00) & 0xC0) != 0x80) { printf("no 82365\n"); return 1; }
    if ((rd(0x01) & 0x0C) != 0x0C) { printf("no card\n"); return 1; }

    printf("VEWSTRB: pre-flight - power cycle + verify EEPROM content...\n");
    if (!power_cycle()) { printf("no power\n"); return 1; }
    if (!shadow_check(&tr) || tr != cis_img[TRACER_OFF]) {
        printf("ABORT: EEPROM content is not the pristine image (tracer byte=%02X).\n", tr);
        return 2;
    }
    printf("pre-flight ok - EEPROM pristine. Testing %u candidates:\n", (unsigned)NCAND);

    for (i = 0; i < (int)NCAND; i++) {
        unsigned char mark = (unsigned char)(0xA0 + i);
        printf("  [%2d/%2u] %s: ", i + 1, (unsigned)NCAND, cand[i].name);
        AW[TRACER_OFF * 2] = mark; iod(400);               /* plant tracer */
        if (cand[i].is_port) {
            config_io();                                    /* ports need a configured card */
            outp(cand[i].off, 0xFF); iod(400);
            AW[TRACER_OFF * 2] = mark; iod(400);            /* rewrite while 'active' */
            outp(cand[i].off, 0x00); iod(400);
        } else {
            AW[cand[i].off] = cand[i].bit; iod(400);        /* set */
            AW[TRACER_OFF * 2] = mark; iod(400);            /* rewrite while set */
            AW[cand[i].off] = 0x00; iod(400);               /* clear */
        }
        MS(3000);                                           /* let any burn finish */
        if (!power_cycle()) { printf("power fail\n"); return 1; }
        if (!shadow_check(&tr)) { printf("ABORT: unexpected shadow content.\n"); return 2; }
        if (tr == mark) { printf("*** HIT - EEPROM committed! ***\n"); found = i; break; }
        printf("no (tracer=%02X)\n", tr);
    }

    if (found < 0) {
        printf("\nNo single candidate committed. (Combination or timing-dependent?)\n");
        return 3;
    }

    /* restore pristine image using the found strobe */
    printf("\nrestoring pristine image via '%s'...\n", cand[found].name);
    AW[TRACER_OFF * 2] = cis_img[TRACER_OFF]; iod(400);
    if (cand[found].is_port) {
        config_io();
        outp(cand[found].off, 0xFF); iod(400);
        AW[TRACER_OFF * 2] = cis_img[TRACER_OFF]; iod(400);
        outp(cand[found].off, 0x00); iod(400);
    } else {
        AW[cand[found].off] = cand[found].bit; iod(400);
        AW[TRACER_OFF * 2] = cis_img[TRACER_OFF]; iod(400);
        AW[cand[found].off] = 0x00; iod(400);
    }
    MS(3000);
    if (!power_cycle()) { printf("power fail on restore\n"); return 1; }
    if (shadow_check(&tr) && tr == cis_img[TRACER_OFF])
        printf("EEPROM restored + verified pristine.\n");
    else
        printf("WARNING: restore verify failed (tracer=%02X) - run VEW21XGO and retry.\n", tr);

    printf("\n>>> EEPROM commit strobe = %s <<<\n", cand[found].name);
    printf("(exercised as: set/write -> rewrite shadow byte -> clear; refine manually\n");
    printf(" if needed to isolate which edge/phase commits.)\n");
    return 0;
}

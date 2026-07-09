/* VEWASIC.C - CF-VEW211: map everything software-visible in the MEI ASIC
 * (DA65646, IC6). Systematic, mostly-read probe; every write is done with
 * save/restore and a codec-liveness check afterwards. Prints a structured
 * report (redirect to a file: VEWASIC > ASIC.TXT).
 *
 * Sections:
 *   1. attribute-space decode map (coarse, 0..64KB, mirrors identified)
 *   2. CIS shadow write extent (which attribute bytes accept writes)
 *   3. config registers 0x200-0x21E: read + per-bit write/latch tests
 *   4. ASIC I/O ports 0x530-0x533 and 0x538-0x539: read + latch tests
 *   5. COR index walk: which config indexes decode a live codec
 *   6. common-memory scan (0..256KB coarse): hunting the 32K SRAM
 *   7. wide I/O decode scan 0x500-0x53F: the ASIC's I/O footprint
 *
 * Leaves the card powered with COR=0x20, windows 0x530+0x388 mapped, and
 * the CIS shadow in whatever state section 2 left it (dead-fill pattern
 * plus restored markers) - run VEW21XGO afterwards to re-heal + re-enable.
 *
 * Build: C:\WATCOM\BLD VEWASIC
 */
#include <stdio.h>
#include <conio.h>
#include <i86.h>

#define PCIC 0x3E0
#define MEMSEG 0xD000
static unsigned pidx = PCIC, soff = 0;
static unsigned char rd(unsigned r){ outp(pidx, soff + r); return (unsigned char)inp(pidx + 1); }
static void          wr(unsigned r, unsigned v){ outp(pidx, soff + r); outp(pidx + 1, v); }
static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)

static unsigned char __far *AW;                 /* attribute window base */

/* point mem window 0 at 16KB of card address space; reg=1 attribute, 0 common */
static void map_window(unsigned long card_base, int reg)
{
    unsigned woff = (unsigned)((((card_base >> 12) - (MEMSEG >> 8)) & 0x3FFF)
                               | (reg ? 0x4000 : 0));
    wr(0x06, rd(0x06) & ~0x01);
    wr(0x10, (MEMSEG >> 8) & 0xFF); wr(0x11, 0x00);
    wr(0x12, ((MEMSEG >> 8) + 3) & 0xFF); wr(0x13, 0x00);
    wr(0x14, woff & 0xFF); wr(0x15, (woff >> 8) & 0xFF);
    wr(0x06, rd(0x06) | 0x01); iod(500);
}

/* classify 256 bytes of even-address window content */
static void classify(unsigned off, unsigned char *fill, int *isconst, unsigned *sum)
{
    unsigned i; unsigned char b0 = AW[off];
    *isconst = 1; *sum = 0;
    for (i = 0; i < 256; i++) {
        unsigned char b = AW[off + i * 2];
        *sum += b;
        if (b != b0) *isconst = 0;
    }
    *fill = b0;
}

static int codec_alive(void){ return (unsigned char)inp(0x534) != 0xFF; }

/* write-test one even attribute byte: returns 1 if the value latched */
static int attr_latch(unsigned off)
{
    unsigned char sv = AW[off], got;
    AW[off] = (unsigned char)(sv ^ 0xFF); iod(200);
    got = AW[off];
    AW[off] = sv; iod(200);
    return got == (unsigned char)(sv ^ 0xFF);
}

/* per-bit latch test on a config register (restores original) */
static void reg_bit_test(unsigned off, const char *name)
{
    unsigned char sv = AW[off], latched = 0, b;
    int i;
    for (i = 0; i < 8; i++) {
        b = (unsigned char)(1 << i);
        AW[off] = b; iod(400);
        if ((AW[off] & b) == b) latched |= b;
    }
    AW[off] = sv; iod(400);
    printf("  %s @%03X: reset=%02X  bits that latch=%02X  codec after=%s\n",
           name, off, sv, latched, codec_alive() ? "ok" : "DEAD");
}

int main(void)
{
    unsigned i, k;
    unsigned char v, sv;
    unsigned long base;

    pidx = PCIC; soff = 0;
    AW = (unsigned char __far *)MK_FP(MEMSEG, 0);
    if ((rd(0x00) & 0xC0) != 0x80) { printf("no 82365\n"); return 1; }
    if ((rd(0x01) & 0x0C) != 0x0C) { printf("no card\n"); return 1; }

    printf("VEWASIC - CF-VEW211 MEI ASIC (DA65646) software-visible map\n");
    printf("============================================================\n");

    /* clean power-up so we see reset-state values */
    wr(0x02, 0x00); MS(150);
    wr(0x02, 0x95); MS(30);
    if (!(rd(0x01) & 0x40)) { printf("no power\n"); return 1; }
    wr(0x03, 0x40); MS(10);
    { unsigned long t; for (t = 0; t < 600000UL; t++) if (rd(0x01) & 0x20) break; }
    printf("power-up: IF status=%02X (READY=%d)\n\n", rd(0x01), (rd(0x01) >> 5) & 1);

    /* ---- 1. attribute space coarse map, 0..64KB ---- */
    printf("[1] attribute space map (256-byte blocks, even addresses):\n");
    for (base = 0; base < 0x10000UL; base += 0x4000UL) {
        map_window(base, 1);
        for (k = 0; k < 32; k++) {              /* 32 x 512 host = 16KB */
            unsigned char fill; int c; unsigned sum;
            classify(k * 512, &fill, &c, &sum);
            if (c) { if (k == 0 || fill != AW[(k-1)*512] ) /* only print transitions */
                       printf("  attr %05lX: const fill %02X\n", base + k * 512UL, fill); }
            else     printf("  attr %05lX: DATA (sum=%04X, first=%02X %02X %02X %02X)\n",
                            base + k * 512UL, sum,
                            AW[k*512], AW[k*512+2], AW[k*512+4], AW[k*512+6]);
        }
    }

    /* ---- 2. shadow write extent ---- */
    map_window(0, 1);
    printf("\n[2] attribute write-latch extent (even addresses):\n  latches:");
    {
        static unsigned probe[] = { 0x000, 0x040, 0x080, 0x0C0, 0x100, 0x140,
                                    0x180, 0x1C0, 0x1FE, 0x210, 0x220, 0x240,
                                    0x280, 0x300, 0x3FE, 0x400, 0x500, 0x7FE,
                                    0x800, 0xFFE, 0 };
        for (i = 0; probe[i] || i == 0; i++) {
            if (i && !probe[i]) break;
            printf(" %03X:%c", probe[i], attr_latch(probe[i]) ? 'Y' : 'n');
        }
        printf("\n");
    }

    /* ---- 3. config registers ---- */
    printf("\n[3] config register file 0x200-0x21E (even bytes, reset state):\n  ");
    for (i = 0; i < 16; i++) printf("%02X ", AW[0x200 + i * 2]);
    printf("\n");
    reg_bit_test(0x200, "COR ");
    reg_bit_test(0x202, "CCSR");
    reg_bit_test(0x204, "+204");
    reg_bit_test(0x206, "+206");
    reg_bit_test(0x208, "+208");
    reg_bit_test(0x20A, "+20A");
    reg_bit_test(0x20C, "+20C");
    reg_bit_test(0x20E, "+20E");
    reg_bit_test(0x210, "+210");

    /* ---- 4/5. configure card, then probe ASIC I/O + index walk ---- */
    AW[0x200] = 0x20; MS(5);
    wr(0x08, 0x30); wr(0x09, 0x05); wr(0x0A, 0x39); wr(0x0B, 0x05);
    wr(0x0C, 0x88); wr(0x0D, 0x03); wr(0x0E, 0x8B); wr(0x0F, 0x03);
    wr(0x07, 0x00); wr(0x03, 0x60);
    wr(0x06, (rd(0x06) & 0x01) | 0xC0 | 0x01); iod(500);

    printf("\n[4] I/O ports at idx 0x20 (base 0x530):\n");
    printf("  read: 530=%02X 531=%02X 532=%02X 533=%02X  [codec 534-537: %02X %02X %02X %02X]  538=%02X 539=%02X\n",
           (unsigned char)inp(0x530), (unsigned char)inp(0x531),
           (unsigned char)inp(0x532), (unsigned char)inp(0x533),
           (unsigned char)inp(0x534), (unsigned char)inp(0x535),
           (unsigned char)inp(0x536), (unsigned char)inp(0x537),
           (unsigned char)inp(0x538), (unsigned char)inp(0x539));
    {
        static unsigned p[] = { 0x530, 0x531, 0x532, 0x533, 0x538, 0x539 };
        for (i = 0; i < 6; i++) {
            unsigned char lat = 0; int b;
            sv = (unsigned char)inp(p[i]);
            for (b = 0; b < 8; b++) {
                outp(p[i], 1 << b); iod(300);
                if ((unsigned char)inp(p[i]) & (1 << b)) lat |= (unsigned char)(1 << b);
            }
            outp(p[i], sv); iod(300);
            printf("  port %03X: reset=%02X bits-latch=%02X codec=%s\n",
                   p[i], sv, lat, codec_alive() ? "ok" : "DEAD");
        }
    }

    printf("\n[5] COR index walk (codec IAR at expected base; FF = no decode):\n");
    {
        static struct { unsigned char idx; unsigned base; } w[] = {
            { 0x00, 0x530 }, { 0x01, 0x530 }, { 0x10, 0x530 }, { 0x20, 0x530 },
            { 0x21, 0xE80 }, { 0x22, 0xF40 }, { 0x23, 0x604 }, { 0x24, 0x530 },
            { 0x2F, 0x530 }, { 0x3F, 0x530 }
        };
        for (i = 0; i < 10; i++) {
            AW[0x200] = w[i].idx; MS(5);
            wr(0x08, w[i].base & 0xFF); wr(0x09, (w[i].base >> 8) & 0xFF);
            wr(0x0A, (w[i].base + 9) & 0xFF); wr(0x0B, ((w[i].base + 9) >> 8) & 0xFF);
            iod(500);
            printf("  idx %02X -> base %03X: +4=%02X +6=%02X  COR readback=%02X\n",
                   w[i].idx, w[i].base,
                   (unsigned char)inp(w[i].base + 4), (unsigned char)inp(w[i].base + 6),
                   AW[0x200]);
        }
        AW[0x200] = 0x20; MS(5);                 /* restore */
        wr(0x08, 0x30); wr(0x09, 0x05); wr(0x0A, 0x39); wr(0x0B, 0x05);
    }

    /* ---- 6. common memory scan ---- */
    printf("\n[6] common memory scan (16KB blocks, first 256KB):\n");
    for (base = 0; base < 0x40000UL; base += 0x4000UL) {
        unsigned char b0, allsame = 1, w0, wg;
        map_window(base, 0);
        b0 = AW[0];
        for (i = 1; i < 64; i++) if (AW[i] != b0) { allsame = 0; break; }
        /* RAM test on first byte: save, invert, readback, restore */
        w0 = AW[0]; AW[0] = (unsigned char)(w0 ^ 0xFF); iod(200);
        wg = AW[0]; AW[0] = w0; iod(200);
        printf("  common %05lX: %s(first=%02X)  write-latch=%s\n",
               base, allsame ? "const " : "DATA! ", b0,
               (wg == (unsigned char)(w0 ^ 0xFF)) ? "YES" : "no");
    }
    map_window(0, 1);                            /* back to attribute */

    /* ---- 7. wide I/O decode 0x500-0x53F ---- */
    printf("\n[7] I/O decode footprint 0x500-0x53F (window widened):\n");
    wr(0x08, 0x00); wr(0x09, 0x05); wr(0x0A, 0x3F); wr(0x0B, 0x05); iod(500);
    for (i = 0; i < 0x40; i += 16) {
        printf("  %03X:", 0x500 + i);
        for (k = 0; k < 16; k++) printf(" %02X", (unsigned char)inp(0x500 + i + k));
        printf("\n");
    }
    wr(0x08, 0x30); wr(0x09, 0x05); wr(0x0A, 0x39); wr(0x0B, 0x05);

    printf("\ndone - run VEW21XGO to re-heal the CIS + restore normal config.\n");
    return 0;
}

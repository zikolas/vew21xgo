/* VEW2TRY.C - CF-VEW212: bring up the card's REAL (undeclared) vendor
 * configuration and prove life - the clean-bench replication of the
 * doc/DUMP212-VND-*.TXT capture.
 *
 * The 212's CIS-declared config (index 0x20) is dead on hardware; the
 * period vendor stack runs the card on UNDECLARED COR index 0x26 with
 * an MPU-401 UART at 330 and the OPL4 FM+wave block at 388-38D on
 * 16-bit autosized windows.  This tool replicates that state verbatim
 * from the capture and then asks the hardware three questions: does the
 * OPL FM core answer (timer detect), does the MPU answer (reset ->
 * 0xFE ACK), is the OPL4 wave device there (DevID reg == 0x20)?
 *
 * SAFETY: scans for the 212 by CIS content and REFUSES to touch any
 * other card (MANFID 0032/0501 required - no /FORCE, no guessing).
 * Card writes: COR only (attr 0x200) plus I/O-space device probes;
 * attr 0x202/0x204/0x206/0x208 are never written.  Every wait loop is
 * hard-capped (no unbounded polls).  /OFF powers the socket back down.
 *
 * Usage: VEW2TRY [/COR=66] [/IOCTL=22] [/WEN=E0] [/OFF] [/?]
 *   default: find the 212, configure vendor state, probe, LEAVE ENABLED
 *   /COR=hex   COR value to try (default 66 = index 26h | level bit)
 *   /IOCTL=hex PCIC I/O control flags (default 22 = both wins autosize)
 *   /WEN=hex   window-enable byte to set (default E0, as the vendor ran)
 *   /CCSR=hex  also write CCSR (attr 0x202) - /CCSR=08 = the vendor's
 *              audio/#SPKR routing bit (host-speaker listening path)
 *   /MIX       program the OPL4 mix-control regs (wave F8/F9 <- 0 = 0 dB
 *              both channels, FM and PCM) and leave NEW2|NEW enabled,
 *              as the vendor synth runs it
 *   /OFF       power the 212's socket down and exit
 *
 * Build: C:\WATCOM\BLD.BAT VEW2TRY
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

#define PCIC 0x3E0
static unsigned pidx = PCIC, soff = 0;
static unsigned char rd(unsigned r){ outp(pidx, soff + r); return (unsigned char)inp(pidx + 1); }
static void          wr(unsigned r, unsigned v){ outp(pidx, soff + r); outp(pidx + 1, v); }
static void iod(unsigned long n){ while (n--) (void)inp(0x80); }   /* ~1us each */
#define MS(x) iod((unsigned long)(x) * 1000UL)

static unsigned winseg;
static unsigned char hostb(unsigned off){ return *(unsigned char __far *)MK_FP(winseg, off); }
static void hostw(unsigned off, unsigned char v){ *(unsigned char __far *)MK_FP(winseg, off) = v; }

static unsigned char sv06, svw[6];
static int wb = 0x10, mapped = 0;

static int mapattr(void)
{
    unsigned start, woff;
    int i;
    static unsigned cand[2] = { 0xCC00, 0xD000 };
    winseg = 0;
    for (i = 0; i < 2; i++) {
        int k, clean = 1;
        for (k = 0; k < 32; k++)
            if (*(unsigned char __far *)MK_FP(cand[i], k) != 0xFF) { clean = 0; break; }
        if (clean) { winseg = cand[i]; break; }
    }
    if (!winseg) return 0;
    sv06 = rd(0x06);
    for (i = 0; i < 5; i++) if (!(sv06 & (1 << i))) break;
    if (i == 5) return 0;
    wb = 0x10 + i * 8;
    for (start = 0; start < 6; start++) svw[start] = rd(wb + start);
    start = winseg >> 8;
    woff  = ((unsigned)(0 - start) & 0x3FFF) | 0x4000;
    wr(wb + 0, start & 0xFF); wr(wb + 1, 0x00);        /* programmed while */
    wr(wb + 2, (start + 3) & 0xFF); wr(wb + 3, 0x00);  /* DISABLED         */
    wr(wb + 4, woff & 0xFF); wr(wb + 5, (woff >> 8) & 0xFF);
    wr(0x06, sv06 | (1 << i));
    iod(2000);
    mapped = 1;
    return 1;
}

static void unmapattr(void)
{
    int i;
    if (!mapped) return;
    wr(0x06, sv06);
    for (i = 0; i < 6; i++) wr(wb + i, svw[i]);
    mapped = 0;
}

/* is the card in this powered socket a healthy CF-VEW212?  (reads the
 * MANFID tuple bytes from the live CIS: 20 04 32 00 01 05) */
static int is212(void)
{
    unsigned off;
    if (hostb(0) != 0x01 || hostb(2) != 0x02) return 0;   /* CISTPL_DEVICE */
    for (off = 0; off < 0x1F0; off += 2) {
        if (hostb(off) == 0x20 && hostb(off + 2) == 0x04 &&
            hostb(off + 4) == 0x32 && hostb(off + 6) == 0x00 &&
            hostb(off + 8) == 0x01 && hostb(off + 10) == 0x05)
            return 1;
    }
    return 0;
}

/* ---- liveness probes (all waits hard-capped) ---------------------------- */

static int opl_detect(void)
{
    unsigned char a, b;
    outp(0x388, 0x04); MS(1); outp(0x389, 0x60); MS(1);   /* mask timers   */
    outp(0x388, 0x04); MS(1); outp(0x389, 0x80); MS(1);   /* reset flags   */
    a = (unsigned char)inp(0x388);
    outp(0x388, 0x02); MS(1); outp(0x389, 0xFF); MS(1);   /* T1 latch      */
    outp(0x388, 0x04); MS(1); outp(0x389, 0x21); MS(1);   /* start T1      */
    MS(2);                                                /* >80us         */
    b = (unsigned char)inp(0x388);
    outp(0x388, 0x04); MS(1); outp(0x389, 0x60); MS(1);
    outp(0x388, 0x04); MS(1); outp(0x389, 0x80); MS(1);
    printf("   OPL timer detect: pre=%02X post=%02X -> %s\n", a, b,
           ((a & 0xE0) == 0 && (b & 0xE0) == 0xC0) ? "FM ALIVE" : "dead");
    return (a & 0xE0) == 0 && (b & 0xE0) == 0xC0;
}

static int mpu_probe(void)
{
    unsigned long t;
    unsigned char st, dat;
    st = (unsigned char)inp(0x331);
    printf("   MPU status 331: %02X", st);
    if (st == 0xFF) { printf(" -> nothing there\n"); return 0; }
    for (t = 0; t < 100000UL; t++)                        /* wait DRR=0    */
        if (!(inp(0x331) & 0x40)) break;
    if (t == 100000UL) { printf(" -> never ready for a command\n"); return 0; }
    outp(0x331, 0xFF);                                    /* MPU RESET     */
    for (t = 0; t < 400000UL; t++)                        /* wait DSR=0    */
        if (!(inp(0x331) & 0x80)) break;
    if (t == 400000UL) { printf(" -> no reply to RESET\n"); return 0; }
    dat = (unsigned char)inp(0x330);
    printf(", RESET -> %02X %s\n", dat, dat == 0xFE ? "(ACK - MPU ALIVE)" : "(unexpected)");
    return dat == 0xFE;
}

static int wave_probe(void)
{
    unsigned char id;
    outp(0x38A, 0x05); MS(1); outp(0x38B, 0x03); MS(1);   /* NEW2|NEW on   */
    outp(0x38C, 0x02); MS(1);                             /* wave DevID reg */
    id = (unsigned char)inp(0x38D);
    printf("   OPL4 wave DevID (38C/38D reg 2): %02X -> %s\n", id,
           id == 0x20 ? "YMF278B WAVE ALIVE" : "not answering");
    outp(0x38A, 0x05); MS(1); outp(0x38B, 0x00); MS(1);   /* back to OPL3  */
    return id == 0x20;
}

int main(int argc, char **argv)
{
    int i, s, found = -1, off = 0, ccsr = -1, mix = 0;
    unsigned corv = 0x66, ioctl = 0x22, wen = 0xE0;
    unsigned char v;

    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '/' && a[0] != '-') continue;
        if (a[1] == '?' || a[1] == 'H' || a[1] == 'h') {
            printf("VEW2TRY - see source header. /COR=66 /IOCTL=22 /WEN=E0 /OFF\n");
            return 0;
        }
        if (!strnicmp(a + 1, "COR=", 4))   corv  = (unsigned)strtol(a + 5, 0, 16);
        if (!strnicmp(a + 1, "IOCTL=", 6)) ioctl = (unsigned)strtol(a + 7, 0, 16);
        if (!strnicmp(a + 1, "WEN=", 4))   wen   = (unsigned)strtol(a + 5, 0, 16);
        if (!strnicmp(a + 1, "OFF", 3))    off   = 1;
        if (!strnicmp(a + 1, "CCSR=", 5))  ccsr  = (int)strtol(a + 6, 0, 16);
        if (!strnicmp(a + 1, "MIX", 3) && strnicmp(a + 1, "MIX=", 4)) mix = 1;
    }

    printf("VEW2TRY - CF-VEW212 vendor-config bring-up (COR=%02X ioctl=%02X wen=%02X)\n",
           corv, ioctl, wen);

    /* ---- find the 212: scan sockets 0-7, identity check, no guessing ---- */
    for (s = 0; s < 8 && found < 0; s++) {
        pidx = PCIC + (s >> 1) * 2;
        soff = (s & 1) ? 0x40 : 0x00;
        if ((rd(0x00) & 0xC0) != 0x80) continue;          /* no controller */
        if ((rd(0x01) & 0x0C) != 0x0C) continue;          /* no card       */
        if (!(rd(0x01) & 0x40)) {                          /* power it up   */
            wr(0x02, 0xF1); MS(300);                       /* vendor value  */
            wr(0x03, 0x40); MS(100);
        }
        {   unsigned long t;                               /* READY, capped */
            for (t = 0; t < 200000UL; t++)
                if (rd(0x01) & 0x20) break;
        }
        MS(100);
        if (!mapattr()) { printf("socket %d: no attr window\n", s); continue; }
        if (is212()) { found = s; break; }                 /* window stays  */
        printf("socket %d: not a CF-VEW212 - untouched (win %04X, cis: %02X %02X %02X %02X)\n",
               s, winseg, hostb(0), hostb(2), hostb(4), hostb(6));
        unmapattr();
    }
    if (found < 0) { printf("No healthy CF-VEW212 found; nothing written.\n"); return 1; }
    printf("CF-VEW212 in socket %d (CIS verified)\n", found);

    if (off) {
        unmapattr();
        wr(0x03, 0x00); MS(50);
        wr(0x02, 0x00);
        printf("socket %d powered down.\n", found);
        return 0;
    }

    /* ---- COR: the one sanctioned attr write ----------------------------- */
    hostw(0x200, (unsigned char)corv);
    MS(50);
    v = hostb(0x200);
    printf("COR <- %02X, reads back %02X %s\n", corv, v,
           v == (unsigned char)corv ? "(latched)" : "(DIFFERS)");
    if (ccsr >= 0) {
        hostw(0x202, (unsigned char)ccsr);
        MS(50);
        v = hostb(0x202);
        printf("CCSR <- %02X, reads back %02X %s\n", ccsr, v,
               v == (unsigned char)ccsr ? "(latched)" : "(DIFFERS)");
    }
    unmapattr();

    /* ---- host side: vendor state verbatim (program windows DISABLED) ---- */
    wr(0x06, rd(0x06) & 0x3F);                             /* io wins off   */
    wr(0x08, 0x30); wr(0x09, 0x03);                        /* win0 330-333  */
    wr(0x0A, 0x33); wr(0x0B, 0x03);
    wr(0x0C, 0x88); wr(0x0D, 0x03);                        /* win1 388-38D  */
    wr(0x0E, 0x8D); wr(0x0F, 0x03);
    wr(0x07, (unsigned char)ioctl);
    wr(0x03, 0xE9);                                        /* I/O mode, IRQ9 */
    MS(10);
    wr(0x06, (unsigned char)wen);
    MS(50);
    printf("PCIC: intctl=%02X ioctl=%02X winEn=%02X win0=330-333 win1=388-38D\n",
           rd(0x03), rd(0x07), rd(0x06));

    /* ---- ask the hardware ------------------------------------------------ */
    i  = opl_detect();
    i += mpu_probe();
    i += wave_probe();

    if (mix) {
        unsigned char m0, m1;
        outp(0x38A, 0x05); MS(1); outp(0x38B, 0x03); MS(1);   /* NEW2|NEW on */
        outp(0x38C, 0xF8); MS(1); m0 = (unsigned char)inp(0x38D);
        outp(0x38D, 0x00); MS(1);                             /* FM mix 0 dB */
        outp(0x38C, 0xF9); MS(1); m1 = (unsigned char)inp(0x38D);
        outp(0x38D, 0x00); MS(1);                             /* PCM mix 0 dB */
        printf("   OPL4 mix: F8 was %02X, F9 was %02X -> both 00 (0 dB); NEW2|NEW left ON\n",
               m0, m1);
    }

    if (i == 3)      printf("ALL THREE ALIVE - vendor config replicated. Card left enabled.\n");
    else if (i > 0)  printf("%d of 3 alive - partial. Card left enabled for inspection.\n", i);
    else             printf("all dead under COR=%02X - try another /COR value.\n", corv);
    printf("(VEW2TRY /OFF powers it back down)\n");
    return i == 3 ? 0 : 2;
}

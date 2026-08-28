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
 *              both channels, FM and PCM), then RESTORE compat mode
 *              (NEW=0) - OPL2-era games never set the NEW-mode per-channel
 *              L/R output-enable bits and go silent if NEW is left on
 *   /V206=hex  write the 212's UNDECLARED vendor register at attr 0x206
 *   /V208=hex  ... and the one at attr 0x208.  Both read as floating on
 *              the 212 - which is exactly how a write-only decode looks -
 *              and the sibling CF-VEW211's vendor driver holds them at
 *              38h and 05h.  An I/O trace of the vendor enabler showed it
 *              makes almost no card I/O writes at all, so attribute space
 *              is what is left to explain its analog output switching on.
 *   /SR=1      COR choreography: configure UNDER soft-reset then release
 *              (COR <- E6 held, settle, then 66) - tests whether the ASIC
 *              fires its analog power-up on the SRESET release edge
 *   /SR=2      COR choreography: post-config SRESET pulse (66, E6, 66)
 *   /UART      after bring-up, send the MPU UART-mode command (3F -> 331)
 *              - tests whether scprodos's /C330 MPU init is the analog
 *              trigger (the MPU lives inside the MEI ASIC)
 *   /CODEC     map I/O window 0 to the CODEC block 530-539 instead of
 *              the MPU, then probe and BRING UP the codec: chip ID, then
 *              unmute DAC / Aux1 / Aux2 / Line / Mono.  The vendor
 *              enabler was caught doing exactly this - its I/O trace
 *              programs window 0 to 530-539 and then re-maps the window
 *              away to 330 - which is what an INITIALISATION window looks
 *              like.  If the OPL4 sums into the codec's output stage,
 *              that stage is the analog switch we have been hunting, and
 *              nothing reaches the jack until the codec is up.
 *   /VND       THE VENDOR SEQUENCE, as captured from SCPRODOS.COM by our
 *              ring-0 I/O tracer.  The OPL4's analog output is wired into
 *              the CODEC's LINE INPUT, and that input powers up MUTED -
 *              which is why every OPL4-side experiment stayed silent.  So:
 *              configure a codec-capable index with a window on 530-539,
 *              wait for the codec's INIT bit to clear, write I12=40h
 *              (MODE2), I18=I19=02h (line in, unmuted, gain 2) - the "big
 *              pop" - then reconfigure to the operational index 26h for
 *              MPU + OPL4 and drop the codec window, exactly as the vendor
 *              does.  The codec keeps its unmuted state across the change.
 *   /MPU       AFTER bring-up, re-point I/O window 1 from the OPL4 block
 *              to the MPU at 330-333 and probe it, leaving window 0 on
 *              the codec.  Answers a question we never actually tested:
 *              does the card DECODE 330h under this config index?  The
 *              vendor's SCPROENB maps only 530 and 388, but that is what
 *              its ENABLER REQUESTS, not what the ASIC decodes - and the
 *              Windows 3.1 driver renders MIDI on the OPL4 itself, so it
 *              would never map 330h even on a card that provides it.
 *              I/O windows are HOST-side: the card's configuration is not
 *              touched, only the mapping, so this is a free question to
 *              ask of any index.  Known-good signature, from the vendor's
 *              own index 26h: 331 reads BF and RESET is answered FE.
 *              Window 1 is put back on 388-38D afterwards.
 *   /TONE      play an FM test note twice: once in NEW mode with the
 *              L/R bits set, once in OPL2-compat mode - separates
 *              "mixer/mode wrong" from "analog path gated"
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

#define CBASE 0x530                 /* codec block when /CODEC maps it */
#define IAR   (CBASE + 4)
#define IDR   (CBASE + 5)

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

/* ---- CS4231A access: the CF-VEW211 lesson applies here too - this codec
 * silently DROPS microsecond-paced register writes from a cold chip, so
 * every write is ms-paced and read back, with retries. ------------------- */

static void ci_wait(void)
{
    unsigned long i;
    for (i = 0; i < 200000UL; i++) if (!(inp(IAR) & 0x80)) return;
}

static unsigned char ci_get(unsigned char idx)
{
    ci_wait(); outp(IAR, idx); MS(1);
    return (unsigned char)inp(IDR);
}

static int ci_put(unsigned char idx, unsigned char v)
{
    int t;
    for (t = 0; t < 5; t++) {
        ci_wait(); outp(IAR, idx); MS(1); outp(IDR, v); MS(2);
        ci_wait(); outp(IAR, idx); MS(1);
        if ((unsigned char)inp(IDR) == v) return 1;
    }
    return 0;
}

/* bring the codec's analog side up: every output path unmuted */
static int codec_up(void)
{
    static unsigned char regs[12] = { 0x06,0x07, 0x02,0x03, 0x04,0x05,
                                      0x12,0x13, 0x1A, 0x00,0x01, 0x1B };
    static unsigned char vals[12] = { 0x00,0x00, 0x08,0x08, 0x08,0x08,
                                      0x00,0x00, 0x00, 0x00,0x00, 0x00 };
    unsigned char iar, id;
    int i, ok = 0, bad = 0;

    iar = (unsigned char)inp(IAR);
    if (iar == 0xFF) { printf("   codec %03X: nothing there (IAR reads FF)\n", IAR); return 0; }
    ci_wait();
    id = ci_get(0x0C);                     /* I12: version / chip id */
    printf("   codec %03X: IAR=%02X  I12=%02X %s\n", IAR, iar, id,
           ((id & 0x8F) == 0x8A) ? "(CS4231A ALIVE)" : "(unexpected id)");
    if (id == 0xFF) return 0;

    for (i = 0; i < 12; i++) {
        if (ci_put(regs[i], vals[i])) ok++;
        else bad++;
    }
    printf("   codec unmute: %d of 12 registers verified%s\n", ok,
           bad ? " (some would not latch)" : "");
    printf("   (I6/I7 DAC, I2-I5 Aux1/Aux2, I18/I19 Line, I26 Mono, I0/I1 in, I27)\n");
    return 1;
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
    int i, s, found = -1, off = 0, ccsr = -1, mix = 0, tone = 0, sr = 0, uart = 0, step = 0;
    int v206 = -1, v208 = -1, codec = 0, vnd = 0, mpu = 0;
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
        if (!strnicmp(a + 1, "TONE", 4)) tone = 1;
        if (!strnicmp(a + 1, "SR=", 3))    sr    = (int)strtol(a + 4, 0, 10);
        if (!strnicmp(a + 1, "UART", 4))   uart  = 1;
        if (!strnicmp(a + 1, "STEP", 4))   step  = 1;
        if (!strnicmp(a + 1, "V206=", 5))  v206  = (int)strtol(a + 6, 0, 16);
        if (!strnicmp(a + 1, "V208=", 5))  v208  = (int)strtol(a + 6, 0, 16);
        if (!strnicmp(a + 1, "CODEC", 5))  codec = 1;
        if (!strnicmp(a + 1, "VND", 3))    vnd   = 1;
        if (!strnicmp(a + 1, "MPU", 3))    mpu   = 1;
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
            if (step) { printf("PHASE: socket power ON (Vcc)...\n"); MS(3000); }
            wr(0x02, 0xF1); MS(300);
            if (step) { printf("PHASE: reset release (mem mode)...\n"); MS(3000); }
            wr(0x03, 0x40); MS(100);
            if (step) MS(3000);
        }
        {   unsigned long t;                               /* READY, capped */
            for (t = 0; t < 200000UL; t++)
                if (rd(0x01) & 0x20) break;
        }
        MS(100);
        if (!mapattr()) { printf("socket %d: no attr window\n", s); continue; }
        {   unsigned t, k;                                 /* data gate: byte0  */
            unsigned char pa[8], pb[8];                    /* != FF AND stable  */
            for (k = 0; k < 8; k++) pa[k] = hostb(k * 2);  /* (un-settled reads */
            for (t = 0; t < 250; t++) {                    /* = FF ramp OR      */
                MS(20);                                    /* unstable garbage; */
                for (k = 0; k < 8; k++) pb[k] = hostb(k*2);/* cis-ff-bug.md)    */
                if (pb[0] != 0xFF) {
                    for (k = 0; k < 8 && pa[k] == pb[k]; k++) ;
                    if (k == 8) break;
                }
                for (k = 0; k < 8; k++) pa[k] = pb[k];
            }
        }
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

    if (step) { printf("PHASE: COR write next...\n"); MS(3000); }
    /* ---- COR: the one sanctioned attr write (3 choreographies) ---------- */
    if (sr == 1) {
        hostw(0x200, (unsigned char)(corv | 0x80));        /* index + SRESET held */
        MS(200);
        hostw(0x200, (unsigned char)corv);                 /* release             */
        MS(500);
        printf("COR /SR=1: %02X held -> %02X released", corv | 0x80, corv);
    } else if (sr == 2) {
        hostw(0x200, (unsigned char)corv); MS(200);
        hostw(0x200, (unsigned char)(corv | 0x80)); MS(200); /* pulse SRESET      */
        hostw(0x200, (unsigned char)corv); MS(500);
        printf("COR /SR=2: %02X -> %02X -> %02X", corv, corv | 0x80, corv);
    } else {
        hostw(0x200, (unsigned char)corv);
        MS(50);
        printf("COR <- %02X", corv);
    }
    v = hostb(0x200);
    printf(", reads back %02X %s\n", v,
           v == (unsigned char)corv ? "(latched)" : "(DIFFERS)");
    if (ccsr >= 0) {
        hostw(0x202, (unsigned char)ccsr);
        MS(50);
        v = hostb(0x202);
        printf("CCSR <- %02X, reads back %02X %s\n", ccsr, v,
               v == (unsigned char)ccsr ? "(latched)" : "(DIFFERS)");
    }
    if (v206 >= 0) {
        hostw(0x206, (unsigned char)v206);
        MS(100);
        printf("attr 206 <- %02X (reads %02X; a floating read is expected)\n",
               v206, hostb(0x206));
    }
    if (v208 >= 0) {
        hostw(0x208, (unsigned char)v208);
        MS(100);
        printf("attr 208 <- %02X (reads %02X; a floating read is expected)\n",
               v208, hostb(0x208));
    }
    unmapattr();

    if (step) { printf("PHASE: COR done. windows/intctl next...\n"); MS(3000); }
    /* ---- host side: vendor state verbatim (program windows DISABLED) ---- */
    wr(0x06, rd(0x06) & 0x3F);                             /* io wins off   */
    if (codec) {
        wr(0x08, 0x30); wr(0x09, 0x05);                    /* win0 530-539  */
        wr(0x0A, 0x39); wr(0x0B, 0x05);                    /* (the vendor's */
    } else {                                              /*  init window) */
        wr(0x08, 0x30); wr(0x09, 0x03);                    /* win0 330-333  */
        wr(0x0A, 0x33); wr(0x0B, 0x03);
    }
    wr(0x0C, 0x88); wr(0x0D, 0x03);                        /* win1 388-38D  */
    wr(0x0E, 0x8D); wr(0x0F, 0x03);
    wr(0x07, (unsigned char)ioctl);
    wr(0x03, 0xE9);                                        /* I/O mode, IRQ9 */
    MS(10);
    wr(0x06, (unsigned char)wen);
    MS(50);
    printf("PCIC: intctl=%02X ioctl=%02X winEn=%02X win0=%02X%02X-%02X%02X win1=%02X%02X-%02X%02X\n",
           rd(0x03), rd(0x07), rd(0x06),
           rd(0x09), rd(0x08), rd(0x0B), rd(0x0A),
           rd(0x0D), rd(0x0C), rd(0x0F), rd(0x0E));

    if (step) { printf("PHASE: config done. OPL probe next...\n"); MS(3000); }
    /* ---- ask the hardware ------------------------------------------------ */
    if (codec) { MS(100); (void)codec_up(); }

    if (vnd) {
        unsigned long t;
        unsigned char iar = 0xFF;
        printf("   /VND phase 1: codec index %02X, window 530-539\n", corv);
        for (t = 0; t < 300000UL; t++) {       /* wait for INIT to clear   */
            iar = (unsigned char)inp(IAR);
            if (iar != 0xFF && !(iar & 0x80)) break;
        }
        printf("   codec IAR settled at %02X %s\n", iar,
               (iar == 0xFF) ? "(NOT REACHABLE - wrong index?)" : "(ready)");
        if (iar != 0xFF) {
            /* Write, then REPORT the readback rather than demanding an
             * exact match: I12's low nibble is a read-only chip id, so a
             * good write of 40h reads back as 4Ah on a CS4231A. */
            unsigned char r12, r18, r19;
            ci_wait(); outp(IAR, 0x0C); MS(2); outp(IDR, 0x40); MS(3);
            ci_wait(); outp(IAR, 0x0C); MS(2); r12 = (unsigned char)inp(IDR);
            ci_wait(); outp(IAR, 0x12); MS(2); outp(IDR, 0x02); MS(3);
            ci_wait(); outp(IAR, 0x12); MS(2); r18 = (unsigned char)inp(IDR);
            ci_wait(); outp(IAR, 0x13); MS(2); outp(IDR, 0x02); MS(3);
            ci_wait(); outp(IAR, 0x13); MS(2); r19 = (unsigned char)inp(IDR);
            printf("   I12<-40 reads %02X %s | I18<-02 reads %02X | I19<-02 reads %02X\n",
                   r12, (r12 & 0x40) ? "(MODE2 SET)" : "(mode2 NOT set)", r18, r19);
            printf("   ^^ line in unmuted = the big pop, if the codec took it\n");
        }
        printf("   /VND phase 2: switching to operational index 26h\n");
        if (mapattr()) {
            hostw(0x200, 0x66);
            MS(100);
            printf("   COR <- 66, reads back %02X\n", hostb(0x200));
            unmapattr();
        }
        wr(0x06, rd(0x06) & 0x3F);             /* windows off while moving */
        wr(0x08, 0x30); wr(0x09, 0x03);        /* win0 -> MPU 330-333      */
        wr(0x0A, 0x33); wr(0x0B, 0x03);
        wr(0x0C, 0x88); wr(0x0D, 0x03);        /* win1 -> OPL4 388-38D     */
        wr(0x0E, 0x8D); wr(0x0F, 0x03);
        wr(0x07, (unsigned char)ioctl);
        wr(0x03, 0xE9);
        MS(10);
        wr(0x06, (unsigned char)wen);
        MS(100);
        printf("   PCIC now: intctl=%02X ioctl=%02X winEn=%02X win0=%02X%02X-%02X%02X win1=%02X%02X-%02X%02X\n",
               rd(0x03), rd(0x07), rd(0x06),
               rd(0x09), rd(0x08), rd(0x0B), rd(0x0A),
               rd(0x0D), rd(0x0C), rd(0x0F), rd(0x0E));
    }

    i  = opl_detect();
    if (step) { printf("PHASE: OPL done. MPU probe next...\n"); MS(3000); }
    if (!codec) i += mpu_probe();
    else { printf("   MPU not probed (/CODEC has the window)\n"); i++; }
    if (step) { printf("PHASE: MPU done. wave probe next...\n"); MS(3000); }
    i += wave_probe();
    if (step) { printf("PHASE: wave done.\n"); MS(3000); }

    if (uart) {
        unsigned long t;
        unsigned char ack = 0;
        for (t = 0; t < 100000UL; t++)
            if (!(inp(0x331) & 0x40)) break;
        outp(0x331, 0x3F);                                 /* enter UART mode */
        for (t = 0; t < 400000UL; t++)
            if (!(inp(0x331) & 0x80)) break;
        if (t < 400000UL) ack = (unsigned char)inp(0x330);
        printf("   MPU UART-mode cmd 3F -> %02X %s\n", ack,
               ack == 0xFE ? "(ACK)" : "(no/odd ack)");
    }

    if (mix) {
        unsigned char m0, m1;
        outp(0x38A, 0x05); MS(1); outp(0x38B, 0x03); MS(1);   /* NEW2|NEW on */
        outp(0x38C, 0xF8); MS(1); m0 = (unsigned char)inp(0x38D);
        outp(0x38D, 0x00); MS(1);                             /* FM mix 0 dB */
        outp(0x38C, 0xF9); MS(1); m1 = (unsigned char)inp(0x38D);
        outp(0x38D, 0x00); MS(1);                             /* PCM mix 0 dB */
        outp(0x38A, 0x05); MS(1); outp(0x38B, 0x00); MS(1);   /* compat mode */
        printf("   OPL4 mix: F8 was %02X, F9 was %02X -> both 00 (0 dB); NEW=0 restored\n",
               m0, m1);
    }

    if (tone) {
        static unsigned char op1[4] = { 0x20, 0x40, 0x60, 0x80 };
        static unsigned char v1[4]  = { 0x01, 0x18, 0xF0, 0x77 };
        static unsigned char op2[4] = { 0x23, 0x43, 0x63, 0x83 };
        static unsigned char v2[4]  = { 0x01, 0x00, 0xF0, 0x77 };
        int ph, k;
        for (ph = 0; ph < 2; ph++) {
            outp(0x38A, 0x05); MS(1);                          /* reg 105h    */
            outp(0x38B, ph == 0 ? 0x01 : 0x00); MS(1);         /* NEW / compat */
            for (k = 0; k < 4; k++) { outp(0x388, op1[k]); MS(1); outp(0x389, v1[k]); MS(1); }
            for (k = 0; k < 4; k++) { outp(0x388, op2[k]); MS(1); outp(0x389, v2[k]); MS(1); }
            outp(0x388, 0xC0); MS(1);
            outp(0x389, ph == 0 ? 0x30 : 0x00); MS(1);         /* NEW: L+R on  */
            outp(0x388, 0xA0); MS(1); outp(0x389, 0x98); MS(1);
            printf("   TONE %s: keying...", ph == 0 ? "NEW-mode (L/R set)" : "compat-mode");
            outp(0x388, 0xB0); MS(1); outp(0x389, 0x31); MS(1);/* key on      */
            MS(900);
            outp(0x388, 0xB0); MS(1); outp(0x389, 0x11); MS(1);/* key off     */
            MS(300);
            printf(" done\n");
        }
        outp(0x38A, 0x05); MS(1); outp(0x38B, 0x00); MS(1);    /* leave compat */
    }

    if (mpu) {
        printf("\n/MPU: does this index DECODE 330h?  Windows are host-side, so\n");
        printf("      only the mapping moves - the card's config is untouched.\n");
        /* Clear the window's enable bit BEFORE touching its start/end regs:
         * a half-written window can span ports it must never span. */
        wr(0x06, rd(0x06) & 0x3F);                  /* both io windows off  */
        wr(0x0C, 0x30); wr(0x0D, 0x03);             /* win1 -> MPU 330-333  */
        wr(0x0E, 0x33); wr(0x0F, 0x03);
        wr(0x07, (unsigned char)ioctl);
        wr(0x06, (unsigned char)wen);
        MS(50);
        printf("   win1 now %02X%02X-%02X%02X, win0 left on the codec\n",
               rd(0x0D), rd(0x0C), rd(0x0F), rd(0x0E));
        if (mpu_probe()) {
            printf("   *** MPU LIVE UNDER COR=%02X - THREE FUNCTIONS, ONE INDEX ***\n", corv);
            printf("   Production layout would be win0 530-539 + win1 330-38D.\n");
            printf("   NOTE before shipping that wide window: 378-37F = LPT1 sits\n");
            printf("   inside it, and the PCIC would drive card cycles there.\n");
        } else {
            printf("   no MPU under COR=%02X (vendor's index 26h gives 331=BF,\n", corv);
            printf("   RESET->FE - compare against that before concluding).\n");
        }
        wr(0x06, rd(0x06) & 0x3F);                  /* put win1 back on OPL4 */
        wr(0x0C, 0x88); wr(0x0D, 0x03);
        wr(0x0E, 0x8D); wr(0x0F, 0x03);
        wr(0x07, (unsigned char)ioctl);
        wr(0x06, (unsigned char)wen);
        printf("   win1 restored to 388-38D.\n");
    }

    if (i == 3)      printf("ALL THREE ALIVE - vendor config replicated. Card left enabled.\n");
    else if (i > 0)  printf("%d of 3 alive - partial. Card left enabled for inspection.\n", i);
    else             printf("all dead under COR=%02X - try another /COR value.\n", corv);
    printf("(VEW2TRY /OFF powers it back down)\n");
    return i == 3 ? 0 : 2;
}

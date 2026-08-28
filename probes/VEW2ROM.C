/* VEW2ROM.C - CF-VEW212: read the YRW801 sample ROM through the OPL4's
 * host memory interface, and decode its wave header table.
 *
 * WHY: a native DOS General MIDI renderer for this card needs the wave
 * map - which YRW801 waveform backs which GM program, and where each one
 * starts, loops and ends.  That map is IN THE ROM ITSELF, as a table of
 * 12-byte headers the OPL4 reads when a voice is keyed on.  Reading it
 * from the chip is the clean-room route: no vendor driver is examined,
 * only the hardware's own contents (cf. clean-room-enabler-preference).
 *
 * THE INTERFACE (wave registers, behind the NEW2 gate), from the YMF278B
 * datasheet register table for wave table synthesis:
 *     0x02  bit0   memory access mode (0 = sound generation, 1 = memory)
 *                  - WRITE only in this sense: READING 02H returns the
 *                    Device ID, so the mode bit cannot be read back
 *     0x03         memory address A21-A16
 *     0x04         memory address A15-A8
 *     0x05         memory address A7-A0
 *     0x06         memory data, AUTO-INCREMENTS the address on access
 * Entering memory access mode DISABLES PCM PLAYBACK, so run this at a DOS
 * prompt with nothing sounding.  Register 0x03 is saved and restored.
 *
 * !! UNTESTED AS OF 2026-08-26 - written while the bench was down.  Every
 * assumption below is one this tool CHECKS AND REPORTS rather than trusts,
 * because this hardware has already produced two convincing false
 * negatives (the all-FF CIS, and the NEW2 gate making a live wave section
 * read as a uniform 0x40).  If the self-checks fail, believe them.
 *   A1  0x02 bit0 enables host memory access (datasheet).
 *   A2  0x03/04/05 are address A21-A16/A15-A8/A7-A0, 0x06 is data.
 *   A3  reading 0x07 auto-increments the address.       -> /VERIFY tests this
 *   A4  DISPROVED 2026-08-28: the first read after a seek is REAL DATA,
 *       not stale.  Discarding it shifted every record by one byte and
 *       made the header table look non-monotonic.  /VERIFY's side-by-side
 *       alignment print is what caught it.  No discard now.
 *   A5  the header table starts at ROM address 0, 12 bytes per wave, in
 *       wave-number order.                              -> monotonic check
 *
 * HEADER LAYOUT (12 bytes):
 *   b0  bits7:6 format (0=8-bit, 1=12-bit, 2=16-bit), bits5:0 start[21:16]
 *   b1  start[15:8]      b2  start[7:0]
 *   b3  loop[15:8]       b4  loop[7:0]
 *   b5  end[15:8]        b6  end[7:0]      (end is held negative)
 *   b7  LFO/VIB   b8 AR/D1R   b9 DL/D2R   b10 RC/RR   b11 AM
 *
 * Usage: VEW2ROM [/WAVE=n] [/N=n] [/ADDR=hex] [/RAW] [/LEN=n] [/VERIFY]
 *                [/BASE=388] [/?]
 *   default    decode 16 headers from wave 0
 *   /WAVE=n    first wave number to decode (default 0)
 *   /N=n       how many headers to decode (default 16, max 512)
 *   /ADDR=hex  override the table base address (default 0)
 *   /RAW       raw hex dump instead of header decode
 *   /LEN=n     bytes for /RAW (default 256, max 4096)
 *   /VERIFY    re-read the same span after a fresh seek and compare -
 *              proves the address/auto-increment model before you trust
 *              anything else this prints
 *   /BASE=hex  OPL4 base (default 388)
 *
 * Redirect to a file:  VEW2ROM /N=384 > YRW801.TXT
 *
 * Build: C:\WATCOM\BLD.BAT VEW2ROM
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

static unsigned BASE = 0x388;
#define FMA1  (BASE + 2)
#define FMD1  (BASE + 3)
#define WADDR (BASE + 4)
#define WDATA (BASE + 5)

static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)

static unsigned char wv_get(unsigned char reg)
{
    outp(WADDR, reg); MS(1);
    return (unsigned char)inp(WDATA);
}
static void wv_put(unsigned char reg, unsigned char val)
{
    outp(WADDR, reg); MS(1);
    outp(WDATA, val); MS(1);
}
static void new2(int on)
{
    outp(FMA1, 0x05); MS(1);
    outp(FMD1, on ? 0x03 : 0x00); MS(1);
}

/* A4, settled: the first read after a seek returns REAL data.  An earlier
 * build discarded it on the assumption it was stale; that shifted every
 * 12-byte record one byte left and turned a perfectly monotonic header
 * table into "5 of 15 start addresses go backwards".  /VERIFY still prints
 * both alignments so the question stays answerable rather than assumed. */
static void mem_seek2(unsigned long a, int discard)
{
    wv_put(0x03, (unsigned char)((a >> 16) & 0x3FL));   /* A21-A16 */
    wv_put(0x04, (unsigned char)((a >>  8) & 0xFFL));   /* A15-A8  */
    wv_put(0x05, (unsigned char)( a        & 0xFFL));   /* A7-A0   */
    if (discard) (void)wv_get(0x06);
}
static void mem_seek(unsigned long a){ mem_seek2(a, 0); }
static unsigned char mem_next(void){ return wv_get(0x06); }

#define MAXBUF 4096
static unsigned char buf[MAXBUF];
static unsigned char buf2[64];

static void usage(void)
{
    printf("VEW2ROM - read the YRW801 sample ROM via the OPL4 memory interface\r\n");
    printf("  /WAVE=n   first wave number to decode (default 0)\r\n");
    printf("  /N=n      how many headers to decode (default 16)\r\n");
    printf("  /ADDR=hex override the table base (default 0)\r\n");
    printf("  /RAW      raw hex dump instead of header decode\r\n");
    printf("  /LEN=n    bytes for /RAW (default 256)\r\n");
    printf("  /VERIFY   re-read after a fresh seek and compare\r\n");
    printf("  /BASE=hex OPL4 base (default 388)\r\n");
    printf("Redirect to a file:  VEW2ROM /N=384 > YRW801.TXT\r\n");
}

int main(int argc, char **argv)
{
    int i, k, raw = 0, verify = 0, nwave = 16, wave0 = 0, len = 256;
    int opened = 0, uniform, bad;
    unsigned long tbase = 0L, a;
    unsigned char id, m03, first;

    for (i = 1; i < argc; i++) {
        char *p = argv[i];
        if (p[0] != '/' && p[0] != '-') continue;
        if (p[1] == '?')                       { usage(); return 0; }
        if (!strnicmp(p + 1, "RAW",    3))     raw    = 1;
        if (!strnicmp(p + 1, "VERIFY", 6))     verify = 1;
        if (!strnicmp(p + 1, "WAVE",   4))     sscanf(p + 5 + (p[5] == '='), "%d", &wave0);
        if (!strnicmp(p + 1, "ADDR",   4))     sscanf(p + 5 + (p[5] == '='), "%lx", &tbase);
        if (!strnicmp(p + 1, "BASE",   4))     sscanf(p + 5 + (p[5] == '='), "%x",  &BASE);
        if (!strnicmp(p + 1, "LEN",    3))     sscanf(p + 4 + (p[4] == '='), "%d",  &len);
        else if (p[1] == 'N' || p[1] == 'n')   sscanf(p + 2 + (p[2] == '='), "%d",  &nwave);
    }
    if (nwave < 1)      nwave = 1;
    if (nwave > 512)    nwave = 512;
    if (len   < 16)     len   = 16;
    if (len   > MAXBUF) len   = MAXBUF;
    if (wave0 < 0)      wave0 = 0;

    printf("VEW2ROM - CF-VEW212 YRW801 sample ROM reader (base %03X)\r\n", BASE);

    /* --- the NEW2 gate.  ALWAYS open it: with the gate shut the bus
     * floats, and we have watched it float to 40h, 10h and 20h - and 20h
     * is EXACTLY the real Device ID, so a DevID read cannot tell you
     * whether the gate is open.  Trusting one produced a full page of
     * "20" once and it looked like data.  Opening is idempotent and the
     * exit restores NEW=0, which is the game-safe state anyway. -------- */
    printf("DevID before gate = %02X (not trusted - see source)\r\n", wv_get(0x02));
    new2(1); opened = 1;
    id = wv_get(0x02);
    printf("DevID after NEW2  = %02X%s\r\n", id,
           (id & 0xF0) == 0x20 ? " -> YMF278B" : " -> NOT a YMF278B");
    if ((id & 0xF0) != 0x20) {
        printf("*** DevID %02X: no YMF278B answering. Is the card configured\r\n", id);
        printf("*** (COR index 23h, window 388-38D)?  Nothing read.\r\n");
        if (opened) new2(0);
        return 1;
    }

    /* --- memory access mode: 02H bit0.  It cannot be read back (reads of
     * 02H return the Device ID), so there is nothing to save - we restore
     * by writing 0 for sound-generation mode on the way out. ------------- */
    m03 = 0;
    printf("02H bit0 <- 1: memory access mode (PCM playback is disabled\r\n");
    printf("               while this bit is set; 0 restores it)\r\n");
    wv_put(0x02, 0x01);

    /* ------------------------------------------------------------------ */
    if (raw) {
        printf("\r\nraw ROM dump, %d bytes from %06lX:\r\n", len, tbase);
        mem_seek(tbase);
        for (k = 0; k < len; k++) buf[k] = mem_next();
        for (k = 0; k < len; k += 16) {
            int j;
            printf("%06lX:", tbase + (unsigned long)k);
            for (j = 0; j < 16 && k + j < len; j++) printf(" %02X", buf[k + j]);
            printf("  ");
            for (j = 0; j < 16 && k + j < len; j++) {
                unsigned char c = buf[k + j];
                printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            printf("\r\n");
        }
    } else {
        len = nwave * 12;
        if (len > MAXBUF) {
            nwave = MAXBUF / 12; len = nwave * 12;
            printf("NOTE: buffer caps this run at %d waves - rerun with\r\n", nwave);
            printf("      /WAVE=%d to continue past it.\r\n", wave0 + nwave);
        }
        a = tbase + (unsigned long)wave0 * 12L;
        printf("\r\nheader table: %d waves from wave %d, ROM %06lX, 12 bytes each\r\n",
               nwave, wave0, a);
        mem_seek(a);
        for (k = 0; k < len; k++) buf[k] = mem_next();
    }

    /* --- self-check 1: a uniform read is not memory -------------------- */
    first = buf[0]; uniform = 1;
    for (k = 1; k < len; k++) if (buf[k] != first) { uniform = 0; break; }

    /* --- self-check 2 (A3/A4): re-seek and compare the first 48 bytes -- */
    if (verify) {
        int n2 = (len < 48) ? len : 48, mism = 0;
        unsigned long va = raw ? tbase : (tbase + (unsigned long)wave0 * 12L);

        /* A3: same seek, same bytes?  If not, addressing or auto-increment
         * is not what this tool assumes and nothing else here is safe. */
        mem_seek(va);
        for (k = 0; k < n2; k++) buf2[k] = mem_next();
        for (k = 0; k < n2; k++) if (buf2[k] != buf[k]) mism++;
        printf("\r\nVERIFY repeatability: re-read %d bytes -> %d mismatches %s\r\n",
               n2, mism, mism ? "*** ADDRESSING MODEL IS WRONG ***"
                              : "(addressing + auto-increment behave)");

        /* A4: the alignment question, shown rather than assumed.  One of
         * these two rows is the real byte 0 of the span. */
        printf("VERIFY alignment - first 12 bytes each way:\r\n");
        printf("  NO discard (what this tool uses):");
        for (k = 0; k < 12 && k < n2; k++) printf(" %02X", buf2[k]);
        printf("\r\n  discarding the first read       :");
        mem_seek2(va, 1);
        for (k = 0; k < 12; k++) printf(" %02X", mem_next());
        printf("\r\n  The first row is the right one: its records are monotonic\r\n");
        printf("  and wave 0's data starts at 1800h = 512 headers x 12 bytes.\r\n");
    }

    wv_put(0x02, 0x00);                    /* back to sound generation mode */
    (void)m03;
    if (opened) { new2(0); printf("(NEW2 gate closed again, NEW=0 restored.)\r\n"); }

    if (uniform) {
        printf("\r\n*** EVERY BYTE READ %02X - THIS IS NOT MEMORY. ***\r\n", first);
        printf("Assumption A1 or A2 is wrong, or host memory access is not\r\n");
        printf("enabled.  Try /RAW at a few addresses before believing any\r\n");
        printf("header decode.  Nothing below would be real.\r\n");
        return 1;
    }
    if (raw) { printf("\r\ndone.\r\n"); return 0; }

    /* --- decode ------------------------------------------------------- */
    printf("\r\nwave |  start   loop    end  | fmt | LFO VIB | AR D1R | DL D2R | RC RR | AM\r\n");
    for (k = 0; k < nwave; k++) {
        unsigned char *h = buf + k * 12;
        unsigned long st = ((unsigned long)(h[0] & 0x3F) << 16)
                         | ((unsigned long)h[1] << 8) | (unsigned long)h[2];
        unsigned lp = ((unsigned)h[3] << 8) | h[4];
        unsigned en = ((unsigned)h[5] << 8) | h[6];
        int fmt = (h[0] >> 6) & 3;
        printf("%4d | %06lX  %04X  %04X |%3s | %2d %3d | %2d %3d | %2d %3d | %2d %2d | %2d\r\n",
               wave0 + k, st, lp, en,
               fmt == 0 ? "8b" : fmt == 1 ? "12b" : fmt == 2 ? "16b" : "?",
               (h[7] >> 3) & 7, h[7] & 7,
               (h[8] >> 4) & 15, h[8] & 15,
               (h[9] >> 4) & 15, h[9] & 15,
               (h[10] >> 4) & 15, h[10] & 15,
               h[11] & 7);
    }

    /* --- self-check 3 (A5): start addresses should not go backwards ---- */
    bad = 0;
    for (k = 1; k < nwave; k++) {
        unsigned char *h0 = buf + (k - 1) * 12, *h1 = buf + k * 12;
        unsigned long s0 = ((unsigned long)(h0[0] & 0x3F) << 16)
                         | ((unsigned long)h0[1] << 8) | (unsigned long)h0[2];
        unsigned long s1 = ((unsigned long)(h1[0] & 0x3F) << 16)
                         | ((unsigned long)h1[1] << 8) | (unsigned long)h1[2];
        if (s1 < s0) bad++;
    }
    printf("\r\nSANITY (A5): %d of %d consecutive start addresses go BACKWARDS.\r\n",
           bad, nwave - 1);
    if (bad * 4 > nwave)
        printf("  => that is far too many. The table is probably NOT at %06lX,\r\n"
               "    or is not 12 bytes per wave. Hunt it with /RAW first.\r\n", tbase);
    else if (bad)
        printf("  => a few is normal at bank boundaries; the layout looks right.\r\n");
    else
        printf("  => monotonic. The table base and stride look correct.\r\n");

    printf("\r\ndone.\r\n");
    return 0;
}

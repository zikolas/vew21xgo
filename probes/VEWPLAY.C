/* VEWPLAY.C - CF-VEW211 / CS4231A: PRDY-paced PIO .WAV player.
 * Follow-up to VEWPIO (which proved the FIFO transfer works electrically):
 * this is the properly PACED audible proof - and a usable little player.
 *
 * Requires the card already enabled at the default base (run VEW21XGO
 * first): codec at 0x534. Does not touch the socket/COR at all.
 *
 * Plays PCM .WAV: 8-bit unsigned or 16-bit signed, mono or stereo, at the
 * nearest CS4231-supported rate. Files up to 32KB play from RAM in one
 * shot; larger files stream in 32KB chunks (tiny seam at each disk read).
 *
 * Pacing: the PIT (1.193182 MHz), NOT the codec's PRDY flag. On this card
 * PRDY reads 'ready' continuously while the FIFO silently drops overfeed -
 * v1 trusted it, pushed 3s of audio in 0.5s, and the result sounded like
 * sub-8-bit gravel (5 of 6 samples discarded). Byte budget = elapsed PIT
 * ticks / (1193182/byterate), bursts capped under the 16-sample FIFO.
 * Hard wall-clock bound so it can never hang.
 *
 * Build: C:\WATCOM\BLD VEWPLAY      Usage: VEWPLAY file.wav
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <i86.h>

#define IAR 0x534
#define IDR 0x535
#define SR  0x536
#define PDR 0x537

static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)
static void ci_wait(void){ unsigned long i; for(i=0;i<400000UL;i++) if(!(inp(IAR)&0x80)) return; }
static void ci_put(unsigned char idx, unsigned char v){ ci_wait(); outp(IAR, idx); iod(200); outp(IDR, v); iod(200); }
static unsigned char ci_get(unsigned char idx){ ci_wait(); outp(IAR, idx); iod(200); return (unsigned char)inp(IDR); }
/* latch + read PIT channel 0 (counts DOWN at 1.193182 MHz, wraps at 0) */
static unsigned pit_read(void){ unsigned lo, hi; outp(0x43, 0x00); lo = (unsigned char)inp(0x40); hi = (unsigned char)inp(0x40); return (hi << 8) | lo; }

/* CS4231/AD1848 sample-rate codes (I8 bits 0-3), nearest match wins */
static struct { unsigned long hz; unsigned char code; } rates[] = {
    { 5510UL,0x01},{ 6620UL,0x0F},{ 8000UL,0x00},{ 9600UL,0x0E},
    {11025UL,0x03},{16000UL,0x02},{18900UL,0x05},{22050UL,0x07},
    {27042UL,0x04},{32000UL,0x06},{33075UL,0x0D},{37800UL,0x09},
    {44100UL,0x0B},{48000UL,0x0C}
};
#define NRATES (sizeof(rates)/sizeof(rates[0]))

static unsigned char buf[32768];

static unsigned long rd32(unsigned char *p){ return (unsigned long)p[0]|((unsigned long)p[1]<<8)|((unsigned long)p[2]<<16)|((unsigned long)p[3]<<24); }
static unsigned      rd16(unsigned char *p){ return (unsigned)p[0]|((unsigned)p[1]<<8); }

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char hdr[8], fmt[16];
    unsigned long dsize = 0, left, riffend, bound, t0, fed = 0, perb, acc = 0;
    unsigned long rate = 0, byterate, best;
    unsigned      chans = 0, bits = 0, tag = 0, n, i, prevp;
    unsigned char rcode = 0x07, i8, sil;
    unsigned long __far *bios = (unsigned long __far *)MK_FP(0x40, 0x6C);
    int have_fmt = 0, ri;

    if (argc < 2) { printf("usage: VEWPLAY file.wav\n"); return 2; }
    /* codec reachable? (0xFF = nothing decoding the port - card not enabled) */
    if ((unsigned char)inp(IAR) == 0xFF) { printf("no codec at %03X - run VEW21XGO first\n", IAR); return 1; }

    f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "RIFF", 4)) { printf("not a RIFF file\n"); fclose(f); return 1; }
    riffend = rd32(hdr + 4);
    if (fread(hdr, 1, 4, f) != 4 || memcmp(hdr, "WAVE", 4)) { printf("not a WAVE file\n"); fclose(f); return 1; }
    (void)riffend;
    /* walk chunks to "fmt " then "data" */
    for (;;) {
        unsigned long csz;
        if (fread(hdr, 1, 8, f) != 8) { printf("no data chunk found\n"); fclose(f); return 1; }
        csz = rd32(hdr + 4);
        if (!memcmp(hdr, "fmt ", 4)) {
            if (csz < 16 || fread(fmt, 1, 16, f) != 16) { printf("bad fmt chunk\n"); fclose(f); return 1; }
            tag = rd16(fmt); chans = rd16(fmt+2); rate = rd32(fmt+4); bits = rd16(fmt+14);
            have_fmt = 1;
            if (csz > 16) fseek(f, (long)(csz - 16 + (csz & 1)), SEEK_CUR);
        } else if (!memcmp(hdr, "data", 4)) {
            dsize = csz; break;
        } else fseek(f, (long)(csz + (csz & 1)), SEEK_CUR);
    }
    if (!have_fmt || tag != 1 || (bits != 8 && bits != 16) || chans < 1 || chans > 2) {
        printf("unsupported format (PCM 8/16-bit mono/stereo only)\n"); fclose(f); return 1;
    }
    /* nearest supported rate */
    best = 0xFFFFFFFFUL;
    for (ri = 0; ri < (int)NRATES; ri++) {
        unsigned long d = rates[ri].hz > rate ? rates[ri].hz - rate : rate - rates[ri].hz;
        if (d < best) { best = d; rcode = rates[ri].code; }
    }
    byterate = rate * chans * (bits / 8);
    if (!byterate) byterate = 22050UL;
    printf("%s: PCM %lu Hz %u-bit %s, %lu bytes (rate code %02X)\n",
           argv[1], rate, bits, chans == 2 ? "stereo" : "mono", dsize, rcode);

    /* codec: format under MCE, PPIO under MCE, then count + PEN */
    i8 = (unsigned char)(rcode | (chans == 2 ? 0x10 : 0x00) | (bits == 16 ? 0x40 : 0x00));
    /* Program format + PIO mode under MCE - PACED IN MILLISECONDS and
     * VERIFIED WITH RETRY. The CF-VEW211's CS4231A-KQ silently drops parts
     * of a microsecond-paced MCE sequence issued from a cold codec (the
     * same bytes land fine when written slowly): the cause of the great
     * silent-22kHz mystery. The identical sequence by hand over a slow
     * link always works, so: slow down, read back, retry until it took. */
    {
        int tries;
        for (tries = 0; tries < 8; tries++) {
            ci_wait();
            outp(IAR, 0x48); MS(2); outp(IDR, i8);   MS(2);  /* MCE | I8 */
            outp(IAR, 0x49); MS(2); outp(IDR, 0x48); MS(2);  /* MCE | I9 = PPIO|ACAL */
            outp(IAR, 0x00); MS(2);                          /* MCE off -> autocalibrate */
            ci_wait(); MS(10);
            /* wait out autocalibration (I11 bit5 = ACI) */
            { unsigned long w; for (w = 0; w < 400000UL; w++) if (!(ci_get(0x0B) & 0x20)) break; }
            if (ci_get(0x08) == i8 && (ci_get(0x09) & 0x48) == 0x48) break;
        }
        if (tries) printf("(format writes needed %d retr%s)\n", tries, tries == 1 ? "y" : "ies");
        if (tries == 8) printf("WARNING: format/PIO setup never verified - expect silence.\n");
    }
    ci_put(0x06, (unsigned char)(ci_get(0x06) & 0x3F));     /* un-mute L/R DAC but */
    ci_put(0x07, (unsigned char)(ci_get(0x07) & 0x3F));     /* keep the enabler's level */
    ci_put(0x0F, 0xFE); ci_put(0x0E, 0xFF);                 /* count base */
    /* PEN on-the-fly (PPIO+ACAL retained) - also verified with retry */
    {
        int tries;
        for (tries = 0; tries < 8; tries++) {
            ci_put(0x09, 0x49); MS(2);
            if (ci_get(0x09) & 0x01) break;
        }
        if (tries == 8) printf("WARNING: PEN never latched - playback disabled.\n");
    }

    /* Calibrate observed PIT rate against one BIOS tick: channel 0 in mode 3
     * (the PC default) decrements TWICE per 1.193182 MHz clock, mode 2 once.
     * One BIOS tick = 65536 real clocks, so cal ~= 65536 or ~131072. */
    { unsigned long cal = 0, guard = 0; unsigned nowp;
      t0 = *bios; while (*bios == t0 && ++guard < 4000000UL) ;
      prevp = pit_read(); cal = 0; t0 = *bios; guard = 0;
      while (*bios == t0 && ++guard < 4000000UL) {
          nowp = pit_read(); cal += (unsigned long)((prevp - nowp) & 0xFFFFU); prevp = nowp;
      }
      if (cal < 30000UL) cal = 65536UL;                     /* calibration failed - assume mode 2 */
      perb = (cal * 182UL / 10UL) / byterate;               /* observed ticks per byte */
    }
    if (!perb) perb = 1;
    prevp = pit_read();
    bound = dsize * 19UL / byterate + 91UL;                 /* ~ length + 5s, in ticks */
    t0 = *bios;
    left = dsize;
    while (left) {
        n = (unsigned)fread(buf, 1, left > sizeof(buf) ? sizeof(buf) : (unsigned)left, f);
        if (!n) break;
        for (i = 0; i < n; ) {
            unsigned nowp = pit_read();
            acc += (unsigned long)((prevp - nowp) & 0xFFFFU); prevp = nowp;
            if (acc > perb * 12) acc = perb * 12;      /* burst cap: FIFO is 16 deep */
            /* datasheet: writes after a completed sample are IGNORED until the
             * Status register is read - the SR read is what commits the sample
             * to the FIFO. So read SR after every byte (harmless mid-sample). */
            while (acc >= perb && i < n) { acc -= perb; outp(PDR, buf[i++]); (void)inp(SR); fed++; }
            if (!(fed & 0xFF) && (*bios - t0) > bound) { printf("wall-clock bound hit\n"); goto done; }
        }
        left -= n;
    }
done:
    /* settle the DAC at midscale before stopping (kills the stop-pop) */
    sil = (unsigned char)(bits == 16 ? 0x00 : 0x80);
    for (i = 0; i < 64; i++) { iod(60); outp(PDR, sil); (void)inp(SR); }
    MS(20);
    ci_put(0x09, 0x00);                                     /* stop; leave mixer to the enabler */
    fclose(f);
    printf("fed %lu/%lu bytes (PIT-paced)  elapsed %lu.%lus (nominal %lu.%lus)\n",
           fed, dsize,
           (*bios - t0) / 18UL, ((*bios - t0) % 18UL) * 10UL / 18UL,
           dsize / byterate, (dsize % byterate) * 10UL / byterate);
    return 0;
}

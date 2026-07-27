/* VEW2DUMP.C - CF-VEW212: one-shot register/state snapshot of a LIVE
 * vendor-configured card, for diffing against our enabler's setup.
 * (Clean-room: observes hardware state only, never driver code.)
 *
 * Designed for the PC110 with IBMDOSCS + the vendor stack resident and the
 * card WORKING: run it in the foreground, it borrows a FREE PCIC memory
 * window (never one that is enabled/owned), reads, restores. Card writes:
 * NONE by default. /CODEC adds the CS4231-style I0..I31 dump (index-reg
 * writes + transient MODE2 peek, restored) - only ask for it deliberately.
 *
 * Dumps: all 64 PCIC socket regs raw + decoded power/interface/IRQ/windows
 * (incl. the 8/16-bit ioctl flags), attr 0x200-0x21E, I/O base+0..+9,
 * OPL 388-38F (status-safe reads), MPU status 331 (NOT 330: a data-port
 * read would pop a byte from the stream).
 *
 * Usage: VEW2DUMP [/S n] [/CODEC]     Build: C:\WATCOM\BLD.BAT VEW2DUMP
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

#define PCIC 0x3E0
static unsigned soff = 0;
static unsigned char rd(unsigned r){ outp(PCIC, soff + r); return (unsigned char)inp(PCIC + 1); }
static void          wr(unsigned r, unsigned v){ outp(PCIC, soff + r); outp(PCIC + 1, v); }
static void iod(unsigned long n){ while (n--) (void)inp(0x80); }
#define MS(x) iod((unsigned long)(x) * 1000UL)

static unsigned BASE = 0x530;
#define IAR (BASE + 4)
#define IDR (BASE + 5)
static void ci_wait(void){ unsigned long i; for(i=0;i<400000UL;i++) if(!(inp(IAR)&0x80)) return; }
static unsigned char ci_get(unsigned char idx){ ci_wait(); outp(IAR, idx); MS(1); return (unsigned char)inp(IDR); }
static void ci_put(unsigned char idx, unsigned char v)
{
    int t;
    for (t = 0; t < 5; t++) {
        ci_wait(); outp(IAR, idx); MS(1); outp(IDR, v); MS(1);
        ci_wait(); outp(IAR, idx); MS(1);
        if ((unsigned char)inp(IDR) == v) return;
    }
}

int main(int argc, char **argv)
{
    int i, do_codec = 0, sock = 0;
    unsigned seg = 0;
    unsigned char en06, regs[64];

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '/' && argv[i][0] != '-') continue;
        if (argv[i][1]=='C' || argv[i][1]=='c') do_codec = 1;
        if (argv[i][1]=='S' || argv[i][1]=='s') sock = argv[i][2+(argv[i][2]=='=')] - '0';
    }
    soff = sock ? 0x40 : 0x00;

    printf("VEW2DUMP - CF-VEW212 live-state snapshot (socket %d)\r\n", sock);

    /* --- full socket register file, raw ---------------------------------- */
    for (i = 0; i < 64; i++) regs[i] = rd((unsigned)i);
    for (i = 0; i < 64; i += 16) {
        int j;
        printf("pcic %02X:", i);
        for (j = 0; j < 16; j++) printf(" %02X", regs[i + j]);
        printf("\r\n");
    }
    printf("decoded: IFstat=%02X pwr=%02X intctl=%02X (IRQ %d%s) csc=%02X winEn=%02X ioctl=%02X\r\n",
           regs[0x01], regs[0x02], regs[0x03],
           regs[0x03] & 0x0F, (regs[0x03] & 0x20) ? ", I/O mode" : ", mem mode",
           regs[0x05], regs[0x06], regs[0x07]);
    printf("  ioctl bits: win0 %s%s / win1 %s%s\r\n",
           (regs[0x07] & 0x01) ? "16bit" : "8bit", (regs[0x07] & 0x02) ? "+autosz" : "",
           (regs[0x07] & 0x10) ? "16bit" : "8bit", (regs[0x07] & 0x20) ? "+autosz" : "");
    printf("  io win0 %02X%02X-%02X%02X en=%d   win1 %02X%02X-%02X%02X en=%d\r\n",
           regs[0x09], regs[0x08], regs[0x0B], regs[0x0A], (regs[0x06] >> 6) & 1,
           regs[0x0D], regs[0x0C], regs[0x0F], regs[0x0E], (regs[0x06] >> 7) & 1);

    /* --- attr config regs via a genuinely FREE memory window -------------- */
    en06 = rd(0x06);
    for (i = 0; i < 5; i++) if (!(en06 & (1 << i))) break;
    if (i == 5) printf("attr: SKIPPED - no free memory window (winEn=%02X)\r\n", en06);
    else {
        static unsigned cand[2] = { 0xD000, 0xCC00 };
        int c, k, wb = 0x10 + i * 8;
        unsigned char sv[6];
        for (c = 0; c < 2; c++) {
            int clean = 1;
            for (k = 0; k < 32; k++)
                if (*(unsigned char __far *)MK_FP(cand[c], k) != 0xFF) { clean = 0; break; }
            if (clean) { seg = cand[c]; break; }
        }
        if (!seg) printf("attr: SKIPPED - D000/CC00 both busy\r\n");
        else {
            unsigned woff = ((unsigned)(0 - (seg >> 8)) & 0x3FFF) | 0x4000;
            unsigned char __far *AW = (unsigned char __far *)MK_FP(seg, 0);
            for (k = 0; k < 6; k++) sv[k] = rd(wb + k);
            wr(wb + 0, (seg >> 8) & 0xFF); wr(wb + 1, 0x00);
            wr(wb + 2, ((seg >> 8) + 3) & 0xFF); wr(wb + 3, 0x00);
            wr(wb + 4, woff & 0xFF); wr(wb + 5, (woff >> 8) & 0xFF);
            wr(0x06, en06 | (1 << i)); iod(2000);
            printf("attr 200-21E:");
            for (k = 0x200; k <= 0x21E; k += 2) printf(" %02X", AW[k]);
            printf("\r\n(cis check: %02X %02X %02X %02X)\r\n", AW[0], AW[2], AW[4], AW[6]);
            wr(0x06, en06);
            for (k = 0; k < 6; k++) wr(wb + k, sv[k]);
        }
    }

    /* --- I/O footprint (reads only; 330 deliberately NOT read) ------------ */
    printf("io %03X+0..9:", BASE);
    for (i = 0; i < 10; i++) printf(" %02X", (unsigned char)inp(BASE + i));
    printf("\r\nio 388-38F  :");
    for (i = 0; i < 8; i++) printf(" %02X", (unsigned char)inp(0x388 + i));
    printf("\r\nio 331 (MPU stat): %02X   (330 not read: would pop a data byte)\r\n",
           (unsigned char)inp(0x331));

    /* --- optional codec dump (writes index reg + transient MODE2) --------- */
    if (do_codec) {
        unsigned char i12, mode2_was;
        i12 = ci_get(0x0C);
        mode2_was = (unsigned char)(i12 & 0x40);
        printf("codec IAR=%02X I12=%02X (MODE2 %s)\r\ncodec I0-I15 : ",
               (unsigned char)inp(IAR), i12, mode2_was ? "on" : "off - peeking");
        for (i = 0; i < 16; i++) printf("%02X ", ci_get((unsigned char)i));
        printf("\r\n");
        if (!mode2_was) ci_put(0x0C, (unsigned char)(i12 | 0x40));
        printf("codec I16-I31: ");
        for (i = 16; i < 32; i++) printf("%02X ", ci_get((unsigned char)i));
        printf("\r\n");
        if (!mode2_was) ci_put(0x0C, (unsigned char)(i12 & ~0x40));
    } else printf("(codec dump skipped - add /CODEC deliberately)\r\n");

    printf("done.\r\n");
    return 0;
}

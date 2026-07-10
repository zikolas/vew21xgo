/* VEWDUMP.C - CF-VEW211: one-shot register state snapshot, for diffing our
 * enabler's configuration against the period vendor driver's (black-box,
 * clean-room: we observe hardware state, never driver code).
 *
 * Usage: enable the card (our tools OR the vendor stack), make it play or
 * idle as desired, then run VEWDUMP and compare snapshots.
 *
 * Dumps: codec I0..I31 (enables MODE2 read access if needed - NOTE: leaves
 * MODE2 as found), attribute config regs 0x200-0x20E, PCIC socket state,
 * I/O ports base+0..+9, OPL status. Read-only except the transient MODE2
 * peek. Assumes socket 0, codec at 0x534 (add /F40 for the PC-98 config).
 *
 * Build: C:\WATCOM\BLD VEWDUMP
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <i86.h>

#define PCIC   0x3E0
#define MEMSEG 0xD000
static unsigned pidx = PCIC, soff = 0;
static unsigned char rd(unsigned r){ outp(pidx, soff + r); return (unsigned char)inp(pidx + 1); }
static void          wr(unsigned r, unsigned v){ outp(pidx, soff + r); outp(pidx + 1, v); }
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
    int i;
    unsigned char i12, mode2_was;
    unsigned char __far *AW = (unsigned char __far *)MK_FP(MEMSEG, 0);
    unsigned char en06;

    for (i = 1; i < argc; i++)
        if (argv[i][0]=='/' && (argv[i][1]=='F'||argv[i][1]=='f')) BASE = 0xF40;

    pidx = PCIC; soff = 0;
    printf("VEWDUMP - CF-VEW211 register snapshot (codec base %03X)\n", BASE);
    printf("PCIC: IFstat=%02X pwr=%02X intctl=%02X winEn=%02X ioctl=%02X misc16=%02X\n",
           rd(0x01), rd(0x02), rd(0x03), rd(0x06), rd(0x07), rd(0x16));
    printf("PCIC io win0 %02X%02X-%02X%02X  win1 %02X%02X-%02X%02X\n",
           rd(0x09), rd(0x08), rd(0x0B), rd(0x0A),
           rd(0x0D), rd(0x0C), rd(0x0F), rd(0x0E));

    if ((unsigned char)inp(IAR) == 0xFF) { printf("no codec at %03X\n", IAR); return 1; }

    /* attribute config regs via a temporary window (restored after) */
    en06 = rd(0x06);
    {
        unsigned woff = ((unsigned)(0 - (MEMSEG >> 8)) & 0x3FFF) | 0x4000;
        unsigned char s10=rd(0x10),s11=rd(0x11),s12=rd(0x12),s13=rd(0x13),s14=rd(0x14),s15=rd(0x15);
        wr(0x10, (MEMSEG >> 8) & 0xFF); wr(0x11, 0x00);
        wr(0x12, ((MEMSEG >> 8) + 3) & 0xFF); wr(0x13, 0x00);
        wr(0x14, woff & 0xFF); wr(0x15, (woff >> 8) & 0xFF);
        wr(0x06, en06 | 0x01); iod(500);
        printf("attr: COR=%02X CCSR=%02X [204]=%02X [206]=%02X [208]=%02X [20A]=%02X\n",
               AW[0x200], AW[0x202], AW[0x204], AW[0x206], AW[0x208], AW[0x20A]);
        wr(0x10,s10); wr(0x11,s11); wr(0x12,s12); wr(0x13,s13); wr(0x14,s14); wr(0x15,s15);
        wr(0x06, en06);
    }

    printf("io: +0..+3 = %02X %02X %02X %02X   +8/+9 = %02X %02X   OPL stat = %02X\n",
           (unsigned char)inp(BASE), (unsigned char)inp(BASE+1),
           (unsigned char)inp(BASE+2), (unsigned char)inp(BASE+3),
           (unsigned char)inp(BASE+8), (unsigned char)inp(BASE+9),
           (unsigned char)inp(0x388));

    /* codec: read I0..I15; then peek I16..I31 under MODE2, restoring I12 */
    i12 = ci_get(0x0C);
    mode2_was = (unsigned char)(i12 & 0x40);
    printf("codec IAR=%02X  I12=%02X (MODE2 %s)\n",
           (unsigned char)inp(IAR), i12, mode2_was ? "was on" : "was off - peeking");
    printf("codec I0-I15 : ");
    for (i = 0; i < 16; i++) printf("%02X ", ci_get((unsigned char)i));
    printf("\n");
    if (!mode2_was) ci_put(0x0C, (unsigned char)(i12 | 0x40));
    printf("codec I16-I31: ");
    for (i = 16; i < 32; i++) printf("%02X ", ci_get((unsigned char)i));
    printf("\n");
    if (!mode2_was) ci_put(0x0C, (unsigned char)(i12 & ~0x40));

    printf("done (read-only; MODE2 restored as found).\n");
    return 0;
}

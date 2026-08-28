/* VEWSPYD.C - arm / disarm / dump the VEWSPY ring-0 I/O trace.
 *
 * VEWSPY.DLL (a Jemm loadable module) traps the CF-VEW212's whole I/O
 * footprint and records every access while armed.  This is the plain-DOS
 * end of that: it talks to the module through the magic port pair the
 * module also traps - 2A2h control, 2A3h data.
 *
 * Typical capture session (on the period vendor boot, stepped so that
 * SCPRODOS.COM has NOT run yet):
 *
 *     JEMM386 LOAD NOEMS X=C800-DFFF
 *     JLOAD VEWSPY.DLL
 *     VEWSPYD /ARM
 *     SCPRODOS.COM /C330 /M388 /I9      <- the enable we want to see
 *     VEWSPYD /OFF
 *     VEWSPYD > TRACE.TXT
 *
 * Usage: VEWSPYD [/ARM | /OFF | /CLEAR | /STAT]   (no switch = dump)
 * Build: C:\WATCOM\BLD.BAT VEWSPYD
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>

#define MAGCTL 0x2A2
#define MAGDAT 0x2A3

static const char *portname(unsigned p)
{
    switch (p) {
    case 0x530: return "ASIC +0  ";
    case 0x531: return "ASIC +1  ";
    case 0x532: return "ASIC +2  ";
    case 0x533: return "ASIC +3  ";
    case 0x534: return "codec IAR";
    case 0x535: return "codec IDR";
    case 0x536: return "codec +6 ";
    case 0x537: return "codec +7 ";
    case 0x538: return "ASIC +8  ";
    case 0x539: return "ASIC +9  ";
    case 0x330: return "MPU data ";
    case 0x331: return "MPU stat ";
    case 0x332: return "MPU +2   ";
    case 0x333: return "MPU +3   ";
    case 0x388: return "OPL addr ";
    case 0x389: return "OPL data ";
    case 0x38A: return "OPL addr2";
    case 0x38B: return "OPL data2";
    case 0x38C: return "WAVE addr";
    case 0x38D: return "WAVE data";
    case 0x38E: return "wave +2  ";
    case 0x38F: return "wave +3  ";
    case 0x3E0: return "PCIC idx ";
    case 0x3E1: return "PCIC dat ";
    }
    if (p >= 0x1000) return "PCIC reg ";
    return "?        ";
}

/* what a PCIC register write means, so the trace reads as intent */
static const char *pcicreg(unsigned r)
{
    switch (r) {
    case 0x02: return "power/RESETDRV";
    case 0x03: return "int+general ctl";
    case 0x05: return "csc int config";
    case 0x06: return "window enable";
    case 0x07: return "I/O control";
    case 0x08: case 0x09: case 0x0A: case 0x0B: return "I/O window 0";
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: return "I/O window 1";
    case 0x16: return "card detect/gen ctl";
    case 0x1E: return "global control";
    case 0x2F: return "mode control 2";
    case 0x3B: return "mode control 3";
    case 0x1F: return "mode control 1";
    }
    if (r >= 0x10 && r <= 0x35) return "memory window";
    return "";
}

int main(int argc, char **argv)
{
    unsigned long n, i;
    unsigned char b[4];
    int stat;

    if (argc > 1 && argv[1][0] == '/') {
        char c = argv[1][1];
        if (c == 'a' || c == 'A') { outp(MAGCTL, 2); printf("VEWSPY armed.\n"); return 0; }
        if (c == 'o' || c == 'O') { outp(MAGCTL, 3); printf("VEWSPY disarmed.\n"); return 0; }
        if (c == 'c' || c == 'C') { outp(MAGCTL, 1); printf("VEWSPY log cleared.\n"); return 0; }
        if (c == 's' || c == 'S') {
            stat = inp(MAGCTL);
            if (stat == 0xFF) { printf("VEWSPY not loaded (2A2h floats).\n"); return 1; }
            printf("VEWSPY loaded, logging %s.\n", stat ? "ARMED" : "off");
            return 0;
        }
        printf("Usage: VEWSPYD [/ARM | /OFF | /CLEAR | /STAT]  (no switch = dump)\n");
        return 0;
    }

    stat = inp(MAGCTL);
    if (stat == 0xFF) { printf("VEWSPY not loaded (2A2h floats).\n"); return 1; }

    outp(MAGCTL, 0);                       /* rewind the read cursor */
    n = 0;
    for (i = 0; i < 4; i++) n |= ((unsigned long)(unsigned char)inp(MAGDAT)) << (i * 8);

    printf("VEWSPY trace: %lu entries%s\n", n, n >= 3000 ? " (BUFFER FULL - may be truncated)" : "");
    printf("  #   port  name        dir  data   note\n");

    for (i = 0; i < n; i++) {
        unsigned port, k;
        for (k = 0; k < 4; k++) b[k] = (unsigned char)inp(MAGDAT);
        port = (unsigned)b[0] | ((unsigned)b[1] << 8);
        if (port >= 0x1000) {
            unsigned r = port & 0xFF;
            printf("%4lu  reg %02X  PCIC       OUT   %02X   %s", i, r, b[2], pcicreg(r));
        } else {
            printf("%4lu  %03X   %s  %s   %02X", i, port, portname(port),
                   (b[3] & 1) ? "OUT" : "in ", b[2]);
            if (b[3] & 2) printf(" (16-bit)");
        }
        printf("\n");
    }
    return 0;
}

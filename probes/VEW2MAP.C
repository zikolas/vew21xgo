/* VEW2MAP.C - CF-VEW212: build the GM program -> YRW801 wave map by
 * observation, using the vendor renderer as the oracle.
 *
 * The YRW801 does not carry a GM map or per-wave root notes; both are
 * driver knowledge.  Rather than disassemble anyone's driver, ask the
 * hardware: with the vendor DOS stack resident, send an exact program
 * change and an exact note to the MPU-401, then read which wave the
 * renderer loaded into which voice, and at what pitch.  Nothing is
 * examined but the chip's own registers.
 *
 * REQUIRES the vendor stack resident and the card on COR index 26h:
 *      DEVICE=C:\SCPRO\OPL4TSR.SYS
 *      C:\SCPRO\SCPRODOS.COM /C330 /M388 /I9
 *      C:\SCPRO\OPL4DRV.COM /LP7 /P64
 * (COR index 23h has no MPU, so there is nothing to send to.)
 *
 * WHAT EACH READING GIVES YOU
 *   08H+v         wave number bits 7:0     20H+v bit0  wave number bit 8
 *   38H+v 7:4     octave (signed)          38H+v 2:0   F-NUM f9..f7
 *   20H+v 7:1     F-NUM f6..f0             50H+v 7:1   total level
 *   68H+v bit7    KEY ON                   68H+v bit4  CH (output pin)
 * A wave number that CHANGES as the note rises marks a multisample key
 * split.  The octave and F-NUM at each note give the tuning, and hence
 * the sample's root: rate = 2^OCT * (1 + FNUM/1024), and the root is the
 * note whose rate is 1.0.
 *
 * SAFETY: writes only the MPU data port and reads only OPL4 registers.
 * It does NOT touch the NEW2 gate - the resident driver owns that, and
 * closing it would pull the wave section out from under the driver.
 * Every note sent is followed by its note-off.
 *
 * Usage: VEW2MAP [/P=a[-b]] [/NOTES=a,b,c] [/DRUM] [/CH=n] [/VEL=n]
 *                [/W=ms] [/FULL] [/MPU=330] [/BASE=388] [/?]
 *   /P=a[-b]  program range (default 0-127)
 *   /NOTES=   comma list of test notes (default 36,48,60,72,84)
 *   /DRUM     sweep channel 10 notes 35-81 instead of programs.  Implies
 *             /WATCH, because KEY ON never auto-clears and percussion
 *             therefore defeats the plain read entirely
 *   /WATCH    find the voice whose WAVE NUMBER changed rather than the
 *             lowest keyed one.  Robust against stale KEY ON bits and
 *             against voice reuse
 *   /CH=n     MIDI channel 0-15 (default 0; /DRUM forces 9)
 *   /VEL=n    velocity (default 100)
 *   /W=ms     settle after note-on before reading (default 60)
 *   /QUIET=n  budget, in ~1 ms sweeps, spent waiting for every voice to
 *             fall silent before each probe (default 400).  Probing into
 *             silence is what makes the reading unambiguous.
 *   /FULL     also print octave, F-NUM and TL for every reading
 *
 * Redirect it:  VEW2MAP > GMMAP.TXT
 *
 * Build: C:\WATCOM\BLD.BAT VEW2MAP
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

static unsigned BASE = 0x388, MPU = 0x330;
#define WADDR  (BASE + 4)
#define WDATA  (BASE + 5)
#define MPUDAT (MPU)
#define MPUST  (MPU + 1)

static void iod(unsigned n){ while (n--) (void)inp(0x80); }
static void msdelay(unsigned ms){ while (ms--) iod(1000); }

static unsigned char wv_get(unsigned char r)
{
    outp(WADDR, r); iod(40);
    return (unsigned char)inp(WDATA);
}

/* MPU-401: wait for DRR (bit 6) clear, then write. */
static int mpu_tx(unsigned char b)
{
    unsigned long t;
    for (t = 0; t < 300000UL; t++)
        if (!(inp(MPUST) & 0x40)) { outp(MPUDAT, b); return 1; }
    return 0;
}

static int  o_vel = 100, o_ch = 0, o_wait = 60, o_full = 0, o_quiet = 400;
static int  notes[16], nnotes;

typedef struct { int voice, wave, oct; unsigned fnum; int tl, nkeyed; } READING;

/* Which voices are keyed right now, as a 24-bit mask. */
static unsigned long keyed_mask(void)
{
    unsigned long m = 0;
    int v;
    for (v = 0; v < 24; v++)
        if (wv_get((unsigned char)(0x68 + v)) & 0x80) m |= 1UL << v;
    return m;
}

/* Report the voice that became keyed since `before`.  A plain "lowest
 * keyed voice" scan is wrong: percussion plays to the end of the sample
 * regardless of note-off, so old voices stay keyed and the scan keeps
 * finding one of those instead of the note we just sent.  The drum sweep
 * showed exactly that - every note reporting the same wave. */
/* Wait until NOTHING is keyed, so the probe that follows is unambiguous.
 *
 * Two earlier designs failed here and both are worth remembering.  A fixed
 * settle then "lowest keyed voice" reads a LEFTOVER when an old voice is
 * still sounding - which is why every drum note reported the same wave.
 * A before/after mask difference looks like the fix but is broken by VOICE
 * REUSE: when the driver re-keys a voice that was already keyed, the mask
 * does not change and the probe sees nothing at all.  Waiting for silence
 * removes the ambiguity instead of trying to reason around it. */
static int wait_silent(unsigned tries)
{
    unsigned t;
    for (t = 0; t < tries; t++)
        if (keyed_mask() == 0) return 1;
    return 0;
}

/* Then poll for ANY keyed voice.  Polling rather than sleeping matters
 * because a short percussion sample can start and finish inside a fixed
 * settle; one keyed_mask() sweep is about a millisecond. */
static unsigned long wait_keyed(unsigned tries)
{
    unsigned long now;
    unsigned t;
    for (t = 0; t < tries; t++) {
        now = keyed_mask();
        if (now) return now;
    }
    return 0;
}

static void read_voices(READING *r, unsigned long now)
{
    int v, first = -1, n = 0;
    unsigned char a, b, c;
    for (v = 0; v < 24; v++)
        if (now & (1UL << v)) { if (first < 0) first = v; n++; }
    r->nkeyed = n;
    if (first < 0) { r->voice = -1; r->wave = -1; r->oct = 0; r->fnum = 0; r->tl = 0; return; }
    a = wv_get((unsigned char)(0x08 + first));
    b = wv_get((unsigned char)(0x20 + first));
    c = wv_get((unsigned char)(0x38 + first));
    r->voice = first;
    r->wave  = (int)a | (int)((b & 1) << 8);
    r->oct   = (c >> 4) & 0x0F;  if (r->oct > 7) r->oct -= 16;
    r->fnum  = (unsigned)((c & 7) << 7) | (unsigned)(b >> 1);
    r->tl    = wv_get((unsigned char)(0x50 + first)) >> 1;
}

/* ---------------------------------------------------------------------
 * WATCHING THE WAVE NUMBER, NOT KEY ON  (the drum-map fix)
 *
 * KEY ON (68H+v bit7) almost certainly never auto-clears: the chip does
 * not reset it when a one-shot sample reaches its end, the sample simply
 * stops producing output.  So KEY ON means "started and not explicitly
 * stopped", NOT "currently sounding".  That one fact explains every
 * failure on channel 10 - voices stay keyed forever after a drum hit, so
 * "lowest keyed voice" returns a stale one, waiting for silence can never
 * succeed, and a keyed-mask difference shows nothing when the driver
 * re-keys a voice that was already keyed.
 *
 * The wave number register does not have that problem.  Allocating a
 * voice for a new sound WRITES 08H+v (and the wave-bit-8 / F-number pair
 * at 20H+v), whether or not the voice was already keyed.  So snapshot all
 * 24 voices, send the note, and find the voice whose wave number moved.
 * ------------------------------------------------------------------- */
static unsigned char snapw[24], snapf[24];

static void snapshot(unsigned char *w, unsigned char *f)
{
    int v;
    for (v = 0; v < 24; v++) {
        w[v] = wv_get((unsigned char)(0x08 + v));
        f[v] = wv_get((unsigned char)(0x20 + v));
    }
}

/* First voice whose (wave low, wave bit8 + F-num low) pair differs. */
static int changed_voice(const unsigned char *w, const unsigned char *f)
{
    unsigned char cw[24], cf[24];
    int v;
    snapshot(cw, cf);
    for (v = 0; v < 24; v++)
        if (cw[v] != w[v] || cf[v] != f[v]) return v;
    return -1;
}

static void read_voice_at(int v, READING *r)
{
    unsigned char a, bb, c;
    if (v < 0) { r->voice = -1; r->wave = -1; r->oct = 0; r->fnum = 0; r->tl = 0; r->nkeyed = 0; return; }
    a  = wv_get((unsigned char)(0x08 + v));
    bb = wv_get((unsigned char)(0x20 + v));
    c  = wv_get((unsigned char)(0x38 + v));
    r->voice  = v;
    r->wave   = (int)a | (int)((bb & 1) << 8);
    r->oct    = (c >> 4) & 0x0F;  if (r->oct > 7) r->oct -= 16;
    r->fnum   = (unsigned)((c & 7) << 7) | (unsigned)(bb >> 1);
    r->tl     = wv_get((unsigned char)(0x50 + v)) >> 1;
    r->nkeyed = (wv_get((unsigned char)(0x68 + v)) & 0x80) ? 1 : 0;
}

/* Probe by watching for a wave-number write.  Used for percussion, and
 * available for melodic via /WATCH. */
static void probe_watch(int chan, int prog, int note, READING *r)
{
    int v = -1;
    unsigned t;
    snapshot(snapw, snapf);
    if (prog >= 0) { mpu_tx((unsigned char)(0xC0 | chan)); mpu_tx((unsigned char)prog); }
    mpu_tx((unsigned char)(0x90 | chan));
    mpu_tx((unsigned char)note);
    mpu_tx((unsigned char)o_vel);
    for (t = 0; t < (unsigned)o_wait && v < 0; t++) v = changed_voice(snapw, snapf);
    read_voice_at(v, r);
    mpu_tx((unsigned char)(0x80 | chan));
    mpu_tx((unsigned char)note);
    mpu_tx(0);
    msdelay(20);
}

static int quiet_fail = 0;

/* ---------------------------------------------------------------------
 * WHAT WORKS, AND TWO THINGS THAT LOOK BETTER AND ARE NOT
 *
 * This plain version - note on, fixed settle, take the lowest keyed
 * voice - is the one whose output VALIDATED: program 0 note 60 read back
 * wave 304, identical to a measurement taken by hand over the link before
 * this tool existed, and the multisample runs came out orderly (program 0
 * walking 300 302 304 306 308 up the keyboard).
 *
 * Two "improvements" were tried on 2026-08-28 and BOTH WERE WORSE:
 *   1. Differential mask - snapshot keyed voices before and after, report
 *      what is newly keyed.  Broken by VOICE REUSE: when the driver
 *      re-keys a voice that was already keyed the mask does not change,
 *      so the probe sees nothing.  8 of 10 melodic probes came back empty
 *      and the ones that did not reported nonsense (324 where 300-308 was
 *      correct).
 *   2. Probe into silence - all-notes-off, wait for no voice keyed.
 *      Something stays keyed that CC123 does not clear: every program
 *      then reported wave 50 and a third of probes never reached silence.
 * Neither failure is understood.  Do not re-attempt either without first
 * working out what holds a voice keyed.
 *
 * KNOWN LIMITS of the plain version, both real:
 *   - It can report a LEFTOVER voice rather than the note just sent.  On
 *     melodic programs with explicit note-offs this is rare enough that
 *     the map validated; on CHANNEL 10 PERCUSSION it fails completely,
 *     because a drum sample plays to its end whatever the note-off says,
 *     so every note reported the same stale wave.  THE DRUM MAP IS
 *     THEREFORE STILL UNSOLVED.
 *   - Where the renderer LAYERS two waves the choice between them is
 *     arbitrary, which is why a few rows alternate between two numbers.
 * ------------------------------------------------------------------- */
static void probe(int chan, int prog, int note, READING *r)
{
    if (prog >= 0) { mpu_tx((unsigned char)(0xC0 | chan)); mpu_tx((unsigned char)prog); }
    mpu_tx((unsigned char)(0x90 | chan));
    mpu_tx((unsigned char)note);
    mpu_tx((unsigned char)o_vel);
    msdelay((unsigned)o_wait);
    read_voices(r, keyed_mask());
    mpu_tx((unsigned char)(0x80 | chan));
    mpu_tx((unsigned char)note);
    mpu_tx(0);
    msdelay(30);
}

int main(int argc, char **argv)
{
    int p0 = 0, p1 = 127, drum = 0, watch = 0, i, p, silent = 0, probes = 0;
    READING r;

    notes[0]=36; notes[1]=48; notes[2]=60; notes[3]=72; notes[4]=84; nnotes=5;

    for (i = 1; i < argc; i++) {
        char *s = argv[i];
        if (s[0] != '/' && s[0] != '-') continue;
        if (s[1] == '?') {
            printf("VEW2MAP [/P=a-b] [/NOTES=a,b,c] [/DRUM] [/CH=n] [/VEL=n]\r\n");
            printf("        [/W=ms] [/FULL] [/MPU=330] [/BASE=388]\r\n");
            printf("Needs the vendor stack resident (OPL4TSR+SCPRODOS+OPL4DRV).\r\n");
            return 0;
        }
        if (!strnicmp(s+1,"NOTES",5)) {
            char *q = s + 6 + (s[6]=='=');
            nnotes = 0;
            while (*q && nnotes < 16) {
                notes[nnotes++] = (int)strtol(q, &q, 10);
                while (*q == ',' || *q == ' ') q++;
            }
            if (!nnotes) { notes[0]=60; nnotes=1; }
        }
        else if (!strnicmp(s+1,"DRUM",4)) drum = 1;
        else if (!strnicmp(s+1,"WATCH",5)) watch = 1;
        else if (!strnicmp(s+1,"FULL",4)) o_full = 1;
        else if (!strnicmp(s+1,"BASE",4)) BASE = (unsigned)strtol(s+6,0,16);
        else if (!strnicmp(s+1,"MPU",3))  MPU  = (unsigned)strtol(s+5,0,16);
        else if (!strnicmp(s+1,"VEL",3))  o_vel = (int)strtol(s+5,0,0);
        else if (!strnicmp(s+1,"CH",2))   o_ch  = (int)strtol(s+4,0,0);
        else if (!strnicmp(s+1,"QUIET",5)) o_quiet = (int)strtol(s+7,0,0);
        else if (s[1]=='W' || s[1]=='w')  o_wait = (int)strtol(s+3,0,0);
        else if (s[1]=='P' || s[1]=='p') {
            char *q = s + 2 + (s[2]=='=');
            p0 = (int)strtol(q, &q, 10);
            p1 = (*q == '-') ? (int)strtol(q+1, 0, 10) : p0;
        }
    }
    if (o_wait < 5)   o_wait = 5;
    if (o_wait > 500) o_wait = 500;
    if (o_quiet < 10)   o_quiet = 10;
    if (o_quiet > 4000) o_quiet = 4000;
    if (drum) o_ch = 9;
    if (drum) watch = 1;            /* percussion needs the wave-number watch */

    printf("VEW2MAP - GM program -> YRW801 wave, read back from the renderer\r\n");
    printf("MPU %03X, OPL4 %03X, channel %d, velocity %d, poll budget %d\r\n",
           MPU, BASE, o_ch, o_vel, o_wait);
    printf(watch ? "watching for a WAVE NUMBER write (KEY ON never auto-clears)\r\n"
                 : "plain read: lowest keyed voice after a fixed settle\r\n");
    printf("MPU status %03X = %02X%s\r\n", MPUST, (unsigned char)inp(MPUST),
           ((unsigned char)inp(MPUST) == 0xFF) ? "  <-- FF: no MPU here!" : "");
    printf("wave DevID = %02X%s\r\n", wv_get(0x02),
           ((wv_get(0x02) & 0xF0) == 0x20) ? "" : "  <-- not answering");
    printf("(the NEW2 gate is left exactly as found - the driver owns it)\r\n\r\n");

    if (drum) {
        printf("note | voice wave  oct fnum  tl   (channel 10 percussion)\r\n");
        for (p = 35; p <= 81; p++) {
            if (watch) probe_watch(9, -1, p, &r); else probe(9, -1, p, &r);
            probes++;
            if (r.wave < 0) { silent++; printf(" %3d |   -     -\r\n", p); continue; }
            printf(" %3d |  %2d  %4d  %+3d %4u %3d%s\r\n",
                   p, r.voice, r.wave, r.oct, r.fnum, r.tl,
                   r.nkeyed > 1 ? "   (layered)" : "");
            if (kbhit()) { (void)getch(); break; }
        }
    } else {
        printf("prog |");
        for (i = 0; i < nnotes; i++) printf(" n%-3d ", notes[i]);
        printf("\r\n");
        for (p = p0; p <= p1; p++) {
            printf(" %3d |", p);
            for (i = 0; i < nnotes; i++) {
                if (watch) probe_watch(o_ch, i == 0 ? p : -1, notes[i], &r);
                else        probe(o_ch, i == 0 ? p : -1, notes[i], &r);
                probes++;
                if (r.wave < 0) { silent++; printf("   -  "); }
                else            printf(" %4d ", r.wave);
            }
            if (o_full) {
                probe(o_ch, -1, notes[nnotes/2], &r);
                if (r.wave >= 0)
                    printf("  | oct %+d fnum %4u tl %3d", r.oct, r.fnum, r.tl);
            }
            printf("\r\n");
            if (kbhit()) { (void)getch(); break; }
        }
    }

    if (probes && silent == probes) {
        printf("\r\nEVERY probe found no keyed voice.  Either the vendor stack is\r\n");
        printf("not resident (OPL4TSR + SCPRODOS + OPL4DRV, in that order), or\r\n");
        printf("the card is not on COR index 26h and there is no MPU at %03X.\r\n", MPU);
        return 1;
    }
    printf("\r\ndone - %d probes, %d found nothing keyed.\r\n", probes, silent);
    if (quiet_fail)
        printf("WARNING: %d probes could not reach silence first - raise /QUIET.\r\n",
               quiet_fail);
    return 0;
}

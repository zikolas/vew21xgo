/* OPL4SYN.C - resident OPL4/YRW801 General MIDI synth for DOS (TSR).
 *
 * The OPL4MID engine as an INT 2Fh multiplex service, so MPUSHIM /SYNTH
 * (and anything else that speaks the same two calls) can hand it a raw
 * MIDI byte stream and get wavetable GM out of the CF-VEW212 on COR
 * index 23h - where the card has its codec and FM but no MPU-401.
 *
 * The multiplex contract (what MPUSHIM's V86 blob calls):
 *   INT 2Fh  AH = id (default BDh)  AL = 00h  -> AL = FFh   "installed?"
 *            AH = id                AL = 01h, DL = byte     "MIDI byte"
 * Anything else on the id is passed down the chain.  The handler runs on
 * the CALLER's stack and preserves everything (AL of the install check
 * excepted); the engine keeps its own state, so calls that nest - a music
 * ISR interrupting a foreground write - append to a small queue that the
 * outer call drains: a MIDI byte is never dropped and never reordered.
 *
 * The MIDI side: note on/off, running status, program change, CC 7
 * (volume, at next note), CC 10 (pan, at next note), CC 120/123 (all
 * notes off), CC 121 (reset controllers), pitch bend (applied LIVE to
 * sounding voices, +/-2 semitones), channel 10 percussion.  Sysex is
 * swallowed, real-time bytes are ignored.  CC 64 sustain is not
 * implemented yet.
 *
 * Instruments: OPL4TAB.H, ported from the Linux ALSA opl4 driver
 * (sound/drivers/opl4/yrw801.c and opl4_synth.c, (c) 2003 Clemens
 * Ladisch, dual-licensed BSD-2-clause / GPL v2).  The voice programming
 * sequence and its rules are the ones OPL4MID proved on hardware -
 * see that file; the tone-number-bit-8-before-08h rule above all.
 *
 * Usage:  OPL4SYN [/BASE=388] [/MIX=n] [/TL=n] [/ID=xx]   install, stay resident
 *         OPL4SYN /TEST [/ID=xx]                          feed the RESIDENT copy
 *                                                         a scale + drums, exit
 * Remove: reboot.  Requires the card up on index 23h (VEW21XGO) first.
 *
 * Build: C:\WATCOM\BLD.BAT OPL4SYN   (wcc -ms, C89; OPL4TAB.H alongside)
 *
 * License: GPL v2, like OPL4MID and the tables (see LICENSE).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <conio.h>
#include <dos.h>

static unsigned BASE = 0x388;
#define FMA1  (BASE + 2)
#define FMD1  (BASE + 3)
#define WADDR (BASE + 4)
#define WDATA (BASE + 5)

static void iod(unsigned n){ while (n--) (void)inp(0x80); }

/* Register pacing.  The probe tools used iod(40) (~50 us) out of caution;
 * resident use cannot afford it - at ~2 ms per note-on inside a trap with
 * interrupts masked, the system clock loses ticks and every sequencer
 * slows down (DOSMID bench, 2026-08-28).  ALSA writes these registers
 * back-to-back with no delay; a 2-read guard covers bus recovery. */
#define IODLY 2

static unsigned char wv_get(unsigned char r)
{
    outp(WADDR, r); iod(IODLY);
    return (unsigned char)inp(WDATA);
}
static void wv_put(unsigned char r, unsigned char v)
{
    outp(WADDR, r); iod(IODLY);
    outp(WDATA, v); iod(IODLY);
}
static void new2(int on)
{
    outp(FMA1, 0x05); iod(IODLY);
    outp(FMD1, on ? 0x03 : 0x00); iod(IODLY);
}

#include "OPL4TAB.H"

/* ---- voices ------------------------------------------------------------ */
#define NVOICE 24
typedef struct {
    int ch, note, on;
    unsigned long age;
    unsigned char misc;
    unsigned short tone;          /* stored for bit 8 on bend re-pitch     */
    unsigned char ksc;
    short pofs;
    unsigned char pnote;          /* the pitch note (60 for drums)         */
} VOICE;
static VOICE vc[NVOICE];
static unsigned long agec = 1;

static int o_tl = 0;
static int chprog[16], chvol[16], chpan[16];
static short chbend[16];          /* in 100/128-cent units, +/-0x100 = 2st */

static void voice_off(int v)
{
    vc[v].misc &= 0x7F;
    wv_put((unsigned char)(0x68 + v), vc[v].misc);
    vc[v].on = 0;
}

static int voice_alloc(void)
{
    int i, best = 0;
    unsigned long oldest = 0xFFFFFFFFUL;
    for (i = 0; i < NVOICE; i++) if (!vc[i].on) return i;
    for (i = 0; i < NVOICE; i++) if (vc[i].age < oldest) { oldest = vc[i].age; best = i; }
    voice_off(best);
    return best;
}

static void all_off(void)
{
    int i;
    for (i = 0; i < NVOICE; i++) voice_off(i);
}

static void note_off(int ch, int note)
{
    int v;
    for (v = 0; v < NVOICE; v++)
        if (vc[v].on && vc[v].ch == ch && vc[v].note == note) voice_off(v);
}

/* pitch from the voice's stored region facts + the channel's live bend */
static void voice_pitch(int v)
{
    long pitch;
    int octv;
    unsigned f;
    pitch = (((long)(vc[v].pnote - 60) << 7) * vc[v].ksc) / 100 + (60L << 7);
    pitch += vc[v].pofs + chbend[vc[v].ch];
    if (pitch < 0)        pitch = 0;
    if (pitch >= 0x6000L) pitch = 0x5FFFL;
    octv = (int)(pitch / 0x600) - 8;
    f = pitch_map[(unsigned)(pitch % 0x600)];
    wv_put((unsigned char)(0x20 + v),
           (unsigned char)(((f & 0x7F) << 1) | ((vc[v].tone >> 8) & 1)));
    wv_put((unsigned char)(0x38 + v),
           (unsigned char)(((octv & 0x0F) << 4) | ((f >> 7) & 0x07)));
}

static void start_region(int ch, int note, int pnote, int vel,
                         const REGION *rg)
{
    int v, att, pan;

    v = voice_alloc();
    vc[v].ch = ch; vc[v].note = note; vc[v].on = 1; vc[v].age = agec++;
    vc[v].tone = rg->tone; vc[v].ksc = rg->ksc; vc[v].pofs = rg->pofs;
    vc[v].pnote = (unsigned char)pnote;

    /* tone number bit 8 must be latched in 20h BEFORE the 08h write -
     * that write triggers the 12-byte header fetch from the YRW801. */
    wv_put((unsigned char)(0x20 + v), (unsigned char)((rg->tone >> 8) & 1));
    wv_put((unsigned char)(0x08 + v), (unsigned char)(rg->tone & 0xFF));

    pan = rg->pan + chpan[ch];
    if (pan < -7) pan = -7;
    if (pan >  7) pan =  7;
    vc[v].misc = (unsigned char)(0x20 | (pan & 0x0F));
    wv_put((unsigned char)(0x68 + v), vc[v].misc);

    voice_pitch(v);

    att = rg->att + vol_tab[chvol[ch] & 0x7F] + vol_tab[vel & 0x7F];
    att = 0x7F - ((0x7F - att) * rg->vf) / 0xFE;
    att += o_tl;
    if (att < 0)    att = 0;
    if (att > 0x7E) att = 0x7E;
    wv_put((unsigned char)(0x50 + v), (unsigned char)((att << 1) | 1));

    /* envelope overrides only after the header load ends, or the loaded
     * header would clobber them */
    { int t = 400; while ((inp(BASE) & 0x02) && --t) iod(1); }
    wv_put((unsigned char)(0x80 + v), rg->lfovib);
    wv_put((unsigned char)(0x98 + v), rg->ad1);
    wv_put((unsigned char)(0xB0 + v), rg->ld2);
    wv_put((unsigned char)(0xC8 + v), rg->rc);
    wv_put((unsigned char)(0xE0 + v), rg->trem);

    vc[v].misc = (unsigned char)((vc[v].misc & 0x1F) | 0x80);   /* KEY ON */
    wv_put((unsigned char)(0x68 + v), vc[v].misc);
}

static void note_on(int ch, int note, int vel)
{
    int i, n = 0, prog;
    unsigned base, cnt;
    if (vel == 0) { note_off(ch, note); return; }
    prog = ch == 9 ? 128 : (chprog[ch] & 0x7F);
    base = alsa_prog[prog].base;
    cnt  = alsa_prog[prog].n;
    for (i = 0; i < (int)cnt && n < 2; i++) {
        const REGION *rg = &alsa_reg[base + i];
        if (note >= rg->lo && note <= rg->hi) {
            start_region(ch, note, ch == 9 ? 60 : note, vel, rg);
            n++;
        }
    }
}

static void ch_bend(int ch, int lo7, int hi7)
{
    int v;
    chbend[ch] = (short)((((hi7 << 7) | lo7) - 8192) / 32);
    for (v = 0; v < NVOICE; v++)
        if (vc[v].on && vc[v].ch == ch) voice_pitch(v);
}

static void ch_off(int ch)
{
    int v;
    for (v = 0; v < NVOICE; v++)
        if (vc[v].on && vc[v].ch == ch) voice_off(v);
}

static void syn_reset(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        chprog[i] = 0; chvol[i] = 100; chpan[i] = 0; chbend[i] = 0;
    }
    all_off();
}

/* ---- the MIDI byte stream --------------------------------------------- */
static unsigned char m_st = 0, m_d0 = 0, m_have = 0, m_need = 0;

static void syn_byte1(unsigned char b)      /* one byte, engine level */
{
    int ch;
    if (b >= 0xF8) return;                  /* real-time: ignore          */
    if (b & 0x80) {
        if (b >= 0xF0) { m_st = 0; return; }   /* sysex/common: cancel RS */
        m_st = b; m_have = 0;
        m_need = ((b & 0xE0) == 0xC0) ? 1 : 2; /* Cn/Dn take 1 data byte  */
        return;
    }
    if (!m_st) return;                      /* stray data / inside sysex  */
    if (m_have == 0 && m_need == 2) { m_d0 = b; m_have = 1; return; }
    m_have = 0;                             /* running status persists    */
    ch = m_st & 0x0F;
    switch (m_st & 0xF0) {
    case 0x80: note_off(ch, m_d0); break;
    case 0x90: note_on(ch, m_d0, b); break;
    case 0xA0: break;                       /* poly aftertouch: ignored   */
    case 0xB0:
        if      (m_d0 ==   7) chvol[ch] = b;
        else if (m_d0 ==  10) chpan[ch] = (b - 64) >> 3;
        else if (m_d0 == 120 || m_d0 == 123) ch_off(ch);
        else if (m_d0 == 121) { chbend[ch] = 0; chpan[ch] = 0; }
        break;
    case 0xC0: chprog[ch] = b; break;
    case 0xD0: break;                       /* channel aftertouch         */
    case 0xE0: ch_bend(ch, m_d0, b); break;
    }
}

/* Nesting: the multiplex call can arrive while a previous one is still
 * synthesising (a music ISR on top of a foreground write).  The inner
 * call appends to this queue and returns; the outer call drains it.
 * Order preserved, nothing dropped - MPUSHIM's own transmit rule.
 * Enqueue, the busy handoff and the drain-exit test sit inside interrupt-
 * off windows (caller's IF restored after): a byte arriving between "queue
 * empty" and "busy = 0" would otherwise strand until the next call. */
static volatile unsigned char q[64], qh = 0, qt = 0, q_busy = 0;

static unsigned flags_cli(void);
#pragma aux flags_cli = "pushf" "pop ax" "cli" value [ax];
static void flags_put(unsigned f);
#pragma aux flags_put = "push ax" "popf" parm [ax];

static void syn_byte(unsigned char b)
{
    unsigned f = flags_cli();
    q[qh] = b;
    qh = (unsigned char)((qh + 1) & 63);
    if (q_busy) { flags_put(f); return; }
    q_busy = 1;
    flags_put(f);
    for (;;) {
        unsigned char c;
        f = flags_cli();
        if (qt == qh) { q_busy = 0; flags_put(f); return; }
        c = q[qt];
        qt = (unsigned char)((qt + 1) & 63);
        flags_put(f);
        syn_byte1(c);
    }
}

/* ---- INT 2Fh multiplex hook -------------------------------------------- */
static unsigned char mux_id = 0xBD;
static void (__interrupt __far *prev2f)();

static void __interrupt __far int2f_handler(union INTPACK r)
{
    if (r.h.ah == mux_id) {
        if (r.h.al == 0x00) { r.h.al = 0xFF; return; }
        if (r.h.al == 0x01) { syn_byte(r.h.dl); return; }
    }
    _chain_intr(prev2f);
}

/* ---- install / test ----------------------------------------------------- */
static unsigned get_ss(void);
#pragma aux get_ss = "mov ax,ss" value [ax];
static unsigned get_sp(void);
#pragma aux get_sp = "mov ax,sp" value [ax];

static void mux_send(unsigned char b)       /* /TEST: feed the resident copy */
{
    union REGS r;
    r.h.ah = mux_id; r.h.al = 0x01; r.h.dl = b;
    int86(0x2F, &r, &r);
}

static void ticks_wait(unsigned n)          /* BIOS ticks, ~55 ms each */
{
    unsigned long __far *tick = (unsigned long __far *)MK_FP(0x0040, 0x006C);
    unsigned long t0 = *tick;
    while (*tick - t0 < n) {}
}

static int self_test(void)
{
    static const unsigned char scale[] = { 60, 64, 67, 72 };
    union REGS r;
    int i;

    r.h.ah = mux_id; r.h.al = 0x00;
    int86(0x2F, &r, &r);
    if (r.h.al != 0xFF) {
        printf("OPL4SYN /TEST: nothing resident on id %02X.\r\n", mux_id);
        return 1;
    }
    printf("piano scale");
    fflush(stdout);
    mux_send(0xC0); mux_send(0x00);              /* program 0              */
    for (i = 0; i < 4; i++) {
        mux_send(0x90); mux_send(scale[i]); mux_send(0x64);
        ticks_wait(6);
        mux_send(0x80); mux_send(scale[i]); mux_send(0x00);
    }
    printf(", drums");
    fflush(stdout);
    for (i = 0; i < 4; i++) {                    /* kick, snare, hats      */
        mux_send(0x99); mux_send(36); mux_send(0x70);
        mux_send(0x99); mux_send(i & 1 ? 38 : 42); mux_send(0x60);
        ticks_wait(5);
    }
    ticks_wait(9);
    mux_send(0xB0); mux_send(123); mux_send(0);  /* tidy up                */
    printf(" - done.\r\n");
    return 0;
}

int main(int argc, char **argv)
{
    int i, mix = 0, test = 0;
    unsigned char id;
    unsigned keep;

    for (i = 1; i < argc; i++) {
        char *p = argv[i];
        if (p[0] != '/' && p[0] != '-') continue;
        if (p[1] == '?') {
            printf("OPL4SYN [/BASE=388] [/MIX=n] [/TL=n] [/ID=xx]  install (TSR)\r\n");
            printf("OPL4SYN /TEST [/ID=xx]                play a test through it\r\n");
            printf("Needs the card on COR index 23h (run VEW21XGO first).\r\n");
            return 0;
        }
        if      (!strnicmp(p+1,"BASE",4)) BASE   = (unsigned)strtol(p+5+(p[5]=='='),0,16);
        else if (!strnicmp(p+1,"TEST",4)) test   = 1;
        else if (!strnicmp(p+1,"MIX",3))  mix    = (int)strtol(p+4+(p[4]=='='),0,0);
        else if (!strnicmp(p+1,"TL",2))   o_tl   = (int)strtol(p+3+(p[3]=='='),0,0);
        else if (!strnicmp(p+1,"ID",2))   mux_id = (unsigned char)strtol(p+3+(p[3]=='='),0,16);
    }

    if (test) return self_test();

    {   /* already installed on this id? */
        union REGS r;
        r.h.ah = mux_id; r.h.al = 0x00;
        int86(0x2F, &r, &r);
        if (r.h.al == 0xFF) {
            printf("OPL4SYN: something already answers INT 2Fh id %02X.\r\n", mux_id);
            return 1;
        }
    }

    /* Bring the OPL4 up.  The NEW2 gate STAYS OPEN for the TSR's lifetime -
     * the engine writes wave registers whenever a byte arrives.  A closed
     * gate floats (and has floated to the DevID's own value), so the check
     * only means anything with the gate freshly opened. */
    new2(1);
    id = wv_get(0x02);
    if ((id & 0xF0) != 0x20) {
        printf("DevID %02X: no OPL4 at %03X - run VEW21XGO first.\r\n", id, BASE);
        new2(0);
        return 1;
    }
    wv_put(0x02, 0x00);                                  /* sound generation */
    if (mix < 0) mix = 0; if (mix > 7) mix = 7;
    wv_put(0xF9, (unsigned char)(((mix & 7) << 3) | (mix & 7)));
    syn_reset();

    prev2f = _dos_getvect(0x2F);
    _dos_setvect(0x2F, int2f_handler);

    printf("OPL4SYN: OPL4 at %03X, INT 2Fh id %02X - resident.\r\n", BASE, mux_id);
    printf("Feed it with MPUSHIM /SYNTH%s, or OPL4SYN /TEST.\r\n",
           mux_id == 0xBD ? "" : "=xx");

    keep = get_ss() + ((get_sp() + 15) >> 4) + 1 - _psp;
    _dos_keep(0, keep);
    return 0;                                            /* not reached */
}

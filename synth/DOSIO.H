/* dosio - the handful of DOS calls a resident synth needs, done directly.
 *
 * OPL4SYN stays resident, so every byte the C library links in stays
 * resident with it, and stdio is the largest tenant by far: printf's
 * formatter and the FILE layer dwarf the synthesis code they support, in
 * aid of a dozen messages with a few integers in them.  None of it is
 * needed - output is one DOS write, and the command line wants two number
 * parsers.  Host builds map the same names onto stdio so anything using
 * this header can still be run off-target.
 *
 * Trimmed from the same header in the mc8kgo repo (TDKSYN), which needs
 * file access as well; this card's tables are compiled in, so the file
 * half is left out.  Both are ours, GPL v2.
 */
#ifndef DOSIO_H
#define DOSIO_H

#ifdef __WATCOMC__

/* every program including this uses some of these and not others */
#pragma off (unreferenced)

#include <dos.h>

static void o_str(char *s)
{
    unsigned n = 0, put;
    while (s[n]) n++;
    if (n) _dos_write(1, s, n, &put);
}

#else   /* host build */

#include <stdio.h>
static void o_str(char *s) { fputs(s, stdout); }

#endif

/* ---- the formatting the messages actually use ---- */

static void o_u(unsigned v)             /* unsigned decimal */
{
    char b[8];
    int i = 7;
    b[i] = '\0';
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    o_str(b + i);
}

static void o_x(unsigned v, int digits) /* zero-padded uppercase hex */
{
    char b[8];
    int i = digits;
    b[i] = '\0';
    while (i > 0) {
        int d = (int)(v & 0x0F);
        b[--i] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        v >>= 4;
    }
    o_str(b);
}

/* ---- and the parsing the command line wants ---- */

static unsigned p_hex(char *s)
{
    unsigned v = 0;
    for (;;) {
        char c = *s++;
        if      (c >= '0' && c <= '9') v = (v << 4) | (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (unsigned)(c - 'A' + 10);
        else return v;
    }
}

static int p_dec(char *s)
{
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

/* case-insensitive match of an uppercase keyword, n characters */
static int p_kw(char *s, char *k, int n)
{
    while (n--) {
        char a = *s++;
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (a != *k++) return 0;
    }
    return 1;
}

#endif

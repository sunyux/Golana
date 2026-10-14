/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele */
/* Golana's drawings (golana_ui.c): above the cards of the pages that have one, a picture of what the page sets, from
 * the values themselves, redrawn as they change:
 *   HOME       the sound playing, from the scope buffer (fx.c scope_buf: what goes out), three ways (GLO SETUP HOME):
 *              RING the wave wrapped round a circle, ORBIT the wave against itself a moment later (a phase portrait:
 *              every timbre draws its own figure), RIDGE the last waves stacked, the newest in front, SPHERE the
 *              notes played as stars on a pitch wheel, joined in the order played (from the Biosphere)
 *   OSC 1/2    a cycle and a half of the oscillator as its WAVE, CTRL 1, MOD and SEMITONE make it
 *   FILTER     the response, dB over a log frequency axis (30 Hz .. 16 kHz), the cutoff marked
 *   FILTER EG, AMP EG   the four stages, their lengths as the times, the sustain as its level
 *   LFO 1/2    the wave, as many cycles as its rate, running
 * Integer only (no FPU); the live ones (HOME, the LFOs) redraw every GU_LIVE_FRAMES frames, the others on a change. */

enum { GG_NONE, GG_HOME, GG_OSC1, GG_OSC2, GG_FILT, GG_FEG, GG_AEG, GG_LFO1, GG_LFO2 };
#define GU_LIVE_FRAMES 3u

static uint32_t gu_graph_of(const gu_page_t *pg)
{
    switch (pg->k[0].id) {
    case GL_CUTOFF: return pg->btn == B_HOME ? GG_HOME : GG_NONE;
    case GL_O1WAVE: return GG_OSC1;
    case GL_O2WAVE: return GG_OSC2;
    case GL_FTYPE: return GG_FILT;
    case GL_FA: return GG_FEG;
    case P_ATK: return GG_AEG;
    case GL_L1WAVE: return GG_LFO1;
    case GL_L2WAVE: return GG_LFO2;
    default: return GG_NONE;
    }
}
static int32_t gu_graph_h(uint32_t g) { return g == GG_HOME ? 84 : g ? 72 : 0; }

/* a polyline of n points (x[], y[]) 2 px thick */
static void gu_poly(const int16_t *x, const int16_t *y, uint32_t n, uint16_t c)
{
    uint32_t i;
    for (i = 1; i < n; i++) {
        cv_line(x[i - 1], y[i - 1], x[i], y[i], c);
        cv_line(x[i - 1] + 1, y[i - 1], x[i] + 1, y[i], c);
    }
}
/* a curve over the columns x0..x0+n-1 (y[] per column), with the area under it down to yb tinted */
static void gu_curve(int32_t x0, const int16_t *y, uint32_t n, int32_t yb, uint16_t c, uint16_t tint)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (y[i] < yb)
            cv_rect(x0 + (int32_t)i, y[i], 1, yb - y[i], tint);
    for (i = 1; i < n; i++) {
        cv_line(x0 + (int32_t)i - 1, y[i - 1], x0 + (int32_t)i, y[i], c);
        cv_line(x0 + (int32_t)i - 1, y[i - 1] + 1, x0 + (int32_t)i, y[i] + 1, c);
    }
}

/* ------------------------------------------------------------------ HOME --- */
static struct {
    int32_t gain;                 /* the auto gain, Q8 (the scope's quiet sounds made visible) */
    int16_t ridge[7][56];         /* RIDGE: the last waves, [0] the oldest */
    uint8_t nridge, tick;
} gh;

/* the last n scope samples from a rising zero crossing (a still picture of a steady wave) -> s[]; their peak */
static int32_t gu_scope_take(int16_t *s, uint32_t n)
{
    uint32_t w = scope_w, i, start = w - n - 1u, best = start;
    int32_t pk = 0;
    for (i = 0; i < 160u; i++) {                       /* a rising crossing in the 160 samples before */
        uint32_t a = start - i;
        if (scope_buf[(a - 1u) & (SCOPE_N - 1u)] < 0 && scope_buf[a & (SCOPE_N - 1u)] >= 0) {
            best = a;
            break;
        }
    }
    for (i = 0; i < n; i++) {
        s[i] = scope_buf[(best + i) & (SCOPE_N - 1u)];
        pk = s[i] > pk ? s[i] : -s[i] > pk ? -s[i] : pk;
    }
    return pk;
}
/* the scale for peak pk, Q8, glided (louder at once, quieter slowly): a full picture whatever the level */
static int32_t gu_scope_gain(int32_t pk)
{
    int32_t want = pk > 400 ? (24000 << 8) / pk : (24000 << 8) / 400;
    if (want > 16 << 8)
        want = 16 << 8;
    gh.gain = !gh.gain || want < gh.gain ? want : gh.gain + ((want - gh.gain) >> 4);
    return gh.gain;
}

static void gu_home_ring(int32_t w, int32_t h, uint16_t acc, uint16_t surf)
{
    int16_t s[128], x[129], y[129];
    int32_t g = gu_scope_gain(gu_scope_take(s, 128)), cx = w / 2, cy = h / 2, r0 = h / 2 - 16, i;
    uint32_t a;
    for (a = 0; a < 64u; a++) {                        /* the circle at rest, dotted */
        uint32_t ph = a << 26;
        cv_pset(cx + (r0 * osc_sine(ph + 0x40000000u) >> 15), cy + (r0 * osc_sine(ph) >> 15), ux_mix(surf, acc, 40));
    }
    for (i = 0; i <= 128; i++) {
        int32_t v = clamp((s[i & 127] * g) >> 8, -32767, 32767), r = r0 + (v * 13 >> 15);
        uint32_t ph = (uint32_t)i << 25;               /* 128 points: a whole turn */
        x[i] = (int16_t)(cx + (r * osc_sine(ph + 0x40000000u) >> 15));
        y[i] = (int16_t)(cy + (r * osc_sine(ph) >> 15));
    }
    gu_poly(x, y, 129, acc);
}

static void gu_home_orbit(int32_t w, int32_t h, uint16_t acc, uint16_t surf)
{
    int16_t s[200], x[192], y[192];
    int32_t g = gu_scope_gain(gu_scope_take(s, 200)), cx = w / 2, cy = h / 2, k = h / 2 - 6, i;
    cv_rect(cx - k, cy, 2 * k, 1, ux_mix(surf, acc, 25));
    cv_rect(cx, cy - k, 1, 2 * k, ux_mix(surf, acc, 25));
    for (i = 0; i < 192; i++) {                        /* x: the wave now, y: 8 samples (0.36 ms) later */
        int32_t a = clamp((s[i] * g) >> 8, -32767, 32767), b = clamp((s[i + 8] * g) >> 8, -32767, 32767);
        x[i] = (int16_t)(cx + (a * (k + 20) >> 15));
        y[i] = (int16_t)(cy - (b * k >> 15));
    }
    gu_poly(x, y, 96, ux_mix(surf, acc, 55));          /* the older half fainter */
    gu_poly(x + 95, y + 95, 97, acc);
}

static void gu_home_ridge(int32_t w, int32_t h, uint16_t acc, uint16_t surf)
{
    int16_t s[224], y[56];
    int32_t g, j, i, step = (h - 24) / 6, amp = step + 6;
    if (!(gh.tick++ & 1u) || !gh.nridge) {             /* a new wave every other drawing */
        g = gu_scope_gain(gu_scope_take(s, 224));
        if (gh.nridge == 7u)
            for (j = 0; j < 6; j++)                     /* (no memmove in the firmware's libc) */
                memcpy(gh.ridge[j], gh.ridge[j + 1], sizeof gh.ridge[0]);
        else
            gh.nridge++;
        for (i = 0; i < 56; i++)
            gh.ridge[gh.nridge - 1u][i] = (int16_t)clamp((s[i * 4] * g) >> 8, -32767, 32767);
    }
    for (j = 0; j < (int32_t)gh.nridge; j++) {         /* the oldest at the top, behind; each one hides what is under */
        int32_t base = 14 + (j + 7 - (int32_t)gh.nridge) * step;
        uint16_t c = ux_mix(surf, acc, 30 + j * 10);
        for (i = 0; i < 56; i++) {
            int32_t v = gh.ridge[j][i], env = i < 8 ? i : i > 47 ? 55 - i : 8;   /* the ends calm, as the cover's */
            y[i] = (int16_t)(base - ((v < 0 ? -v : v) * amp * env >> 18) - (v * amp >> 17));
        }
        for (i = 0; i < 55; i++) {
            int32_t x0 = 8 + i * (w - 16) / 55, x1 = 8 + (i + 1) * (w - 16) / 55, xx;
            for (xx = x0; xx < x1; xx++) {
                int32_t yy = y[i] + (y[i + 1] - y[i]) * (xx - x0) / (x1 - x0);
                cv_rect(xx, yy, 1, h - yy, surf);      /* (the painter's way: the band under the line hidden) */
            }
            cv_line(x0, y[i], x1, y[i + 1], c);
            cv_line(x0, y[i] + 1, x1, y[i + 1] + 1, c);
        }
    }
}

/* SPHERE (after the Biosphere, github.com/sunyux/biosphere37): the notes as they start (eng_golana.c gb_note_q) as
 * stars on a pitch wheel, each pitch class its own spoke (C at the top, clockwise) and each octave further out; the
 * last 12 joined in the order played, the newest a ringed planet with a shockwave; a little spiral galaxy turns in
 * the middle and the sky twinkles. Rules first: the same note always lands on the same star. And the sound itself
 * (the scope, as RING draws it): a static of coloured sparks that thickens with the level, the galaxy turning faster,
 * the stars riding the wave. And a wanderer, the ringed planet, travels from star to star, a leg at a time (to the
 * newest note when one is played), drawing the wave along its way like an oscilloscope's beam, the last legs fading
 * behind as an afterglow; the wave's peaks throw sparks off the beam that slow down and linger as little stars, and
 * round the wanderer the wave is drawn against itself a moment later (XY, as ORBIT). */
#define GB_N 12u
#define GB_TRAIL 4u                /* the beams of the last legs, fading: the scope's afterglow */
#define GB_P 48u                   /* the sparks the wave throws off */
static struct {
    uint8_t note[GB_N], n;        /* [0] the oldest */
    uint32_t r, rot, tick;        /* gb_note_w read so far, the galaxy's turn, drawings */
    uint16_t since;               /* drawings since the newest note */
    int16_t fx, fy, px, py;       /* the wanderer: from (fx, fy), now at (px, py) */
    uint8_t to, tt;               /* ... towards star `to`, tt of 64 along the way */
    int16_t bx[GB_TRAIL][33], by[GB_TRAIL][33];   /* the finished beams, [0] the newest */
    uint8_t nb, pw;
    struct { int16_t x, y, vx, vy; uint8_t life, pc; } p[GB_P];   /* sparks: Q4 position and speed */
} gb;

/* the 12 pitch classes round the palette's nine button colours */
static uint16_t gb_col(uint32_t pc)
{
    uint32_t a = pc * 3u / 4u;
    return ux_mix(GU_P->btn[a % 9u], GU_P->btn[(a + 1u) % 9u], (int32_t)((pc * 3u) % 4u) * 25);
}
/* a point on an ellipse of radii rx, ry round (cx, cy), at phase ph */
static void gb_at(uint32_t ph, int32_t rx, int32_t ry, int32_t cx, int32_t cy, int32_t *x, int32_t *y)
{
    *x = cx + (rx * osc_sine(ph + 0x40000000u) >> 15);
    *y = cy + (ry * osc_sine(ph) >> 15);
}
/* note's star: its pitch class the spoke, its octave the ring (C4 at 76 % of the outer one, C2 60 %, C7 100 %) */
static void gb_star(uint32_t note, int32_t cx, int32_t cy, int32_t *x, int32_t *y)
{
    int32_t s = 76 + clamp((int32_t)note / 12 - 5, -2, 3) * 8;
    gb_at((note % 12u) * 0x15555555u - 0x40000000u, 98 * s / 100, 31 * s / 100, cx, cy, x, y);
}

static void gu_home_sphere(int32_t w, int32_t h, uint16_t acc, uint16_t surf)
{
    static const uint8_t DISC[9] = {2, 3, 4, 4, 4, 4, 4, 3, 2};   /* the planet's half-widths, row by row */
    int16_t wave[72], sx[GB_N], sy[GB_N], cbx[33], cby[33];
    int32_t cx = w / 2, cy = h / 2, i, x0, y0, x1, y1, g, loud, amp;
    uint32_t wr = gb_note_w, k, rnd;
    if (wr - gb.r > 16u)                               /* (a burst longer than the ring: its last 16) */
        gb.r = wr - 16u;
    while (gb.r != wr) {
        uint8_t nt = gb_note_q[gb.r++ & 15u];
        if (gb.n && gb.since == 0u && gb.note[gb.n - 1u] == nt)
            continue;                                  /* (UNISON: one note starts several voices at once) */
        if (gb.n == GB_N) {
            for (k = 0; k + 1u < GB_N; k++)
                gb.note[k] = gb.note[k + 1u];
            gb.n--;
            if (gb.to)
                gb.to--;
        }
        gb.note[gb.n++] = nt;
        gb.since = 0;
        if (gb.n == 1u) {                              /* the first note: the wanderer appears on its star */
            gb_star(nt, cx, cy, &x0, &y0);
            gb.px = gb.fx = (int16_t)x0;
            gb.py = gb.fy = (int16_t)y0;
        } else {                                       /* a new note: off to it, from wherever it is */
            gb.fx = gb.px;
            gb.fy = gb.py;
        }
        gb.to = (uint8_t)(gb.n - 1u);
        gb.tt = 0;
    }
    if (gb.since < 999u)
        gb.since++;
    gb.tick++;
    {                                                  /* the level: 0 (silence) .. 255 (full scale) */
        int32_t pk = gu_scope_take(wave, 72);
        g = gu_scope_gain(pk);
        loud = clamp(pk >> 6, 0, 255);
    }
    for (i = 0; i < 72; i++)                           /* the wave, scaled to full height */
        wave[i] = (int16_t)clamp((wave[i] * g) >> 8, -32767, 32767);
    gb.rot += 0x00A00000u + (uint32_t)loud * 0x00030000u;
    rnd = gb.tick * 2654435761u;
    amp = loud > 4 ? 10 + loud / 4 : 0;                /* how far the wave throws things: up to ~73 px */

    for (i = 0; i < 24 + loud * 3; i++) {              /* the static: sparks in the notes' colours, more as it is louder */
        int32_t qx, qy;
        rnd = rnd * 1664525u + 1013904223u;
        qx = 6 + (int32_t)((rnd >> 8) % (uint32_t)(w - 12));
        qy = 4 + (int32_t)((rnd >> 20) % (uint32_t)(h - 8));
        cv_pset(qx, qy, ux_mix(surf, gb_col((rnd >> 4) % 12u), 25 + (int32_t)((rnd >> 27) * 8u)));
        if ((rnd & 15u) == 0u && loud > 48)            /* (now and then a brighter cross) */
            cv_rect(qx - 1, qy, 3, 1, ux_mix(surf, GC_TEXT, 60)), cv_rect(qx, qy - 1, 1, 3, ux_mix(surf, GC_TEXT, 60));
    }
    for (i = 0; i < 56; i++) {                         /* the sky: fixed stars, each twinkling at its own pace */
        uint32_t hs = (uint32_t)i * 2654435761u, tw = ((hs >> 4) + gb.tick * (1u + (hs & 3u))) & 63u;
        int32_t b = tw < 32u ? (int32_t)tw : 63 - (int32_t)tw;
        cv_pset(8 + (int32_t)((hs >> 8) % (uint32_t)(w - 16)), 4 + (int32_t)((hs >> 20) % (uint32_t)(h - 8)),
                ux_mix(surf, GC_TEXT, 14 + b * 2));
    }
    for (i = 0; i < 84; i++) {                         /* the galaxy: three arms winding out, turning */
        int32_t j = i / 3, r = 2 + j;
        gb_at(gb.rot + (uint32_t)j * 0x0A000000u + (uint32_t)(i % 3) * 0x55555555u, r + (r >> 2), r * 40 / 100, cx, cy,
              &x0, &y0);
        cv_pset(x0, y0, ux_mix(surf, ux_mix(GC_TEXT, GC_AMBER, 40), 90 - j * 2));
    }
    cv_rect(cx - 1, cy - 1, 3, 2, GC_TEXT);
    for (k = 0; k < 12u; k++) {                        /* the wheel: a tick per pitch class, in its colour */
        gb_at(k * 0x15555555u - 0x40000000u, 101, 33, cx, cy, &x0, &y0);
        cv_rect(x0 - 1, y0, 3, 1, ux_mix(surf, gb_col(k), 45));
    }
    if (!gb.n) {
        cv_text(10, 4, &AF_S, "PLAY A KEY", ux_mix(surf, GC_TEXT, 45));
        return;
    }

    for (i = 0; i < (int32_t)gb.n; i++) {              /* the stars ride the wave: each its own moment of it */
        gb_star(gb.note[i], cx, cy, &x0, &y0);
        sx[i] = (int16_t)(x0 + (wave[(i * 11) % 72] * amp / 2 >> 15));
        sy[i] = (int16_t)(y0 + (wave[(i * 11 + 9) % 72] * amp / 3 >> 15));
    }

    /* the wanderer: from star to star, a leg at a time, faster when loud; then on to another one */
    gb.tt = (uint8_t)clamp((int32_t)gb.tt + 2 + loud / 24, 0, 64);
    {
        int32_t t = gb.tt, e = t * t * (192 - 2 * t) / 4096;   /* ease in and out, 0..64 */
        gb.px = (int16_t)(gb.fx + (sx[gb.to] - gb.fx) * e / 64);
        gb.py = (int16_t)(gb.fy + (sy[gb.to] - gb.fy) * e / 64);
    }
    {   /* the beam: the wave drawn along the way so far, across it, as an oscilloscope's trace */
        int32_t dx = gb.px - gb.fx, dy = gb.py - gb.fy, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        int32_t len = (ax > ay ? ax + ay / 2 : ay + ax / 2) + 1, nx = -dy * 256 / len, ny = dx * 256 / len;
        if (len < 3) { nx = 0; ny = 256; }
        for (k = 0; k <= 32u; k++) {
            int32_t d = wave[k * 2u] * amp >> 15;
            cbx[k] = (int16_t)(gb.fx + dx * (int32_t)k / 32 + (nx * d >> 8));
            cby[k] = (int16_t)(gb.fy + dy * (int32_t)k / 32 + (ny * d >> 9));
            if ((k & 1u) && loud > 16 && (d > amp / 3 || d < -amp / 3) && ((rnd >> (k & 31u)) & 1u)) {
                uint32_t q = gb.pw++ % GB_P;          /* a peak throws a spark off the beam */
                int32_t sp = 16 + (d < 0 ? -d : d);   /* (Q4: px/16 per drawing) */
                gb.p[q].x = (int16_t)(cbx[k] << 4);
                gb.p[q].y = (int16_t)(cby[k] << 4);
                gb.p[q].vx = (int16_t)((d < 0 ? -nx : nx) * sp >> 8);
                gb.p[q].vy = (int16_t)((d < 0 ? -ny : ny) * sp >> 9);
                gb.p[q].life = (uint8_t)(48 + (rnd >> 24) % 48u);
                gb.p[q].pc = (uint8_t)(gb.note[gb.to] % 12u);
            }
        }
    }
    if (gb.tt >= 64u) {                                /* arrived: the beam stays a while, the next star is picked */
        for (k = GB_TRAIL - 1u; k > 0u; k--) {
            memcpy(gb.bx[k], gb.bx[k - 1u], sizeof gb.bx[0]);
            memcpy(gb.by[k], gb.by[k - 1u], sizeof gb.by[0]);
        }
        memcpy(gb.bx[0], cbx, sizeof cbx);
        memcpy(gb.by[0], cby, sizeof cby);
        if (gb.nb < GB_TRAIL)
            gb.nb++;
        gb.fx = gb.px;
        gb.fy = gb.py;
        gb.tt = 0;
        if (gb.n > 1u)
            gb.to = (uint8_t)((gb.to + 1u + (rnd >> 16) % (gb.n - 1u)) % gb.n);
    }

    for (k = gb.nb; k-- > 0u;)                          /* the afterglow: the last legs' beams, fading */
        for (i = 1; i <= 32; i++)
            cv_line(gb.bx[k][i - 1], gb.by[k][i - 1], gb.bx[k][i], gb.by[k][i], ux_mix(surf, acc, 40 - (int32_t)k * 9));
    for (i = 1; i < (int32_t)gb.n; i++)                /* the route, faint now: the order played */
        cv_line(sx[i - 1], sy[i - 1], sx[i], sy[i], ux_mix(surf, gb_col(gb.note[i] % 12u), 18 + 40 * i / ((int32_t)gb.n - 1)));
    for (i = 0; i < (int32_t)gb.n; i++)                /* the stars */
        cv_rect(sx[i] - 1, sy[i] - 1, 3, 3, ux_mix(surf, gb_col(gb.note[i] % 12u), 50 + 50 * (i + 1) / (int32_t)gb.n));
    for (k = 0; k < GB_P; k++) {                       /* the sparks: fly out, slow down, linger as little stars */
        if (!gb.p[k].life)
            continue;
        gb.p[k].x += gb.p[k].vx;
        gb.p[k].y += gb.p[k].vy;
        gb.p[k].vx = (int16_t)(gb.p[k].vx * 7 / 8);
        gb.p[k].vy = (int16_t)(gb.p[k].vy * 7 / 8);
        gb.p[k].life--;
        x0 = gb.p[k].x >> 4;
        y0 = gb.p[k].y >> 4;
        if (gb.p[k].life > 70u)
            cv_rect(x0, y0, 2, 2, ux_mix(gb_col(gb.p[k].pc), GC_TEXT, 40));
        else
            cv_pset(x0, y0, ux_mix(surf, gb_col(gb.p[k].pc), 20 + gb.p[k].life));
    }
    {
        uint16_t c = gb_col(gb.note[gb.to] % 12u);
        for (i = 1; i <= 32; i++) {                    /* the beam being drawn, bright */
            cv_line(cbx[i - 1], cby[i - 1], cbx[i], cby[i], ux_mix(c, GC_TEXT, 45));
            cv_line(cbx[i - 1] + 1, cby[i - 1], cbx[i] + 1, cby[i], ux_mix(c, GC_TEXT, 20));
            cv_line(cbx[i - 1], cby[i - 1] + 1, cbx[i], cby[i] + 1, ux_mix(surf, c, 70));
        }
    }
    {
        uint32_t nt = gb.note[gb.n - 1u];
        uint16_t c = gb_col(gb.note[gb.to] % 12u);
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        char name[8];
        if (gb.since < 24u)                            /* the shockwave of a new note, on its star */
            for (k = 0; k < 48u; k++) {
                gb_at(k * 0x05555555u, 6 + (int32_t)gb.since * 2, 3 + (int32_t)gb.since, sx[gb.n - 1u], sy[gb.n - 1u],
                      &x1, &y1);
                cv_pset(x1, y1, ux_mix(surf, gb_col(nt % 12u), 80 - (int32_t)gb.since * 3));
            }
        x0 = gb.px;
        y0 = gb.py;
        if (loud > 8) {                                /* round the wanderer: the wave against itself a moment later (XY) */
            int32_t r = 10 + loud / 5;
            for (k = 1; k < 64u; k++)
                cv_line(x0 + (wave[k - 1u] * r >> 15), y0 - (wave[k + 7u] * r / 2 >> 15),
                        x0 + (wave[k] * r >> 15), y0 - (wave[k + 8u] * r / 2 >> 15), ux_mix(surf, c, 55 + loud / 6));
        }
        for (k = 0; k < 48u; k++) {                    /* the ring's back half, behind the planet */
            gb_at(k * 0x05555555u, 9, 3, x0, y0, &x1, &y1);
            if (y1 < y0)
                cv_pset(x1, y1, ux_mix(c, GC_TEXT, 45));
        }
        for (i = 0; i < 9; i++)                        /* the planet */
            cv_rect(x0 - DISC[i], y0 - 4 + i, 2 * DISC[i] + 1, 1, i < 3 ? ux_mix(c, GC_TEXT, 30) : c);
        for (k = 0; k < 48u; k++) {                    /* the ring's front half */
            gb_at(k * 0x05555555u, 9, 3, x0, y0, &x1, &y1);
            if (y1 >= y0)
                cv_pset(x1, y1, ux_mix(c, GC_TEXT, 45));
        }
        str_cpy(name, NN[nt % 12u], 4);                /* the newest note, named (C4 = 60) */
        fmt_int(name + str_len(name), (int32_t)nt / 12 - 1);
        cv_text(10, 4, &AF_S, name, gb_col(nt % 12u));
    }
}

/* -------------------------------------------------------------- OSC 1 / 2 --- */
static int32_t gu_osc1_at(const int16_t *p, uint32_t ph, uint32_t ph2)
{
    int32_t c1 = clamp(p[GL_O1C1], 0, 127);
    switch (clamp(p[GL_O1WAVE], 0, 3)) {
    case 1:
        return ph < 0x80000000u + (uint32_t)c1 * 0xF00000u ? 26000 : -26000;
    case 2: {
        int32_t u = (osc_tri(ph) * ((32768 + c1 * 768) >> 4)) >> 11;
        u = (int32_t)((uint32_t)(u + 32768) & 0x1FFFFu);
        return u < 65536 ? u - 32768 : 98303 - u;
    }
    case 3:
        return osc_sine(ph + (((uint32_t)gl_osc2(0, ph2, 0) * (uint32_t)(c1 * 380)) << 1));
    default: {
        int32_t a = (int32_t)(ph >> 16) - 32768, b = (int32_t)((ph << 1) >> 16) - 32768;
        return a + (((b - a) * c1) >> 8);
    }
    }
}
static void gu_osc(int32_t w, int32_t h, uint32_t two, uint16_t acc, uint16_t surf)
{
    const int16_t *p = gu_t()->p;
    int16_t y[224];
    int32_t i, cy = h / 2, k = h / 2 - 8;
    int32_t semi = clamp(p[GL_O2SEMI], -24, 24), md = clamp(p[GL_O2MOD], 0, 3);
    uint32_t r2 = pitch_inc((uint32_t)(60 * 16 + semi * 16)), r1 = pitch_inc(60u * 16u);
    cv_rect(8, cy, w - 16, 1, ux_mix(surf, acc, 25));
    {
        uint32_t ratio = (r2 >> 8) * 256u / ((r1 >> 16) | 1u);   /* OSC 2 / OSC 1, Q16 (32-bit: no 64-bit divide) */
        for (i = 0; i < 224; i++) {                    /* 1.5 cycles of OSC 1 */
            uint64_t tot = (uint64_t)(uint32_t)i * (0xFFFFFFFFu / 149u);
            uint32_t ph = (uint32_t)tot, ph2;
            int32_t v;
            ph2 = md >= 2 ? (uint32_t)(((uint64_t)ph * ratio) >> 16)     /* SYNC: from OSC 1's start, every cycle */
                          : (uint32_t)((tot * ratio) >> 16);
            if (!two) {
                v = gu_osc1_at(p, ph, ph2);
            } else {
                v = gl_osc2((uint32_t)clamp(p[GL_O2WAVE], 0, 2), ph2, 0);
                if (md == 1 || md == 3)
                    v = mulq15(v, gu_osc1_at(p, ph, ph2));
            }
            y[i] = (int16_t)(cy - (clamp(v, -32767, 32767) * k >> 15));
        }
    }
    for (i = 1; i < 224; i++) {
        cv_line(8 + i - 1, y[i - 1], 8 + i, y[i], acc);
        cv_line(8 + i - 1, y[i - 1] + 1, 8 + i, y[i] + 1, acc);
    }
}

/* ----------------------------------------------------------------- FILTER --- */
static void gu_filter(int32_t w, int32_t h, uint16_t acc, uint16_t surf)
{
    const int16_t *p = gu_t()->p;
    int16_t y[224];
    int32_t ft = clamp(p[GL_FTYPE], 0, 4), cut = clamp(p[GL_CUTOFF], 0, 127), res = clamp(p[GL_RESO], 0, 127);
    int32_t xc = cut * 223 / 127, i, top = 8, zero = top + 18;   /* 0 dB at zero; 1 px = 1 dB */
    int32_t peak = ft == 4 ? res * res / 420 : res * res / 640;  /* the resonance, dB */
    int32_t slope = ft == 0 ? 24 : 12;                  /* dB per octave; 26.6 px an octave */
    for (i = 0; i < 6; i++)                             /* octave lines */
        cv_rect(8 + i * 40, top, 1, h - top - 6, ux_mix(surf, acc, 12));
    cv_rect(8, zero, w - 16, 1, ux_mix(surf, acc, 25));
    for (i = 0; i < 224; i++) {
        int32_t d = i - xc, db, bump;
        bump = d > -14 && d < 14 ? peak * (196 - d * d) / 196 : 0;   /* the resonance: a hump round the cutoff */
        if (ft == 3)                                    /* HPF */
            db = (d < 0 ? d * slope * 10 / 266 : 0) + bump;
        else if (ft == 2)                               /* BPF: 6 dB / octave each side, its peak the resonance */
            db = -(d < 0 ? -d : d) * 60 / 266 + bump - 6 + res / 12;
        else
            db = (d > 0 ? -d * slope * 10 / 266 : 0) + bump;
        y[i] = (int16_t)clamp(zero - db, top, h - 6);
    }
    gu_curve(8, y, 224, h - 6, acc, ux_mix(surf, acc, 16));
    cv_rect(8 + xc, top, 1, h - top - 6, ux_mix(surf, acc, 60));
}

/* ----------------------------------------------------------------- the EGs --- */
static void gu_eg(int32_t w, int32_t h, uint32_t amp, uint16_t acc, uint16_t surf)
{
    const int16_t *p = gu_t()->p;
    int16_t y[224];
    int32_t a = clamp(p[amp ? P_ATK : GL_FA], 0, 127), d = clamp(p[amp ? P_DEC : GL_FD], 0, 127);
    int32_t s = clamp(p[amp ? P_SUS : GL_FS], 0, 127), r = clamp(p[amp ? P_REL : GL_FR], 0, 127);
    int32_t wa = 2 + a * 56 / 127, wd = 3 + d * 56 / 127, wr = 3 + r * 56 / 127, ws, top = 8, bot = h - 8, i, n = 0;
    int32_t yt = bot - top, ys = bot - s * yt / 127, lv;
    ws = 224 - wa - wd - wr;
    for (i = 0; i < wa && n < 224; i++)                 /* attack: linear */
        y[n++] = (int16_t)(bot - (i + 1) * yt / wa);
    lv = 32767;                                         /* decay, release: exponential, ~99 % over their length */
    for (i = 0; i < wd && n < 224; i++) {
        lv -= lv * 150 / (wd * 32 + 1);
        y[n++] = (int16_t)(ys - (ys - top) * lv / 32767);
    }
    for (i = 0; i < ws && n < 224; i++)
        y[n++] = (int16_t)ys;
    lv = 32767;
    for (i = 0; i < wr && n < 224; i++) {
        lv -= lv * 150 / (wr * 32 + 1);
        y[n++] = (int16_t)(bot - (bot - ys) * lv / 32767);
    }
    while (n < 224)
        y[n++] = (int16_t)bot;
    gu_curve(8, y, 224, bot, acc, ux_mix(surf, acc, 16));
    cv_rect(8 + wa + wd, top, 1, bot - top, ux_mix(surf, acc, 35));            /* the key let go */
    cv_rect(8 + wa + wd + ws, top, 1, bot - top, ux_mix(surf, acc, 35));
}

/* ------------------------------------------------------------------ LFOs --- */
static uint32_t gu_lfo_ph[2], gu_lfo_n[2];
static void gu_lfo(int32_t w, int32_t h, uint32_t two, uint16_t acc, uint16_t surf)
{
    const int16_t *p = gu_t()->p;
    int16_t y[224];
    uint32_t wave = (uint32_t)clamp(p[two ? GL_L2WAVE : GL_L1WAVE], 0, 4), i;
    int32_t f = clamp(p[two ? GL_L2FREQ : GL_L1FREQ], 0, 127), sync = p[two ? GL_L2SYNC : GL_L1SYNC];
    uint32_t cyc = sync ? 2u : 1u + (uint32_t)f / 32u, inc = 0xFFFFFFFFu / 224u * cyc;
    int32_t cy = h / 2, k = h / 2 - 9;
    {
        uint32_t old = gu_lfo_ph[two];
        gu_lfo_ph[two] += LFO_INC[f] * 8u;              /* it runs (a slow LFO slowly) */
        if (gu_lfo_ph[two] < old)
            gu_lfo_n[two]++;
    }
    cv_rect(8, cy, w - 16, 1, ux_mix(surf, acc, 25));
    {
        uint32_t prev = gu_lfo_ph[two], c = gu_lfo_n[two];
        for (i = 0; i < 224u; i++) {
            uint32_t ph = gu_lfo_ph[two] + i * inc;
            if (ph < prev)
                c++;                                    /* a new cycle: S&H a new value */
            prev = ph;
            y[i] = (int16_t)(cy - (gl_wave(wave, ph, (int32_t)((c * 2654435761u) >> 16) - 32768) * k >> 15));
        }
    }
    for (i = 1; i < 224u; i++) {
        cv_line(8 + (int32_t)i - 1, y[i - 1], 8 + (int32_t)i, y[i], acc);
        cv_line(8 + (int32_t)i - 1, y[i - 1] + 1, 8 + (int32_t)i, y[i] + 1, acc);
    }
}

/* the page's drawing (y 60 .. 60 + its height), when something it shows changed or it is a live one */
static uint32_t gu_sig_graph, gu_frame;
static void gu_draw_graph(uint32_t g)
{
    const int16_t *p = gu_t()->p;
    int32_t h = gu_graph_h(g), w = 240;
    uint16_t acc = gu_btn_col(GU_PAGES[gu.page].btn), surf = GC_SURF;
    uint32_t sig = 0x811C9DC5u, live = g == GG_HOME || g == GG_LFO1 || g == GG_LFO2, i;
    static const uint8_t IDS[] = {GL_O1WAVE, GL_O1C1, GL_O2WAVE, GL_O2MOD, GL_O2SEMI, GL_FTYPE, GL_CUTOFF, GL_RESO,
                                  GL_FA, GL_FD, GL_FS, GL_FR, P_ATK, P_DEC, P_SUS, P_REL, GL_L1WAVE, GL_L1FREQ, GL_L1SYNC,
                                  GL_L2WAVE, GL_L2FREQ, GL_L2SYNC};
    gu_frame++;
    for (i = 0; i < sizeof IDS; i++)
        sig = gu_hashv(sig, p[IDS[i]]);
    sig = gu_hashv(sig, (int32_t)(g | gu_pi << 8 | gu_view << 12));
    if (live ? gu_frame % GU_LIVE_FRAMES && sig == gu_sig_graph : sig == gu_sig_graph)
        return;
    gu_sig_graph = sig;
    cv_begin((uint32_t)w, (uint32_t)h, GC_BG);
    cv_rrect(3, 0, w - 6, h, -10, surf, GC_BG);
    switch (g) {
    case GG_HOME:
        if (gu_view == 1u)
            gu_home_orbit(w, h, acc, surf);
        else if (gu_view == 2u)
            gu_home_ridge(w, h, acc, surf);
        else if (gu_view == 3u)
            gu_home_sphere(w, h, acc, surf);
        else
            gu_home_ring(w, h, acc, surf);
        break;
    case GG_OSC1: case GG_OSC2: gu_osc(w, h, g == GG_OSC2, acc, surf); break;
    case GG_FILT: gu_filter(w, h, acc, surf); break;
    case GG_FEG: case GG_AEG: gu_eg(w, h, g == GG_AEG, acc, surf); break;
    default: gu_lfo(w, h, g == GG_LFO2, acc, surf); break;
    }
    cv_blit(0, 60);
}

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele, based on Felucca's web/emu/native_check.c */
/* Golana on the simulated FM-1 (web/emu/felucca_web.c built natively): the whole firmware, its main loop and the
 * audio ISR, driven by a script of presses and turns; screenshots (PPM) of every page and the WRITE dialog, the
 * keys played, a program written and read back, and checks.
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/golana_sim tests/golana_sim.c -lm
 *   build/host/golana_sim OUTDIR */
#include <stdio.h>
#include "../web/emu/felucca_web.c"

static const char *dir = ".";
static int fails;
static float peak;

static void render(double ms)
{
    uint32_t n = (uint32_t)(ms * 44.1 + 0.5), k, i;
    for (k = 0; k < n; k += 128u) {
        uint32_t m = n - k < 128u ? n - k : 128u;
        web_render(m);
        for (i = 0; i < m; i++) {
            float a = out_l[i] < 0 ? -out_l[i] : out_l[i];
            if (a > peak)
                peak = a;
        }
    }
}

static void shot(const char *name)
{
    char path[256];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = (uint16_t)((web_fb[i] >> 8) | (web_fb[i] << 8));
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void press(uint32_t b)
{
    web_buttons(1u << b);
    render(40);
    web_buttons(0);
    render(40);
}

static void turn(uint32_t role, int32_t n)
{
    web_enc(role, n);
    render(40);
}

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        fails++;
    }
}

int main(int argc, char **argv)
{
    uint32_t p;
    char nm[64];
    if (argc > 1)
        dir = argv[1];
    web_nor_erase();
    web_boot();
    render(700);
    shot("00_boot");
    check(trk[0].eng_req == ENGI_GOLANA && trk[0].engine == ENGI_GOLANA, "part 1 plays GOLANA");
    check(gu.prog == 0 && gu.ready, "program 1 at power-on");
    /* every page, by SELECT */
    for (p = 0; p < GU_NPAGE; p++) {
        snprintf(nm, sizeof nm, "%02u_%s_%s", p + 1u, B_NAME[GU_PAGES[gu.page].btn], GU_PAGES[gu.page].tab);
        for (char *c = nm; *c; c++)
            if (*c == ' ')
                *c = '_';
        render(60);
        shot(nm);
        check(gu.page == p, "SELECT walks the pages");
        turn(EN_SELECT, 1);
    }
    check(gu.page == 0, "SELECT wraps");
    /* a key plays: part 1 sounds */
    peak = 0;
    web_keys(1u << 12);
    render(300);
    check(peak > 0.01f, "a key sounds");
    web_keys(0);
    render(800);
    /* the knobs: CUTOFF on PERFORM */
    {
        int16_t c0 = trk[0].p[GL_CUTOFF];
        turn(EN_K1, -5);
        check(trk[0].p[GL_CUTOFF] < c0, "KNOB 1 moves CUTOFF");
        check(gu.dirty, "an edit marks the program");
        render(100);
        shot("30_edited");
    }
    /* the buttons */
    press(B_EDIT);
    check(GU_PAGES[gu.page].btn == B_EDIT && gu.page == 1, "EDIT: OSC 1");
    press(B_EDIT);
    check(GU_PAGES[gu.page].btn == B_EDIT && gu.page == 2, "EDIT again: OSC 2");
    press(B_EDIT);
    press(B_EDIT);
    check(gu.page == 1, "EDIT wraps to OSC 1");
    press(B_FX);
    check(GU_PAGES[gu.page].btn == B_FX && !strcmp(GU_PAGES[gu.page].tab, "MOD FX"), "FX: MOD FX");
    press(B_ARP);
    check(trk[0].p[GL_ARPON] == 1 && trk[0].p[P_AMODE] != 0, "ARP on");
    render(100);
    shot("31_arp_on_toast");
    peak = 0;
    web_keys((1u << 0) | (1u << 4) | (1u << 7));
    render(1500);
    check(peak > 0.01f, "the arp plays");
    web_keys(0);
    press(B_ARP);
    check(trk[0].p[GL_ARPON] == 0 && trk[0].p[P_AMODE] == 0, "ARP off");
    render(1500);
    press(B_OCTUP);
    check(song.octave == 1, "OCT+");
    press(B_OCTDN);
    check(song.octave == 0, "OCT-");
    /* PRESETS: another program, back */
    turn(EN_PRESET, 3);
    check(gu.prog == 3 && !gu.dirty, "PRESETS loads program 4");
    render(100);
    shot("32_program4");
    turn(EN_PRESET, -3);
    check(gu.prog == 0, "PRESETS back to 1");
    /* WRITE: an edit into slot 20, named */
    turn(EN_K1, 0);
    press(B_HOME);
    turn(EN_K2, 7);                               /* RESO */
    {
        int16_t reso = trk[0].p[GL_RESO];
        press(B_SAVE);
        check(gu.save, "SAVE opens WRITE");
        turn(EN_PRESET, 19);
        check(gu.save_slot == 19, "PRESETS picks the slot");
        turn(EN_K2, 1);                           /* the first letter one on */
        render(100);
        shot("33_write");
        press(B_SAVE);
        check(!gu.save && gu.prog == 19, "SAVE writes");
        check(gu_user(19) && up_rec(19)->engine == ENGI_GOLANA, "slot 20 holds a Golana program");
        turn(EN_PRESET, -5);
        turn(EN_PRESET, 5);
        check(gu.prog == 19 && trk[0].p[GL_RESO] == reso, "the written program loads back");
        render(100);
        shot("34_written");
    }
    /* MIDI program change 2 -> program 3 */
    web_midi(0xC0, 2, 0);
    render(100);
    check(gu.prog == 2, "Program Change loads program 3");
    /* LFO: LFO 1, LFO 2, then PATCH 1 */
    press(B_LFO);
    press(B_LFO);
    press(B_LFO);
    check(GU_PAGES[gu.page].btn == B_LFO && GU_PAGES[gu.page].k[0].id == GL_P1SRC, "LFO third press: PATCH 1");
    render(80);
    shot("40_lfo_patch1");
    /* the step sequencer: REC, three keys entered, PLAY, it sounds, STOP; written and loaded back */
    press(B_REC);
    check(GU_PAGES[gu.page].btn == B_REC && GU_PAGES[gu.page].k[0].id == GV_SSTEP, "REC: the sequencer");
    {
        static const uint32_t K[3] = {0, 4, 7};
        uint32_t i;
        int16_t len;
        for (i = 0; i < 3u; i++) {
            web_keys(1u << K[i]);
            render(80);
            web_keys(0);
            render(80);
        }
        check(gu.sstep == 3 && trk[0].step[0].time == ST_NOTE && trk[0].step[2].time == ST_NOTE &&
              trk[0].step[1].note[0] == trk[0].step[0].note[0] + 4, "keys write steps 1..3 and move on");
        turn(EN_K1, 0);
        turn(EN_SELECT, 1);
        turn(EN_K1, -12);                         /* LENGTH 16 -> 4 */
        len = trk[0].p[P_SLEN];
        check(len == 4, "SETUP LENGTH 4");
        turn(EN_SELECT, -1);
        render(80);
        shot("41_rec_pattern");
        web_buttons(1u << B_PLAY);
        render(60);
        web_buttons(0);
        peak = 0;
        render(1500);
        check(song.playing && peak > 0.01f, "PLAY: the sequencer plays");
        render(10);
        shot("42_rec_playing");
        web_buttons(1u << B_PLAY);
        render(60);
        web_buttons(0);
        render(200);
        check(!song.playing, "PLAY again: stop");
        press(B_SAVE);
        turn(EN_PRESET, 30 - (int32_t)gu.save_slot);
        press(B_SAVE);
        check(gu.prog == 30 && up_rec(30)->note[0] == trk[0].step[0].note[0], "the pattern written with the program");
        turn(EN_PRESET, -1);
        check(trk[0].step[0].time == ST_REST, "another program: its own (empty) pattern");
        turn(EN_PRESET, 1);
        check(trk[0].step[1].time == ST_NOTE && trk[0].p[P_SLEN] == 4, "loaded back: the steps and LENGTH");
    }
    /* HOME · SPHERE: the notes played land as stars, joined in order; the same note twice in a row (UNISON) is one */
    {
        static const uint32_t K[6] = {0, 4, 7, 11, 14, 7};
        uint32_t i;
        press(B_HOME);
        gu_view = 3;
        ui.force = 1;
        render(120);
        shot("50_home_sphere_earlier");                /* (the keys played earlier in this script) */
        check(gb.n > 0 && gb.n <= GB_N, "SPHERE: the notes played before, at most 12");
        gb.n = 0;
        for (i = 0; i < 6u; i++) {
            web_keys(1u << K[i]);
            render(160);
            web_keys(0);
            render(120);
        }
        render(60);
        shot("51_home_sphere_phrase");
        check(gb.n == 6, "SPHERE: six keys, six stars");
        check(gb.note[1] == gb.note[0] + 4 && gb.note[5] == gb.note[2], "SPHERE: in the order played");
        render(900);
        shot("52_home_sphere_settled");
        for (i = 0; i < 4u && !(GU_PAGES[gu.page].btn == B_GLO && GU_PAGES[gu.page].k[2].id == GV_VIEW); i++)
            press(B_GLO);
        render(60);
        shot("53_glo_setup_sphere");
        check(GU_PAGES[gu.page].k[2].id == GV_VIEW && gu_view == 3, "GLO · SETUP · HOME shows SPHERE");
        gu_view = 2;
        ui.force = 1;
        render(80);
    }
    /* (UPDATE MODE's countdown is main.c's loop: not in the browser build) */
    printf("%s: %d failures\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}

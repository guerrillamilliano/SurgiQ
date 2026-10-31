/* Offline check of the screen model: selected-band routing, on/off, slope, reset, curve columns, markers, display_rev,
 * the spectrum analyser and state.
 *   gcc -O2 -Wall -Iwrapper ports/peq/test/view.c ports/peq/src/peq.c -lm -o /tmp/view && /tmp/view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine.h"

static const mpc_engine_t *api;
static int fails;

static int slides;   /* display polls a value needed to land (the last settle()) */

/* the screen glides to new values on each display poll (the wrapper reads display_rev ~10 times a second): poll until
 * nothing moves, as the wrapper would over the next second */
static void settle(void *e) {
    char b[32], last[32] = "";
    slides = 0;
    for (int i = 0; i < 40; i++) {
        api->get_param(e, "display_rev", b, sizeof b);
        if (!strcmp(b, last)) break;
        snprintf(last, sizeof last, "%s", b);
        slides++;
    }
}

/* A SurgiQ in the layout these tests were written for (0.17.0's defaults): the peaks at 150 Hz, 400 Hz, 1, 2.5, 6, 10
 * and 14 kHz and the HP, LS, HS and LP on. Since 0.18.0 a fresh one starts with the peaks spread evenly and those four
 * off (checked by itself in main) */
static void *create_classic(void) {
    void *e = api->create(NULL);
    static const char *const set[] = { "p1_freq", "0.2917", "p2_freq", "0.4337", "p3_freq", "0.5663", "p4_freq", "0.699",
                                       "p5_freq", "0.8257", "p6_freq", "0.8997", "p7_freq", "0.9484", "on_hp", "1",
                                       "on_ls", "1", "on_hs", "1", "on_lp", "1", NULL };
    for (int i = 0; set[i]; i += 2) api->set_param(e, set[i], set[i + 1]);
    settle(e);
    return e;
}
static const char *get(void *e, const char *k) {
    static char b[4][8192];
    static int n;
    char *o = b[n++ & 3];
    /* what the screen shows (curve, markers, pills, the grid's ring) is read once the slide has landed */
    if ((k[0] == 'c' && k[1] >= '0' && k[1] <= '9') || !strncmp(k, "m_", 2) || !strncmp(k, "pp_", 3) || !strcmp(k, "pad"))
        settle(e);
    if (api->get_param(e, k, o, 8192) <= 0) strcpy(o, "<none>");
    return o;
}

static void expect(const char *what, const char *got, const char *want) {
    int ok = !strcmp(got, want);
    fails += !ok;
    printf("%s %-46s %s%s%s\n", ok ? "ok  " : "FAIL", what, got, ok ? "" : "  want ", ok ? "" : want);
}

/* a curve column's level at its left edge (its option is level * 9 + the tilt's index, src/peq.c TILT) and its tilt */
static const char *level(void *e, const char *key) {
    static char b[16];
    snprintf(b, sizeof b, "%d", atoi(get(e, key)) / 9);
    return b;
}

static int tilt(void *e, const char *key) {
    static const int T[9] = { -8, -4, -2, -1, 0, 1, 2, 4, 8 };
    return T[atoi(get(e, key)) % 9];
}

/* the chain never breaks: every column starts where the one before it ended */
static int chained(void *e) {
    char k[8], k2[8];
    for (int i = 0; i + 1 < 48; i++) {
        snprintf(k, sizeof k, "c%02d", i);
        int end = atoi(level(e, k)) + tilt(e, k);
        snprintf(k2, sizeof k2, "c%02d", i + 1);
        if (atoi(level(e, k2)) != end) return 0;
    }
    return 1;
}

static void curve_row(void *e) {
    char k[8];
    printf("     curve:");
    for (int i = 0; i < 48; i++) { snprintf(k, sizeof k, "c%02d", i); printf(" %s%+d", level(e, k), tilt(e, k)); }
    printf("\n");
}

/* a spectrum column's level at its left edge (its frame is level * 9 + its tilt's index, src/peq.c WTILT) */
static int wlevel(void *e, const char *key) { return atoi(get(e, key)) / 9; }

static int tilt_w(void *e, const char *key) {
    static const int T[9] = { 0, -1, 1, -2, 2, -4, 4, -8, 8 };
    return T[atoi(get(e, key)) % 9];
}

int main(void) {
    api = mpc_engine();
    /* a fresh SurgiQ (0.18.0): the HP, LS, HS and LP off, the seven peaks evenly across the graph, flat */
    void *f = api->create(NULL);
    settle(f);
    expect("fresh: HP off", get(f, "on_hp"), "0");
    expect("  LS off", get(f, "on_ls"), "0");
    expect("  HS off", get(f, "on_hs"), "0");
    expect("  LP off", get(f, "on_lp"), "0");
    expect("  and their pills hidden", get(f, "pp_hp"), "264");
    static const char *const pk[7] = { "pp_p1", "pp_p2", "pp_p3", "pp_p4", "pp_p5", "pp_p6", "pp_p7" };
    char want[8], what[64];
    for (int i = 0; i < 7; i++) {   /* every third pill column on the 0 dB row: 3, 6, .. 21 */
        snprintf(want, sizeof want, "%d", 5 * 24 + 3 * (i + 1));
        snprintf(what, sizeof what, "  band %d's pill evenly placed", i + 1);
        expect(what, get(f, pk[i]), want);
    }
    expect("  band 1 at 47 Hz", get(f, "p1_freq_display"), "47 Hz");
    expect("  band 7 at 8.4 kHz", get(f, "p7_freq_display"), "8.43 kHz");
    expect("  and the curve flat", get(f, "c24"), "166");
    api->destroy(f);

    void *e = create_classic();
    expect("default selected band = 3 (index 4)", get(e, "sel_band"), "4");
    expect("default curve flat (col 24 = 0 dB)", level(e, "c24"), "18");
    expect("  and level (no tilt)", get(e, "c24"), "166");
    expect("HP and LP markers hidden while off", get(e, "m_hp"), "48");
    expect("  (LP)", get(e, "m_lp"), "48");
    expect("band 3 marker at the 1 kHz column", get(e, "m_p3"), "27");
    expect("selected highlight on band 3", get(e, "m_sel"), "27");
    expect("band 4 marker at 2.5 kHz", get(e, "m_p4"), "33");
    unsigned r0 = (unsigned)atoi(get(e, "display_rev"));

    api->set_param(e, "sel_gain", "12");
    expect("sel_gain writes band 3 gain", get(e, "p3_gain"), "12.00000");
    expect("sel_gain_display", get(e, "sel_gain_display"), "+12.0 dB");
    expect("curve peaks at the 1 kHz column's edge (+12 = 30)", level(e, "c27"), "30");
    expect("  rising into it", tilt(e, "c26") > 0 ? "yes" : "no", "yes");
    expect("  and falling after it", tilt(e, "c28") < 0 ? "yes" : "no", "yes");
    expect("  one unbroken line (each column starts where the last ended)", chained(e) ? "yes" : "no", "yes");
    printf("     display_rev %u -> %s\n", r0, get(e, "display_rev"));
    curve_row(e);

    api->set_param(e, "sel_band", "0");   /* HP */
    expect("HP selected: highlight moves (off: column 0)", get(e, "m_sel"), "0");
    expect("HP has no gain: display", get(e, "sel_gain_display"), "-");
    expect("HP Q is its resonance (Butterworth)", get(e, "sel_q_display"), "0.71");
    expect("HP slope reads 12 dB (option 0)", get(e, "sel_slope"), "0");
    expect("HP freq when off", get(e, "sel_freq_display"), "Off");
    api->set_param(e, "sel_gain", "6");
    expect("sel_gain on HP is ignored (band 3 kept)", get(e, "p3_gain"), "12.00000");
    api->set_param(e, "sel_freq", "0.3656");
    api->set_param(e, "sel_slope", "2");
    api->set_param(e, "sel_q", "0.6026");
    expect("sel_freq turns the HP on at 250 Hz", get(e, "hp_freq_display"), "250 Hz");
    expect("sel_slope sets the HP to 48 dB", get(e, "hp_slope"), "2");
    expect("sel_q sets the HP resonance", get(e, "hp_q_display"), "2.77");
    expect("HP marker shows at its column", get(e, "m_hp"), "17");
    expect("highlight follows the HP", get(e, "m_sel"), "17");
    curve_row(e);

    api->set_param(e, "sel_band", "1");   /* low shelf */
    expect("shelf has no Q: display", get(e, "sel_q_display"), "-");
    char base18[16];   /* a column the shelf reaches but the 250 Hz high-pass leaves alone */
    snprintf(base18, sizeof base18, "%s", get(e, "c18"));
    api->set_param(e, "sel_gain", "-6");
    expect("low shelf gain via sel_gain (to -6)", get(e, "ls_gain"), "-6.00000");
    api->set_param(e, "sel_gain", "-30");
    expect("gain clamps at -24 dB", get(e, "ls_gain"), "-24.00000");
    printf("     column 18: %s before the shelf, %s with it at -24 dB\n", base18, get(e, "c18"));
    api->set_param(e, "sel_on", "0");
    expect("sel_on switches the shelf off", get(e, "on_ls"), "0");
    expect("its marker hides", get(e, "m_ls"), "48");
    expect("but the highlight still points at it", get(e, "m_sel"), "11");
    expect("curve drops a band that is off (col 18)", get(e, "c18"), base18);
    api->set_param(e, "band_reset", "1");
    api->set_param(e, "band_reset", "0");
    expect("RESET: shelf gain back to 0", get(e, "ls_gain"), "0.00000");
    expect("RESET: shelf on again", get(e, "on_ls"), "1");
    expect("RESET left band 3 alone", get(e, "p3_gain"), "12.00000");

    api->set_param(e, "sel_band", "5");   /* band 4 */
    api->set_param(e, "sel_gain", "6");
    expect("band 4 gain via sel_gain", get(e, "p4_gain"), "6.00000");

    char c05[16];
    snprintf(c05, sizeof c05, "%s", get(e, "c05"));
    api->set_param(e, "c05", "3");   /* read-only */
    api->set_param(e, "m_ls", "20");
    expect("curve columns ignore host writes", get(e, "c05"), c05);
    expect("markers ignore host writes", get(e, "m_ls"), "11");

    char st[8192];
    snprintf(st, sizeof st, "%s", get(e, "state"));
    void *e2 = create_classic();
    api->set_param(e2, "state", st);
    expect("state restores the selected band", get(e2, "sel_band"), "5");
    expect("state restores band 3 gain", get(e2, "p3_gain"), "12.00000");
    expect("state restores the HP slope", get(e2, "hp_slope"), "2");
    expect("state restores band 4 gain", get(e2, "p4_gain"), "6.00000");
    expect("state restores the curve", get(e2, "c27"), get(e, "c27"));
    api->destroy(e2);
    api->destroy(e);

    /* the tap grid: option = row * 48 + column, rows every 3 dB from +15 (row 0); row 5 is 0 dB, where the pills sit */
    e = create_classic();
    expect("grid reads back band 3 at 1 kHz, 0 dB", get(e, "pad"), "267");   /* row 5 * 48 + column 27 */
    api->set_param(e, "pad", "136");   /* row 2 (+9 dB), column 40 (~6.4 kHz) */
    expect("tap moves band 3's gain to +9 dB", get(e, "p3_gain"), "9.00000");
    expect("  and its frequency to column 40's centre", get(e, "p3_freq_display"), "6.80 kHz");
    expect("grid reads back the new cell", get(e, "pad"), "136");
    expect("band 3's stem moved with it", get(e, "m_p3"), "40");
    api->set_param(e, "pad", "254");   /* row 5, column 14: band 1's pill (150 Hz) */
    expect("tap on band 1's pill selects band 1", get(e, "sel_band"), "2");
    expect("  without moving band 3", get(e, "p3_gain"), "9.00000");
    expect("  or band 1", get(e, "p1_gain"), "0.00000");
    expect("grid now reads back band 1's cell", get(e, "pad"), "254");
    api->set_param(e, "pad", "254");   /* the selected band's own pill: nothing happens */
    expect("tap on its own pill leaves band 1 alone", get(e, "p1_freq_display"), "150 Hz");
    api->set_param(e, "on_p1", "0");
    api->set_param(e, "pad", "438");   /* row 9 (-12 dB), column 6 */
    expect("placing a band that was off switches it on", get(e, "on_p1"), "1");
    expect("  at -12 dB", get(e, "p1_gain"), "-12.00000");
    api->set_param(e, "sel_band", "0");   /* the high-pass has no gain: a tap only moves its frequency */
    api->set_param(e, "pad", "10");       /* row 0, column 10 */
    expect("HP placed at column 10", get(e, "m_hp"), "10");
    expect("  and its cell reads back on the 0 dB row", get(e, "pad"), "250");
    api->destroy(e);

    /* pills at their real height, sliding there */
    e = create_classic();
    /* pill cells: row * 24 + column, on every other curve column */
    expect("band 3's pill: 0 dB row, 1 kHz column", get(e, "pp_p3"), "133");
    expect("HP fully open: its pill at the graph's left edge (0.15.1)", get(e, "pp_hp"), "120");
    expect("LP fully open: its pill at the right edge", get(e, "pp_lp"), "143");
    api->set_param(e, "on_hp", "0");
    for (int i = 0; i < 20; i++) api->get_param(e, "display_rev", (char[16]){0}, 16);   /* the display catches up */
    expect("HP switched off: its pill hidden", get(e, "pp_hp"), "264");
    api->set_param(e, "on_hp", "1");
    api->set_param(e, "p3_gain", "9");
    char midway[16];
    api->get_param(e, "display_rev", midway, sizeof midway);   /* one display poll after the change */
    api->get_param(e, "pp_p3", midway, sizeof midway);
    printf("     band 3's pill one poll after +9 dB: cell %s (on its way)\n", midway);
    expect("  lands at +9 dB (row 2)", get(e, "pp_p3"), "61");
    printf("     the slide took %d display polls (~%d ms)\n", slides, slides * 100);
    expect("a 6 dB cut puts band 2's pill two rows down", (api->set_param(e, "p2_gain", "-6"), get(e, "pp_p2")), "178");
    expect("tapping a pill at its real height selects that band", (api->set_param(e, "pad", "356"), get(e, "sel_band")), "3");
    expect("the ring is on band 2 now", get(e, "pad"), "356");
    api->destroy(e);

    /* the selected band's ring; the old theme switch is inert */
    e = create_classic();
    expect("the ring sits on band 3's pill", get(e, "pp_sel"), get(e, "pp_p3"));
    api->set_param(e, "sel_band", "1");
    expect("the ring follows the selection (LS)", get(e, "pp_sel"), get(e, "pp_ls"));
    api->set_param(e, "sel_gain", "6");
    expect("  and its pill as it moves", get(e, "pp_sel"), get(e, "pp_ls"));
    api->set_param(e, "pick_p5", "1");
    expect("a tap on band 5's pill selects it", get(e, "sel_band"), "6");
    api->set_param(e, "pick_p5", "0");
    expect("  the release changes nothing", get(e, "sel_band"), "6");
    expect("  and the ring moves to it", get(e, "pp_sel"), get(e, "pp_p5"));
    /* bands 6 and 7 (0.17.0): flat at 10 and 14 kHz, between the high shelf and the low-pass on the 0 dB row */
    expect("band 6's pill: 0 dB row, 10 kHz column", get(e, "pp_p6"), "141");
    expect("band 7's pill: 0 dB row, 14 kHz column", get(e, "pp_p7"), "142");
    api->set_param(e, "pick_p7", "1");
    expect("a tap on band 7's pill selects it (selector index 8)", get(e, "sel_band"), "8");
    api->set_param(e, "pick_p7", "0");
    expect("  shown as band 7's frequency", get(e, "sel_freq_display"), "14.0 kHz");
    char c45[8];
    snprintf(c45, sizeof c45, "%s", level(e, "c45"));
    api->set_param(e, "p6_gain", "9");
    settle(e);
    printf("     column 45 (10 kHz): level %s, then %s with band 6 at +9 dB\n", c45, level(e, "c45"));
    expect("band 6's gain lifts the curve near 10 kHz", atoi(level(e, "c45")) >= atoi(c45) + 4 ? "yes" : "no", "yes");
    char st6[8192];
    api->get_param(e, "state", st6, sizeof st6);
    void *e3 = create_classic();
    api->set_param(e3, "state", st6);
    expect("state restores band 6's gain", get(e3, "p6_gain"), "9.00000");
    api->destroy(e3);
    /* sharp shapes (0.17.0): the curve stays one line, however steep */
    void *e4 = create_classic();
    api->set_param(e4, "p3_gain", "-24");
    api->set_param(e4, "p3_q", "0.75");   /* Q 4: a narrow, deep cut at 1 kHz */
    settle(e4);
    curve_row(e4);
    expect("a narrow -24 dB cut: still one line", chained(e4) ? "yes" : "no", "yes");
    int lo = 99;
    char k4[8];
    for (int i = 20; i < 34; i++) { snprintf(k4, sizeof k4, "c%02d", i); int l = atoi(level(e4, k4)); if (l < lo) lo = l; }
    printf("     its dip on the line: level %d (%d dB)\n", lo, lo - 18);
    expect("  and it dips to within 3 dB of the graph's bottom (-18 dB; steeper than 8 dB a column catches up)", lo <= 3 ? "yes" : "no", "yes");
    api->set_param(e4, "p3_gain", "0");
    api->set_param(e4, "hp_freq", "0.4337");   /* a 48 dB/oct high-pass at 400 Hz */
    api->set_param(e4, "hp_slope", "2");
    settle(e4);
    curve_row(e4);
    expect("a 48 dB/oct high-pass: still one line", chained(e4) ? "yes" : "no", "yes");
    api->set_param(e4, "hp_freq", "0");
    api->set_param(e4, "p4_gain", "18");
    api->set_param(e4, "p4_q", "0.6");   /* a narrow +18 dB boost at 2.5 kHz */
    settle(e4);
    curve_row(e4);
    expect("a narrow +18 dB boost: still one line", chained(e4) ? "yes" : "no", "yes");
    int hi = -1;
    for (int i = 26; i < 40; i++) { snprintf(k4, sizeof k4, "c%02d", i); int l = atoi(level(e4, k4)); if (l > hi) hi = l; }
    printf("     its peak on the line: level %d (+%d dB)\n", hi, hi - 18);
    expect("  and it climbs to within 3 dB of the boost", hi >= 18 + 15 ? "yes" : "no", "yes");
    api->destroy(e4);
    api->set_param(e, "theme_apply", "1");
    expect("theme_apply does nothing since 0.12.0", get(e, "theme_apply"), "0");
    api->destroy(e);

    /* the FX tab: values, the range brackets on the graph, and the sound */
    e = create_classic();
    expect("reverb off: its brackets hidden", get(e, "fxm_rev_lo"), "24");
    expect("mix shown in %", get(e, "fx_rev_mix_display"), "30%");
    expect("delay time in ms (20 * 75^0.5)", get(e, "fx_dly_time_display"), "173 ms");
    expect("chorus rate in Hz (0.05 * 100^0.4)", get(e, "fx_cho_rate_display"), "0.32 Hz");
    expect("band low in Hz", get(e, "fx_rev_lo_display"), "100 Hz");
    api->set_param(e, "fx_rev_on", "1");
    expect("reverb on, FULL: brackets at both ends", get(e, "fxm_rev_lo"), "0");
    expect("  (the high one)", get(e, "fxm_rev_hi"), "23");
    api->set_param(e, "fx_rev_full", "1");
    expect("BAND: the low bracket at 100 Hz's column", get(e, "fxm_rev_lo"), "5");
    expect("  the high one at 8 kHz's", get(e, "fxm_rev_hi"), "20");
    expect("the delay's stay hidden", get(e, "fxm_dly_hi"), "24");
    snprintf(st, sizeof st, "%s", get(e, "state"));
    e2 = create_classic();
    api->set_param(e2, "state", st);
    expect("state restores the reverb on", get(e2, "fx_rev_on"), "1");
    expect("  in BAND", get(e2, "fx_rev_full"), "1");
    api->destroy(e2);
    api->destroy(e);
    /* an impulse, then silence: the tail after 0.25 s with each effect alone, and none */
    static const char *const fxs[] = { "", "fx_rev_on", "fx_dly_on", "fx_cho_on", "fx_shm_on" };
    double tail[5];
    for (int f = 0; f < 5; f++) {
        e = create_classic();
        if (f) api->set_param(e, fxs[f], "1");
        if (f == 2) api->set_param(e, "fx_dly_time", "0.6");   /* 20 * 75^0.6 = 267 ms: after the 0.25 s mark */
        int16_t ib[256] = { 0 }, ob[256];
        double sum = 0, peak = 0;
        for (int blk = 0; blk < 44100 * 2 / 128; blk++) {
            for (int n = 0; n < 128; n++) ib[2 * n] = ib[2 * n + 1] = blk < 4 ? (int16_t)lrint(20000 * sin(2 * 3.14159265 * 1000 * (blk * 128 + n) / 44100.0)) : 0;
            api->process(e, ib, ob, 128);
            for (int n = 0; n < 256; n++) {
                if (blk * 128 >= 11025) sum += (double)ob[n] * ob[n];
                if (fabs(ob[n]) > peak) peak = fabs(ob[n]);
            }
        }
        tail[f] = sqrt(sum / (44100 * 2 * 2 - 11025 * 2));
        printf("     %-10s tail RMS %.1f, peak %.0f\n", f ? fxs[f] : "dry", tail[f], peak);
        api->destroy(e);
    }
    expect("dry: silence after the hit", tail[0] < 1 ? "quiet" : "ringing", "quiet");
    expect("reverb rings on", tail[1] > 20 ? "tail" : "none", "tail");
    expect("delay repeats", tail[2] > 20 ? "tail" : "none", "tail");
    expect("shimmer rings on", tail[4] > 20 ? "tail" : "none", "tail");
    expect("chorus adds no long tail", tail[3] < 5 ? "short" : "long", "short");

    /* the relative gain control: nudges by the distance moved, then springs back to the middle */
    e = create_classic();
    expect("g_rel rests in the middle", get(e, "g_rel"), "0.50000");
    api->set_param(e, "g_rel", "0.5078125");   /* one Q-Link event up (1/128) */
    expect("one Q-Link event: +0.125 dB on band 3", get(e, "p3_gain"), "0.12500");
    api->set_param(e, "g_rel", "0.5625");      /* a drag on: 0.0546875 further = +0.875 dB */
    expect("a drag nudges by the distance moved", get(e, "p3_gain"), "1.00000");
    expect("  and reads back as the band's gain", get(e, "g_rel_display"), "+1.0 dB");
    settle(e);
    expect("left alone, it springs back to the middle", get(e, "g_rel"), "0.50000");
    expect("  without touching the gain", get(e, "p3_gain"), "1.00000");
    api->set_param(e, "g_rel", "0.4375");      /* a new drag, down from the middle: -1 dB */
    expect("the next drag carries on from the band's gain", get(e, "p3_gain"), "0.00000");
    api->set_param(e, "sel_band", "0");
    api->set_param(e, "hp_freq", "0.3");
    char q0[16];
    snprintf(q0, sizeof q0, "%s", get(e, "hp_q"));
    api->set_param(e, "g_rel", "0.6");         /* on the HP (no gain): its resonance */
    expect("on a filter it nudges the resonance", strcmp(get(e, "hp_q"), q0) > 0 ? "higher" : "not", "higher");
    api->destroy(e);

    /* the Q-Link knobs for the short option lists: zones over a continuous knob */
    e = create_classic();
    expect("q_view starts in POST's zone", get(e, "q_view_display"), "POST");
    expect("  at its middle", get(e, "q_view"), "0.75000");
    api->set_param(e, "q_view", "0.55");
    expect("a turn inside POST's zone changes nothing", get(e, "wave_view"), "1");
    expect("  and the knob keeps its own position", get(e, "q_view"), "0.55000");
    api->set_param(e, "q_view", "0.49");
    expect("crossing the middle switches to PRE", get(e, "wave_view"), "0");
    expect("  and the knob stays where it was turned", get(e, "q_view"), "0.49000");
    api->set_param(e, "q_speed", "0.10");
    expect("q_speed's first third is SLOW", get(e, "wave_time"), "0");
    api->set_param(e, "q_rate", "0.95");
    expect("q_rate's last third is HIGH", get(e, "spec_rate"), "2");
    expect("  shown as HIGH", get(e, "q_rate_display"), "HIGH");
    api->set_param(e, "wave_view", "1");   /* a touch on POST: the knob moves into POST's zone */
    expect("a touch moves the knob to the setting's zone", get(e, "q_view"), "0.75000");
    expect("q_slope on a peak band has nothing to show", get(e, "q_slope_display"), "-");
    api->set_param(e, "sel_band", "0");
    api->set_param(e, "q_slope", "0.9");
    expect("q_slope on the HP sets 48 dB", get(e, "hp_slope"), "2");
    api->destroy(e);

    /* the spectrum analyser: a -6 dBFS 1 kHz tone, PRE and POST of a -12 dB cut there, then silence */
    e = create_classic();
    expect("analyser on by default", get(e, "spec_on"), "1");
    expect("no spectrum before any sound", get(e, "w27"), "0");
    api->set_param(e, "p3_gain", "-12");   /* band 3 sits on 1 kHz: POST is 12 dB quieter there */
    int16_t in[256], out[256];
    long t = 0;
    #define PLAY(seconds, amp) for (int blk = 0; blk < (int)((seconds) * 44100 / 128); blk++) { \
        for (int n = 0; n < 128; n++, t++) { int16_t s = (int16_t)lrint((amp) * sin(2 * 3.14159265 * 1000 * t / 44100.0)); \
            in[2 * n] = in[2 * n + 1] = s; } \
        api->process(e, in, out, 128); }
    api->set_param(e, "wave_view", "0");
    PLAY(1.0, 16384);
    char row[128] = "", k[8];
    for (int i = 0; i < 48; i++) { snprintf(k, sizeof k, "w%02d", i); strcat(row, (char[]){ "0123456789abcdefghijklmno"[wlevel(e, k)], 0 }); }
    printf("     PRE : %s\n", row);
    char lv[8];   /* the tone: -4.2 dB in its column (-6 dBFS, +1.8 dB Hann spread), -7.2 dB on its edges (half the power) */
    snprintf(lv, sizeof lv, "%d", wlevel(e, "w27"));
    printf("     the tone's peak on the line: level %s (true: 22)\n", lv);
    expect("PRE: the 1 kHz column's line at the tone, within a level (-7.2 dB: level 22)", wlevel(e, "w27") >= 21 ? "yes" : "no", "yes");
    expect("  the line climbs to it and joins (a chain)", tilt_w(e, "w26") > 0 && wlevel(e, "w26") + tilt_w(e, "w26") == wlevel(e, "w27") ? "yes" : "no", "yes");
    snprintf(lv, sizeof lv, "%d", wlevel(e, "w20"));
    expect("PRE: 400 Hz stays dark", lv, "0");
    snprintf(lv, sizeof lv, "%d", wlevel(e, "w41"));
    expect("PRE: 8 kHz stays dark", lv, "0");
    api->set_param(e, "wave_view", "1");
    PLAY(1.0, 16384);
    row[0] = 0;
    for (int i = 0; i < 48; i++) { snprintf(k, sizeof k, "w%02d", i); strcat(row, (char[]){ "0123456789abcdefghijklmno"[wlevel(e, k)], 0 }); }
    printf("     POST: %s\n", row);
    expect("POST: 12 dB lower after the cut, within a level (-19.2 dB: level 17)", wlevel(e, "w27") >= 16 && wlevel(e, "w27") <= 17 ? "yes" : "no", "yes");
    PLAY(3.5, 0);   /* MED falls 24 dB a second: -4 to -61 dB takes ~2.4 s */
    expect("silence: the spectrum falls to nothing", get(e, "w27"), "0");
    /* timing (0.18.2): a short hit is never missed, and shows soon after it sounds */
    for (int rate = 0; rate < 3; rate++) {
        static const char *const RN[3] = { "LOW", "MED", "HIGH" };
        char rv[4], what[96];
        snprintf(rv, sizeof rv, "%d", rate);
        api->set_param(e, "spec_rate", rv);
        int shown_ms = -1, lat_ms[3] = { 70, 70, 70 };   /* the screen looked at 30 times a second whatever the budget */
        for (int trial = 0; trial < 5 && shown_ms < 9999; trial++) {   /* hits at different moments of the cycle */
            PLAY(3.0 + 0.037 * trial, 0);   /* everything back to nothing */
            int seen = -1;
            for (int blk = 0; blk < (int)(0.5 * 44100 / 128); blk++) {
                for (int n = 0; n < 128; n++, t++) {
                    double amp = blk < 4 ? 16384 : 0;   /* a 12 ms hit of 1 kHz */
                    in[2 * n] = in[2 * n + 1] = (int16_t)lrint(amp * sin(2 * 3.14159265 * 1000 * t / 44100.0));
                }
                api->process(e, in, out, 128);
                if (seen < 0 && wlevel(e, "w27") > 0) seen = (blk + 1) * 128 * 1000 / 44100;
            }
            if (seen < 0) { shown_ms = 99999; break; }   /* missed */
            if (seen > shown_ms) shown_ms = seen;
        }
        printf("     %s: a 12 ms hit shows at worst %d ms after it starts (5 tries)\n", RN[rate], shown_ms);
        snprintf(what, sizeof what, "%s: a short hit is never missed, and shows within %d ms", RN[rate], lat_ms[rate]);
        expect(what, shown_ms <= lat_ms[rate] ? "yes" : "no", "yes");
    }
    PLAY(3.0, 0);   /* the last hit gone again */
    char rev[32];
    settle(e);   /* the band 3 cut above has slid into place (the wrapper polls display_rev all along) */
    snprintf(rev, sizeof rev, "%s", get(e, "display_rev"));
    PLAY(2.0, 0);
    expect("silence: nothing changes (no redraws)", get(e, "display_rev"), rev);
    PLAY(0.5, 16384);
    api->set_param(e, "spec_on", "0");
    expect("OFF clears the spectrum", get(e, "w27"), "0");
    PLAY(0.5, 16384);
    expect("  and it stays clear while off", get(e, "w27"), "0");
    api->set_param(e, "wave_time", "2");
    snprintf(st, sizeof st, "%s", get(e, "state"));
    e2 = create_classic();
    api->set_param(e2, "state", st);
    expect("state restores analyser off", get(e2, "spec_on"), "0");
    expect("  POST", get(e2, "wave_view"), "1");
    expect("  and FAST", get(e2, "wave_time"), "2");
    api->destroy(e2);
    api->destroy(e);
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}

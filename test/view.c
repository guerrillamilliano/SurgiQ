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

static void curve_row(void *e) {
    char k[8];
    printf("     curve:");
    for (int i = 0; i < 48; i++) { snprintf(k, sizeof k, "c%02d", i); printf(" %s", get(e, k)); }
    printf("\n");
}

int main(void) {
    api = mpc_engine();
    void *e = api->create(NULL);
    expect("default selected band = 3 (index 4)", get(e, "sel_band"), "4");
    expect("default curve flat (col 24 = 0 dB)", get(e, "c24"), "18");
    expect("HP and LP markers hidden while off", get(e, "m_hp"), "48");
    expect("  (LP)", get(e, "m_lp"), "48");
    expect("band 3 marker at the 1 kHz column", get(e, "m_p3"), "27");
    expect("selected highlight on band 3", get(e, "m_sel"), "27");
    expect("band 4 marker at 2.5 kHz", get(e, "m_p4"), "33");
    unsigned r0 = (unsigned)atoi(get(e, "display_rev"));

    api->set_param(e, "sel_gain", "12");
    expect("sel_gain writes band 3 gain", get(e, "p3_gain"), "12.00000");
    expect("sel_gain_display", get(e, "sel_gain_display"), "+12.0 dB");
    expect("curve peaks at the 1 kHz column (+12 = 30)", get(e, "c27"), "30");
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
    void *e2 = api->create(NULL);
    api->set_param(e2, "state", st);
    expect("state restores the selected band", get(e2, "sel_band"), "5");
    expect("state restores band 3 gain", get(e2, "p3_gain"), "12.00000");
    expect("state restores the HP slope", get(e2, "hp_slope"), "2");
    expect("state restores band 4 gain", get(e2, "p4_gain"), "6.00000");
    expect("state restores the curve", get(e2, "c27"), get(e, "c27"));
    api->destroy(e2);
    api->destroy(e);

    /* the tap grid: option = row * 48 + column, rows every 3 dB from +15 (row 0); row 5 is 0 dB, where the pills sit */
    e = api->create(NULL);
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
    e = api->create(NULL);
    /* pill cells: row * 24 + column, on every other curve column */
    expect("band 3's pill: 0 dB row, 1 kHz column", get(e, "pp_p3"), "133");
    expect("HP's pill hidden while it is off", get(e, "pp_hp"), "264");
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
    e = api->create(NULL);
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
    api->set_param(e, "theme_apply", "1");
    expect("theme_apply does nothing since 0.12.0", get(e, "theme_apply"), "0");
    api->destroy(e);

    /* the FX tab: values, the range brackets on the graph, and the sound */
    e = api->create(NULL);
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
    e2 = api->create(NULL);
    api->set_param(e2, "state", st);
    expect("state restores the reverb on", get(e2, "fx_rev_on"), "1");
    expect("  in BAND", get(e2, "fx_rev_full"), "1");
    api->destroy(e2);
    api->destroy(e);
    /* an impulse, then silence: the tail after 0.25 s with each effect alone, and none */
    static const char *const fxs[] = { "", "fx_rev_on", "fx_dly_on", "fx_cho_on", "fx_shm_on" };
    double tail[5];
    for (int f = 0; f < 5; f++) {
        e = api->create(NULL);
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
    e = api->create(NULL);
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
    e = api->create(NULL);
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
    expect("q_rate's last third is 15/S", get(e, "spec_rate"), "2");
    expect("  shown as 15/S", get(e, "q_rate_display"), "15/S");
    api->set_param(e, "wave_view", "1");   /* a touch on POST: the knob moves into POST's zone */
    expect("a touch moves the knob to the setting's zone", get(e, "q_view"), "0.75000");
    expect("q_slope on a peak band has nothing to show", get(e, "q_slope_display"), "-");
    api->set_param(e, "sel_band", "0");
    api->set_param(e, "q_slope", "0.9");
    expect("q_slope on the HP sets 48 dB", get(e, "hp_slope"), "2");
    api->destroy(e);

    /* the spectrum analyser: a -6 dBFS 1 kHz tone, PRE and POST of a -12 dB cut there, then silence */
    e = api->create(NULL);
    expect("analyser on by default", get(e, "spec_on"), "1");
    expect("no bars before any sound", get(e, "w27"), "0");
    api->set_param(e, "p3_gain", "-12");   /* band 3 sits on 1 kHz: POST is 12 dB quieter there */
    int16_t in[256], out[256];
    long t = 0;
    #define PLAY(seconds, amp) for (int blk = 0; blk < (int)((seconds) * 44100 / 128); blk++) { \
        for (int n = 0; n < 128; n++, t++) { int16_t s = (int16_t)lrint((amp) * sin(2 * 3.14159265 * 1000 * t / 44100.0)); \
            in[2 * n] = in[2 * n + 1] = s; } \
        api->process(e, in, out, 128); }
    api->set_param(e, "wave_view", "0");
    PLAY(1.0, 16384);
    char row[64] = "", k[8];
    for (int i = 0; i < 48; i++) { snprintf(k, sizeof k, "w%02d", i); strcat(row, (char[]){ "0123456789abcdefghijklmno"[atoi(get(e, k))], 0 }); }
    printf("     PRE : %s\n", row);
    expect("PRE: the 1 kHz column at -6 dB (+1.8 dB Hann spread: step 23)", get(e, "w27"), "23");
    expect("PRE: 400 Hz stays dark", get(e, "w20"), "0");
    expect("PRE: 8 kHz stays dark", get(e, "w42"), "0");
    api->set_param(e, "wave_view", "1");
    PLAY(1.0, 16384);
    row[0] = 0;
    for (int i = 0; i < 48; i++) { snprintf(k, sizeof k, "w%02d", i); strcat(row, (char[]){ "0123456789abcdefghijklmno"[atoi(get(e, k))], 0 }); }
    printf("     POST: %s\n", row);
    expect("POST: 12 dB (5 steps) lower after the cut", get(e, "w27"), "18");
    PLAY(3.5, 0);   /* MED falls 24 dB a second: -4 to -61 dB takes ~2.4 s */
    expect("silence: the bars fall to nothing", get(e, "w27"), "0");
    char rev[32];
    settle(e);   /* the band 3 cut above has slid into place (the wrapper polls display_rev all along) */
    snprintf(rev, sizeof rev, "%s", get(e, "display_rev"));
    PLAY(2.0, 0);
    expect("silence: nothing changes (no redraws)", get(e, "display_rev"), rev);
    PLAY(0.5, 16384);
    api->set_param(e, "spec_on", "0");
    expect("OFF clears the bars", get(e, "w27"), "0");
    PLAY(0.5, 16384);
    expect("  and they stay clear while off", get(e, "w27"), "0");
    api->set_param(e, "wave_time", "2");
    snprintf(st, sizeof st, "%s", get(e, "state"));
    e2 = api->create(NULL);
    api->set_param(e2, "state", st);
    expect("state restores analyser off", get(e2, "spec_on"), "0");
    expect("  POST", get(e2, "wave_view"), "1");
    expect("  and FAST", get(e2, "wave_time"), "2");
    api->destroy(e2);
    api->destroy(e);
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}

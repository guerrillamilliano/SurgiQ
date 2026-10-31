/* Prints "<key> <option>" for the curve, marker, waveform and panel params of a sample EQ setting with a captured
 * kick-like hit (for test/composite.py).
 *   gcc -O2 -Iwrapper ports/peq/test/demo_values.c ports/peq/src/peq.c -lm -o /tmp/demo && /tmp/demo */
#include <math.h>
#include <stdio.h>
#include "engine.h"

int main(void) {
    const mpc_engine_t *api = mpc_engine();
    void *e = api->create(NULL);
    static const char *const set[] = { "hp_freq", "0.08", "hp_slope", "1", "ls_gain", "4", "p1_gain", "-3", "p2_gain", "-4",
                                       "p2_q", "0.45", "p3_gain", "6", "p4_gain", "3", "p4_q", "0.2", "p5_gain", "-5",
                                       "p5_q", "0.5", "hs_gain", "3", "on_p1", "0", "sel_band", "4",
                                       /* effects: a reverb on the whole signal, a delay on 300 Hz..3 kHz, a shimmer on the highs */
                                       "fx_rev_on", "1", "fx_dly_on", "1", "fx_dly_full", "1", "fx_dly_lo", "0.3577",
                                       "fx_dly_hi", "0.6911", "fx_shm_on", "1", "fx_shm_full", "1", "fx_shm_lo", "0.7", NULL };
    for (int i = 0; set[i]; i += 2) api->set_param(e, set[i], set[i + 1]);
    /* the analyser: 1.5 s of a music-like spectrum (falling towards the highs: white noise through two one-pole lowpasses,
     * plus some of the white) with a 60 Hz bass tone, so its bars show something on every column */
    int16_t in[256], out[256];
    unsigned rnd = 1;
    double lp1 = 0, lp2 = 0;
    for (long t = 0, blk = 0; blk < 520; blk++) {
        for (int n = 0; n < 128; n++, t++) {
            rnd = rnd * 1103515245u + 12345u;
            double w = (rnd >> 16) / 32768.0 - 1;
            lp1 += 0.05 * (w - lp1);
            lp2 += 0.2 * (w - lp2);
            int16_t s = (int16_t)lrint(32767 * (0.9 * lp1 + 0.25 * lp2 + 0.03 * w + 0.25 * sin(2 * M_PI * 60 * t / 44100.0)));
            in[2 * n] = in[2 * n + 1] = s;
        }
        api->process(e, in, out, 128);
    }
    for (int i = 0; i < 40; i++) api->get_param(e, "display_rev", (char[32]){0}, 32);   /* let the pills slide into place */
    static const char *const keys[] = { "m_hp", "m_ls", "m_p1", "m_p2", "m_p3", "m_p4", "m_p5", "m_hs", "m_lp", "m_sel",
                                        "sel_band", "wave_view", "pad", "pp_hp", "pp_ls", "pp_p1", "pp_p2", "pp_p3", "pp_p4", "pp_p5", "pp_p6", "pp_p7", "pp_hs", "pp_lp", "pp_sel",
                                        "fxm_rev_lo", "fxm_rev_hi", "fxm_dly_lo", "fxm_dly_hi", "fxm_cho_lo", "fxm_cho_hi",
                                        "fxm_shm_lo", "fxm_shm_hi", NULL };
    char b[64], k[8];
    for (int m = 0; keys[m]; m++) { api->get_param(e, keys[m], b, sizeof b); printf("%s %s\n", keys[m], b); }
    for (int c = 0; c < 48; c++) { snprintf(k, sizeof k, "c%02d", c); api->get_param(e, k, b, sizeof b); printf("%s %s\n", k, b); }
    for (int c = 0; c < 96; c++) { snprintf(k, sizeof k, "w%02d", c); api->get_param(e, k, b, sizeof b); printf("%s %s\n", k, b); }
    api->get_param(e, "sel_freq_display", b, sizeof b); fprintf(stderr, "freq %s\n", b);
    api->get_param(e, "sel_gain_display", b, sizeof b); fprintf(stderr, "gain %s\n", b);
    api->get_param(e, "wave_status", b, sizeof b); fprintf(stderr, "capture %s\n", b);
    api->destroy(e);
    return 0;
}

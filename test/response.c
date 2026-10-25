/* Offline response check for the EQ engine: plays sine tones through it and prints the gain at each frequency.
 * Build and run (from the repo root, in the gcc:12 image):
 *   gcc -O2 -Iwrapper ports/peq/test/response.c ports/peq/src/peq.c -lm -o /tmp/resp && /tmp/resp */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "engine.h"

static double gain_db(const mpc_engine_t *api, void *e, double hz) {
    int16_t in[256], out[256];
    double sin2 = 0, sout2 = 0;
    long t = 0;
    for (int blk = 0; blk < 400; blk++) {   /* ~1.2 s; the first half settles the filters */
        for (int n = 0; n < 128; n++, t++) {
            int16_t s = (int16_t)lrint(8000.0 * sin(2 * M_PI * hz * t / 44100.0));
            in[2 * n] = in[2 * n + 1] = s;
        }
        api->process(e, in, out, 128);
        if (blk >= 200)
            for (int n = 0; n < 128; n++) { sin2 += (double)in[2 * n] * in[2 * n]; sout2 += (double)out[2 * n] * out[2 * n]; }
    }
    return 10 * log10(sout2 / sin2);
}

static void sweep(const char *title, const char *const *kv) {
    const mpc_engine_t *api = mpc_engine();
    static const double F[] = { 30, 60, 100, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000 };
    printf("%-34s", title);
    for (unsigned i = 0; i < sizeof F / sizeof F[0]; i++) {
        void *e = api->create(NULL);
        for (int k = 0; kv[k]; k += 2) api->set_param(e, kv[k], kv[k + 1]);
        printf(" %6.1f", gain_db(api, e, F[i]));
        api->destroy(e);
    }
    printf("\n");
}

int main(void) {
    printf("%-34s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n", "dB at Hz:", "30", "60", "100", "250", "500", "1k", "2k", "4k", "8k",
           "12k", "16k");
    sweep("defaults (flat)", (const char *const[]){ NULL });
    sweep("band 3 +12 dB @1k Q1", (const char *const[]){ "p3_gain", "12", NULL });
    sweep("band 3 -12 dB @1k Q1", (const char *const[]){ "p3_gain", "-12", NULL });
    sweep("band 3 +24 dB @1k (clips: 16-bit)", (const char *const[]){ "p3_gain", "24", NULL });
    sweep("band 3 +24 dB, output -18", (const char *const[]){ "p3_gain", "24", "out_gain", "-18", NULL });
    sweep("band 3 +12 but switched off", (const char *const[]){ "p3_gain", "12", "on_p3", "0", NULL });
    sweep("band 4 +6 dB @2.5k", (const char *const[]){ "p4_gain", "6", NULL });
    sweep("band 5 -6 dB @6k", (const char *const[]){ "p5_gain", "-6", NULL });
    sweep("low shelf +6 dB @100", (const char *const[]){ "ls_gain", "6", NULL });
    sweep("high shelf -6 dB @8k", (const char *const[]){ "hs_gain", "-6", NULL });
    sweep("HP 250 Hz 12 dB/oct", (const char *const[]){ "hp_freq", "0.3656", NULL });
    sweep("HP 250 Hz 24 dB/oct", (const char *const[]){ "hp_freq", "0.3656", "hp_slope", "1", NULL });
    sweep("HP 250 Hz 48 dB/oct", (const char *const[]){ "hp_freq", "0.3656", "hp_slope", "2", NULL });
    sweep("HP 250 Hz 12 dB res Q2.8", (const char *const[]){ "hp_freq", "0.3656", "hp_q", "0.6026", NULL });
    sweep("LP 4 kHz 12 dB/oct", (const char *const[]){ "lp_freq", "0.767", NULL });
    sweep("LP 4 kHz 48 dB/oct", (const char *const[]){ "lp_freq", "0.767", "lp_slope", "2", NULL });
    sweep("LP 4 kHz 24 dB res Q2.8", (const char *const[]){ "lp_freq", "0.767", "lp_slope", "1", "lp_q", "0.6026", NULL });
    sweep("output -6 dB", (const char *const[]){ "out_gain", "-6", NULL });
    /* the filter presets' shapes (gen_params.py "Filter - ..."; positions: 1 kHz 0.5663, Q 10 0.9505) */
    sweep("preset Notch (1 kHz, -24 dB, Q10)", (const char *const[]){ "p3_gain", "-24", "p3_q", "0.9505", NULL });
    sweep("preset Band Pass (500 Hz-2 kHz)", (const char *const[]){ "hp_freq", "0.4663", "hp_slope", "1", "lp_freq", "0.6667",
                                                                   "lp_slope", "1", NULL });
    /* display text */
    const mpc_engine_t *api = mpc_engine();
    void *e = api->create(NULL);
    char b[64];
    const char *keys[] = { "hp_freq_display", "ls_freq_display", "p1_freq_display", "p2_freq_display", "p3_freq_display",
                           "p4_freq_display", "p5_freq_display", "p3_q_display", "hp_q_display", "lp_freq_display", NULL };
    for (int i = 0; keys[i]; i++) { api->get_param(e, keys[i], b, sizeof b); printf("%s = %s\n", keys[i], b); }
    api->get_param(e, "state", b, sizeof b);
    printf("state (first 60) = %.60s\n", b);
    api->destroy(e);
    return 0;
}

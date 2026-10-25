/* Time per 128-frame block with every effect on (BAND mode: the band filters too) against the EQ alone.
 *   gcc -O2 -Iwrapper ports/peq/test/fx_cost.c ports/peq/src/peq.c -lm -o /tmp/cost && /tmp/cost
 * The device's budget is 2.9 ms a block; its CPU is several times slower than a desktop's. */
#include <stdio.h>
#include <time.h>
#include "engine.h"

static double run(const mpc_engine_t *api, void *e) {
    int16_t in[256], out[256];
    for (int n = 0; n < 256; n++) in[n] = (int16_t)((n * 7919) % 20000 - 10000);
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < 20000; i++) api->process(e, in, out, 128);
    clock_gettime(CLOCK_MONOTONIC, &b);
    return ((b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) / 1e3) / 20000;
}

int main(void) {
    const mpc_engine_t *api = mpc_engine();
    void *e = api->create(NULL);
    double dry = run(api, e);
    static const char *const fx[] = { "rev", "dly", "cho", "shm" };
    char k[32];
    for (int i = 0; i < 4; i++) {
        snprintf(k, sizeof k, "fx_%s_on", fx[i]); api->set_param(e, k, "1");
        snprintf(k, sizeof k, "fx_%s_full", fx[i]); api->set_param(e, k, "1");
    }
    double wet = run(api, e);
    printf("EQ alone %.1f us a block, with all four effects %.1f us (budget 2900 us)\n", dry, wet);
    api->destroy(e);
    return 0;
}

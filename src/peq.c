/* Parametric EQ: an MPC OS insert effect on the shared wrapper (wrapper/engine.h, vst.json "effect": true).
 * Nine bands in series: high-pass, low shelf, five peaks, high shelf, low-pass, then an output gain. The filters are
 * 12, 24 or 48 dB/oct Butterworth cascades whose last stage takes a resonance; every band can be switched off.
 * Biquads from the RBJ Audio EQ Cookbook, Direct Form II transposed, float internally.
 * Frequency and Q knobs are 0..1 positions mapped on a log scale here; their text comes from "<key>_display".
 * Parameters glide per 128-frame block (about 10 ms) so a Q-Link sweep does not zipper.
 * The screen is FL-style: sel_band picks a band and the sel_* controls edit it (the Q-Links and the graph drag),
 * cNN are read-only curve columns (the response at that column's frequency in 1 dB steps), m_xx are the bands' marker
 * positions under the graph and m_sel the selected band's. The skin follows them through display_rev (HAS_DISPLAY_REV).
 * wNN is a live spectrum analyser (FL Parametric EQ 2 style) on the curve's 48 frequency columns: a 4096-point FFT of what
 * plays through, spec_rate times a second (4, 8 or 15), each column the energy of its slice of the log axis, in 2.5 dB
 * steps that rise at once and fall at wave_time's speed (12, 24 or 48 dB a second, whatever the rate). wave_view picks PRE (before the EQ) or POST, spec_on switches it off. Every update can move
 * most columns, which costs MPC's screen thread (docs/NOTES.md "Animation is expensive"), so the rate and the steps are
 * coarse and nothing moves in silence. wave_cap and wave_status are unused since 0.5.0.
 * pad is the tap grid over the graph (one option per cell): a tap moves the selected band to that frequency and gain, or,
 * on another band's pill (the 0 dB row), selects that band; it reads back the cell the selected band is in, so its node
 * ring follows Q-Links, presets and automation. pp_xx is each band's pill: the grid cell at its frequency and gain.
 * The screen shows disp, a copy of the settings that glides to them on each display poll, so pills, ring and curve slide.
 * q_* are the Q-Links for the short option lists: continuous knobs split into zones (see QKEYS).
 * g_rel is the relative gain control (the GAIN box's drag and its Q-Link): each move nudges the selected band's gain by
 * the distance moved (GREL_SPAN dB per full travel), like a Q-Link, and it springs back to the middle once left alone
 * for a few display polls, so a drag can always carry on in either direction.
 * pp_sel is the selected band's ring, drawn around its pill. Pills sit on PILL_COLS columns (every other curve column).
 * pick_xx (momentary, 0.12.1): a tap on band xx's pill (the pills are buttons) selects that band.
 * theme_* are unused since 0.12.0 (one skin); they stay so old projects load.
 * The FX tab (fx_<rev|dly|cho|shm>_*): reverb, delay, chorus and shimmer as parallel sends after the EQ, each with ON,
 * MIX, two settings, and a range: FULL (everything) or BAND (only lo..hi, through a 12 dB/oct high-pass and low-pass).
 * fxm_<fx>_lo / _hi place each effect's range brackets on the graph (FXM_COLS columns, FXM_COLS = hidden).
 * Keys, ranges and defaults must agree with gen_params.py. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"

#define SR 44100.0f
#define F_MIN 20.0f
#define F_RATIO 1000.0f   /* 20 Hz .. 20 kHz */
#define Q_MIN 0.3f
#define Q_RATIO 40.0f     /* 0.3 .. 12 */
#define GAIN 24.0f        /* band gain range, dB */
#define GLIDE 0.3f        /* share of the distance to the target covered per block */
#define NCOL 48           /* curve columns across the graph, on the same log axis as the frequency knobs */
#define LEVELS 37         /* curve column steps: -18..+18 dB in 1 dB, 18 = 0 dB */
#define MARK_OFF NCOL     /* marker option for a band that is off */
#define MAXST 4           /* biquad stages per band (a 48 dB/oct filter) */
#define PAD_COLS NCOL     /* the tap grid over the graph ("pad"): the curve's columns ... */
#define PAD_ROWS 11       /* ... by rows every 3 dB, +15 dB at the top */
#define PAD_ZERO 5        /* the 0 dB row, where the band pills sit */
#define GREL_SPAN 16.0f   /* g_rel: dB per full travel (a Q-Link event, 1/128 of it, is 0.125 dB) */
#define GREL_Q_SPAN 0.3f  /* ... and Q-knob positions per full travel, for the filters (no gain: resonance) */
#define PILL_COLS 24      /* pill positions across the graph: every other curve column (fewer screen parts) */
#define PILL_OFF (PILL_COLS * PAD_ROWS)
#define FXM_COLS 24       /* FX range markers: positions across the graph (fxm_*: PILL_COLS... the last option is off) */
/* the FX DSP: a Freeverb-style reverb (Jezar's tunings at 44.1 kHz; the right channel 23 samples longer) */
#define RV_COMBS 8
#define RV_APS 4
#define RV_SPREAD 23
static const int RV_COMB_LEN[RV_COMBS] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int RV_AP_LEN[RV_APS] = { 556, 441, 341, 225 };
#define DLY_MAX 66304     /* the delay's line: 1.5 s */
#define CHO_LEN 2048      /* the chorus's line: 46 ms */
#define PS_LEN 4096       /* the shimmer's octave-up pitch shifter: two read heads over a 93 ms line */
#define WCOL NCOL         /* analyser columns: the curve's */
#define WLEVELS 25        /* analyser steps: 0 = below -60 dB, 1..24 = -60..0 dB (a full-scale sine) in 2.5 dB */
#define W_STEP 2.5f
#define FFT_N 4096        /* 10.8 Hz bins */
static const int SPEC_EVERY[3] = { 11025, 5513, 2940 };   /* spec_rate options: frames between analyses (4, 8, 15 a second) */
static const float RATE_HZ[3] = { 4.0f, 8.0f, 15.0f };
static const float FALL_DB[3] = { 12.0f, 24.0f, 48.0f };   /* wave_time options: how fast a column falls, dB a second */

/* the real parameters, stored and saved; the sel_*, marker and curve keys are derived from these. The FX send each
 * have the same seven: on, mix (%), two settings (0..1), FULL/BAND, and the band's low and high edges (frequency positions) */
enum {
    HP_F, LS_F, LS_G, P1_F, P1_G, P1_Q, P2_F, P2_G, P2_Q, P3_F, P3_G, P3_Q, HS_F, HS_G, LP_F, OUT_G,
    P4_F, P4_G, P4_Q, P5_F, P5_G, P5_Q, HP_Q, LP_Q, HP_SL, LP_SL,
    ON_HP, ON_LS, ON_P1, ON_P2, ON_P3, ON_P4, ON_P5, ON_HS, ON_LP,
    FX0, NP = FX0 + 4 * 7
};
enum { FX_REV, FX_DLY, FX_CHO, FX_SHM, NFX };
enum { FO_ON, FO_MIX, FO_P1, FO_P2, FO_FULL, FO_LO, FO_HI };   /* offsets within an effect's seven */
#define FXP(fx, o) (FX0 + (fx) * 7 + (o))
static const char *const KEYS[NP] = {
    "hp_freq", "ls_freq", "ls_gain", "p1_freq", "p1_gain", "p1_q", "p2_freq", "p2_gain", "p2_q", "p3_freq", "p3_gain",
    "p3_q", "hs_freq", "hs_gain", "lp_freq", "out_gain", "p4_freq", "p4_gain", "p4_q", "p5_freq", "p5_gain", "p5_q",
    "hp_q", "lp_q", "hp_slope", "lp_slope", "on_hp", "on_ls", "on_p1", "on_p2", "on_p3", "on_p4", "on_p5", "on_hs", "on_lp",
    "fx_rev_on", "fx_rev_mix", "fx_rev_size", "fx_rev_damp", "fx_rev_full", "fx_rev_lo", "fx_rev_hi",
    "fx_dly_on", "fx_dly_mix", "fx_dly_time", "fx_dly_fb", "fx_dly_full", "fx_dly_lo", "fx_dly_hi",
    "fx_cho_on", "fx_cho_mix", "fx_cho_rate", "fx_cho_depth", "fx_cho_full", "fx_cho_lo", "fx_cho_hi",
    "fx_shm_on", "fx_shm_mix", "fx_shm_size", "fx_shm_amt", "fx_shm_full", "fx_shm_lo", "fx_shm_hi",
};
#define G GAIN
#define FXR 0, 0, 0, 0, 0, 0, 0
static const float PMIN[NP] = { 0, 0, -G, 0, -G, 0, 0, -G, 0, 0, -G, 0, 0, -G, 0, -24, 0, -G, 0, 0, -G, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, FXR, FXR, FXR, FXR };
#undef FXR
#define FXR 1, 100, 1, 1, 1, 1, 1
static const float PMAX[NP] = { 1, 1, G, 1, G, 1, 1, G, 1, 1, G, 1, 1, G, 1, 12, 1, G, 1, 1, G, 1, 1, 1, 2, 2,
                                1, 1, 1, 1, 1, 1, 1, 1, 1, FXR, FXR, FXR, FXR };
#undef FXR
#undef G
/* positions: 100 Hz 0.233, 150 Hz 0.2917, 400 Hz 0.4337, 1 kHz 0.5663, 2.5 kHz 0.699, 6 kHz 0.8257, 8 kHz 0.8674;
 * Q 1.0 = 0.3264, Q 0.7071 = 0.2324. FX: off, 30% mix, FULL, band 100 Hz..8 kHz */
static const float PDEF[NP] = { 0.0f, 0.233f, 0, 0.2917f, 0, 0.3264f, 0.4337f, 0, 0.3264f, 0.5663f, 0, 0.3264f,
                                0.8674f, 0, 1.0f, 0, 0.699f, 0, 0.3264f, 0.8257f, 0, 0.3264f, 0.2324f, 0.2324f, 0, 0,
                                1, 1, 1, 1, 1, 1, 1, 1, 1,
                                0, 30, 0.6f, 0.4f, 0, 0.233f, 0.8674f,    /* reverb: size, damping */
                                0, 30, 0.5f, 0.35f, 0, 0.233f, 0.8674f,   /* delay: time (20 ms..1.5 s), feedback */
                                0, 30, 0.4f, 0.5f, 0, 0.233f, 0.8674f,    /* chorus: rate (0.05..5 Hz), depth */
                                0, 30, 0.7f, 0.5f, 0, 0.233f, 0.8674f };  /* shimmer: size, amount */

/* option parameters: slopes, band on/off, and each effect's on and FULL/BAND (stored as option indices) */
static int is_opt(int i) {
    if (i >= HP_SL && i < FX0) return 1;
    if (i >= FX0) { int o = (i - FX0) % 7; return o == FO_ON || o == FO_FULL; }
    return 0;
}

/* bands in selector order (gen_params.py BANDS) */
enum { B_HP, B_LS, B_P1, B_P2, B_P3, B_P4, B_P5, B_HS, B_LP, NB };
enum { T_HP, T_LS, T_PEAK, T_HS, T_LP };
typedef struct { int type, freq, gain, q, slope, on; const char *mark; } band_t;
static const band_t BAND[NB] = {
    { T_HP, HP_F, -1, HP_Q, HP_SL, ON_HP, "m_hp" },   { T_LS, LS_F, LS_G, -1, -1, ON_LS, "m_ls" },
    { T_PEAK, P1_F, P1_G, P1_Q, -1, ON_P1, "m_p1" },  { T_PEAK, P2_F, P2_G, P2_Q, -1, ON_P2, "m_p2" },
    { T_PEAK, P3_F, P3_G, P3_Q, -1, ON_P3, "m_p3" },  { T_PEAK, P4_F, P4_G, P4_Q, -1, ON_P4, "m_p4" },
    { T_PEAK, P5_F, P5_G, P5_Q, -1, ON_P5, "m_p5" },  { T_HS, HS_F, HS_G, -1, -1, ON_HS, "m_hs" },
    { T_LP, LP_F, -1, LP_Q, LP_SL, ON_LP, "m_lp" },
};
/* Butterworth stage Qs per slope (12/24/48 dB/oct), lowest first; the resonance scales the last stage */
static const int NSTAGE[3] = { 1, 2, 4 };
static const float BW_Q[3][MAXST] = { { 0.7071f }, { 0.5412f, 1.3066f }, { 0.5098f, 0.6013f, 0.9000f, 2.5629f } };

typedef struct { float b0, b1, b2, a1, a2; } coef_t;
typedef struct { int n; coef_t c[MAXST]; } chain_t;   /* a band's biquads; n = 0 when the band does nothing */

typedef struct {   /* one Freeverb: per channel 8 parallel damped combs into 4 series allpasses */
    float comb[2][RV_COMBS][1640 + RV_SPREAD], cstore[2][RV_COMBS];
    float ap[2][RV_APS][556 + RV_SPREAD];
    int ci[2][RV_COMBS], ai[2][RV_APS];
} reverb_t;

typedef struct {   /* the four effects' state */
    coef_t bp[NFX][2];            /* each send's band: high-pass at its low edge, low-pass at its high edge */
    float bz[NFX][2][2][2];       /* [fx][filter][channel][state] */
    reverb_t rev, shm;            /* the reverb, and the shimmer's own reverb */
    float dly[2][DLY_MAX], dly_lp[2];
    int dly_w;
    float cho[2][CHO_LEN], cho_ph;
    int cho_w;
    float ps[2][PS_LEN], ps_ph, shm_fb[2];   /* the pitch shifter's line, head phase, and the shimmer's last output */
    int ps_w;
} fx_t;

typedef struct {
    float target[NP], cur[NP];
    int fresh;                 /* first block: jump to the targets */
    chain_t ch[NB];
    float z[NB][MAXST][2][2];  /* [band][stage][channel][state] */
    float out_lin;
    int sel;                   /* selected band (sel_band) */
    unsigned rev;              /* display_rev: bumped on every change the screen shows */
    int view_dirty;
    float disp[NP];            /* what the screen shows: glides to the targets over a few display polls (disp_tick) */
    int curve[NCOL], mark[NB], mark_sel, pill[NB];
    float qv[6];               /* the Q-Link controls' own positions (q_*: zones over a continuous knob) */
    float grel, grel_last;     /* g_rel: the relative gain control's position, and where the last move left it */
    int grel_idle;             /* display polls since g_rel last moved (it re-centres when left alone) */
    fx_t *fx;                  /* the effects' lines (about 1 MB: allocated apart) */
    /* the analyser (written by process() on the audio thread, read for the screen under the same engine lock) */
    int wview, wtime, spec_on;     /* 0 = PRE, 1 = POST; wave_time option; on/off */
    int spec_rate;                 /* spec_rate option */
    float spec_db[WCOL];           /* each column's level, falling smoothly between analyses */
    float ring[2][FFT_N];          /* the latest FFT_N frames, mono, PRE and POST */
    int rpos, rcount;              /* next write position; frames since the last analysis */
    float fre[FFT_N], fim[FFT_N];  /* FFT work space */
    unsigned char spec[WCOL];      /* shown levels */
} peq_t;

/* the FFT's tables, shared by every instance: filled by the first create(), read-only after that */
static int fft_ready;
static float hann[FFT_N], tw_re[FFT_N / 2], tw_im[FFT_N / 2];
static int spec_bin0[WCOL], spec_bin1[WCOL];   /* each column's FFT bins [bin0, bin1) */

static float col_cos[NCOL], col_sin[NCOL], col_cos2[NCOL], col_sin2[NCOL];

static float to_hz(float n) { return F_MIN * powf(F_RATIO, n); }
static float to_q(float n) { return Q_MIN * powf(Q_RATIO, n); }

static int key_index(const char *k) {
    for (int i = 0; i < NP; i++)
        if (!strcmp(k, KEYS[i])) return i;
    return -1;
}

static int is_freq(int i) {
    for (int b = 0; b < NB; b++)
        if (BAND[b].freq == i) return 1;
    return 0;
}

static int is_q(int i) {
    for (int b = 0; b < NB; b++)
        if (BAND[b].q == i) return 1;
    return 0;
}

static void set_value(peq_t *e, int i, float v) {
    if (v < PMIN[i]) v = PMIN[i];
    if (v > PMAX[i]) v = PMAX[i];
    if (is_opt(i)) v = roundf(v);   /* slopes, on/off and FULL/BAND are option indices */
    if (v == e->target[i]) return;
    e->target[i] = v;
    e->rev++;
    e->view_dirty = 1;
}

/* RBJ cookbook; a0 normalised out */
static void design(coef_t *c, int type, float hz, float gain_db, float q) {
    if (hz > 20000.0f) hz = 20000.0f;
    float w0 = 2.0f * (float)M_PI * hz / SR, cw = cosf(w0), sw = sinf(w0);
    float A = powf(10.0f, gain_db / 40.0f), alpha = sw / (2.0f * q);
    float b0, b1, b2, a0, a1, a2;
    switch (type) {
    case T_HP:
        b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    case T_LP:
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    case T_LS: {
        float s = 2 * sqrtf(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s;
        break;
    }
    case T_HS: {
        float s = 2 * sqrtf(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
        break;
    }
    default:   /* peak */
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
        break;
    }
    c->b0 = b0 / a0; c->b1 = b1 / a0; c->b2 = b2 / a0; c->a1 = a1 / a0; c->a2 = a2 / a0;
}

/* is band b switched on and doing something, from a parameter set (targets for the screen, glided values for audio) */
static int band_active(const float *p, int b) {
    const band_t *d = &BAND[b];
    if (p[d->on] < 0.5f) return 0;
    if (d->type == T_HP) return p[d->freq] > 0.0005f;
    if (d->type == T_LP) return p[d->freq] < 0.9995f;
    return fabsf(p[d->gain]) > 1e-3f;
}

static void design_band(chain_t *ch, const float *p, int b) {
    const band_t *d = &BAND[b];
    ch->n = 0;
    if (!band_active(p, b)) return;
    const float sh = 0.7071f;   /* shelf slope S = 1 */
    float hz = to_hz(p[d->freq]);
    if (d->type == T_HP || d->type == T_LP) {
        int sl = (int)p[d->slope];
        float res = to_q(p[d->q]) / 0.7071f;   /* 1 at the Butterworth position */
        ch->n = NSTAGE[sl];
        for (int s = 0; s < ch->n; s++)
            design(&ch->c[s], d->type, hz, 0, BW_Q[sl][s] * (s == ch->n - 1 ? res : 1.0f));
    } else {
        ch->n = 1;
        design(&ch->c[0], d->type, hz, p[d->gain], d->type == T_PEAK ? to_q(p[d->q]) : sh);
    }
}

/* one display step: the shown frequencies and gains move part of the way to their targets, so a band's pill, its ring and
 * the curve slide to a new place over a few polls (the wrapper reads display_rev about 10 times a second); everything else
 * shows at once. Returns whether anything moved. */
static int disp_tick(peq_t *e) {
    int moved = 0;
    for (int b = 0; b < NB; b++) {
        const int idx[2] = { BAND[b].freq, BAND[b].gain };
        const float snap[2] = { 0.002f, 0.1f };   /* a frequency position / dB close enough to land */
        for (int k = 0; k < 2; k++) {
            int i = idx[k];
            if (i < 0 || e->disp[i] == e->target[i]) continue;
            float d = e->target[i] - e->disp[i];
            e->disp[i] = fabsf(d) < snap[k] ? e->target[i] : e->disp[i] + d * 0.8f;   /* keeps up with a drag: ~3 polls */
            moved = 1;
        }
    }
    for (int i = 0; i < NP; i++)
        if (!is_freq(i) && e->disp[i] != e->target[i]) {
            int gain = 0;
            for (int b = 0; b < NB; b++) gain |= BAND[b].gain == i;
            if (!gain) { e->disp[i] = e->target[i]; moved = 1; }
        }
    if (moved) e->view_dirty = 1;
    return moved;
}

/* the screen's curve columns, markers and pills, from the shown values (disp) */
static void refresh_view(peq_t *e) {
    if (!e->view_dirty) return;
    e->view_dirty = 0;
    chain_t ch[NB];
    for (int b = 0; b < NB; b++) design_band(&ch[b], e->disp, b);
    for (int k = 0; k < NCOL; k++) {
        double db = 0;
        for (int b = 0; b < NB; b++)
            for (int s = 0; s < ch[b].n; s++) {
                const coef_t *q = &ch[b].c[s];   /* |H(e^jw)|^2 of one biquad */
                double nr = q->b0 + q->b1 * col_cos[k] + q->b2 * col_cos2[k], ni = q->b1 * col_sin[k] + q->b2 * col_sin2[k];
                double dr = 1 + q->a1 * col_cos[k] + q->a2 * col_cos2[k], di = q->a1 * col_sin[k] + q->a2 * col_sin2[k];
                db += 10 * log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-30);
            }
        long lv = lround(db) + (LEVELS - 1) / 2;
        e->curve[k] = lv < 0 ? 0 : lv > LEVELS - 1 ? LEVELS - 1 : (int)lv;
    }
    for (int b = 0; b < NB; b++) {
        int col = (int)(e->disp[BAND[b].freq] * NCOL);
        col = col < 0 ? 0 : col > NCOL - 1 ? NCOL - 1 : col;
        const band_t *d = &BAND[b];
        int shown = e->disp[d->on] >= 0.5f && !(d->type == T_HP && e->disp[d->freq] <= 0.0005f) &&
                    !(d->type == T_LP && e->disp[d->freq] >= 0.9995f);
        e->mark[b] = shown ? col : MARK_OFF;
        if (b == e->sel) e->mark_sel = col;   /* the selected band is always pointed at, even when off */
        /* its pill: the cell at its frequency (PILL_COLS across) and gain (rows every 3 dB; filters ride the 0 dB row) */
        int row = d->gain >= 0 ? PAD_ZERO - (int)lroundf(e->disp[d->gain] / 3.0f) : PAD_ZERO;
        row = row < 0 ? 0 : row > PAD_ROWS - 1 ? PAD_ROWS - 1 : row;
        int pc = (int)(e->disp[d->freq] * PILL_COLS);
        pc = pc < 0 ? 0 : pc > PILL_COLS - 1 ? PILL_COLS - 1 : pc;
        e->pill[b] = shown ? row * PILL_COLS + pc : PILL_OFF;
    }
}

/* glide the parameters one block and redesign the bands that moved */
static void update(peq_t *e) {
    int moved = e->fresh;
    for (int i = 0; i < NP; i++) {
        float d = e->target[i] - e->cur[i];
        if (e->fresh || is_opt(i) || fabsf(d) < 1e-5f) e->cur[i] = e->target[i];   /* options switch at once */
        else e->cur[i] += d * GLIDE;
        if (d != 0) moved = 1;
    }
    e->fresh = 0;
    if (!moved) return;
    for (int b = 0; b < NB; b++) {
        int was = e->ch[b].n;
        design_band(&e->ch[b], e->cur, b);
        if (e->ch[b].n != was) memset(e->z[b], 0, sizeof e->z[b]);   /* switched on or re-sloped: start clean */
    }
    e->out_lin = powf(10.0f, e->cur[OUT_G] / 20.0f);
    if (e->fx)   /* each send's band: a high-pass at its low edge and a low-pass at its high edge (12 dB/oct) */
        for (int k = 0; k < NFX; k++) {
            design(&e->fx->bp[k][0], T_HP, to_hz(e->cur[FXP(k, FO_LO)]), 0, 0.7071f);
            design(&e->fx->bp[k][1], T_LP, to_hz(e->cur[FXP(k, FO_HI)]), 0, 0.7071f);
        }
}

static void fft_init(void) {
    if (fft_ready) return;
    for (int i = 0; i < FFT_N; i++) hann[i] = 0.5f - 0.5f * cosf(2 * (float)M_PI * i / FFT_N);
    for (int i = 0; i < FFT_N / 2; i++) { tw_re[i] = cosf(2 * (float)M_PI * i / FFT_N); tw_im[i] = -sinf(2 * (float)M_PI * i / FFT_N); }
    for (int k = 0; k < WCOL; k++) {   /* column k is the log-axis slice [k/WCOL, (k+1)/WCOL) */
        int b0 = (int)lroundf(to_hz((float)k / WCOL) * FFT_N / SR), b1 = (int)lroundf(to_hz((float)(k + 1) / WCOL) * FFT_N / SR);
        if (b0 < 1) b0 = 1;
        if (b1 <= b0) b1 = b0 + 1;   /* low columns are narrower than a bin: they share the nearest one */
        spec_bin0[k] = b0; spec_bin1[k] = b1;
    }
    fft_ready = 1;
}

/* in-place radix-2 FFT of re/im (FFT_N points) */
static void fft(float *re, float *im) {
    for (int i = 1, j = 0; i < FFT_N; i++) {   /* bit reversal */
        int bit = FFT_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= FFT_N; len <<= 1) {
        int half = len >> 1, step = FFT_N / len;
        for (int i = 0; i < FFT_N; i += len)
            for (int k = 0; k < half; k++) {
                float wr = tw_re[k * step], wi = tw_im[k * step];
                float xr = re[i + k + half] * wr - im[i + k + half] * wi, xi = re[i + k + half] * wi + im[i + k + half] * wr;
                re[i + k + half] = re[i + k] - xr; im[i + k + half] = im[i + k] - xi;
                re[i + k] += xr; im[i + k] += xi;
            }
    }
}

/* one analysis of the latest FFT_N frames of the chosen side: rise at once, fall at wave_time's speed */
static void analyse(peq_t *e) {
    const float *r = e->ring[e->wview];
    for (int i = 0; i < FFT_N; i++) { e->fre[i] = r[(e->rpos + i) % FFT_N] * hann[i]; e->fim[i] = 0; }
    fft(e->fre, e->fim);
    const float ref = (FFT_N / 4.0f) * (FFT_N / 4.0f);   /* a full-scale sine's peak bin, Hann window: 0 dB */
    const float fall = FALL_DB[e->wtime] / RATE_HZ[e->spec_rate];   /* dB a column may fall in one analysis */
    int changed = 0;
    for (int k = 0; k < WCOL; k++) {
        float p = 0;
        for (int b = spec_bin0[k]; b < spec_bin1[k] && b < FFT_N / 2; b++) p += e->fre[b] * e->fre[b] + e->fim[b] * e->fim[b];
        float db = 10 * log10f(p / ref + 1e-12f);
        if (db < -61) db = -61;
        if (db < e->spec_db[k] - fall) db = e->spec_db[k] - fall;   /* rise at once, fall smoothly */
        e->spec_db[k] = db;
        int lv = db < -60 ? 0 : (int)ceilf((db + 60) / W_STEP);
        lv = lv > WLEVELS - 1 ? WLEVELS - 1 : lv < 0 ? 0 : lv;
        if (lv != e->spec[k]) { e->spec[k] = (unsigned char)lv; changed = 1; }
    }
    if (changed) e->rev++;   /* silence: once everything has fallen to 0, nothing changes and nothing is redrawn */
}

/* one block into the analyser, from the block's input and output (interleaved int16 stereo) */
static void spec_block(peq_t *e, const int16_t *in, const int16_t *out, int frames) {
    for (int n = 0; n < frames; n++) {
        e->ring[0][e->rpos] = (in[2 * n] + in[2 * n + 1]) * (0.5f / 32768.0f);
        e->ring[1][e->rpos] = (out[2 * n] + out[2 * n + 1]) * (0.5f / 32768.0f);
        e->rpos = (e->rpos + 1) % FFT_N;
    }
    if ((e->rcount += frames) >= SPEC_EVERY[e->spec_rate]) {
        e->rcount = 0;
        analyse(e);
    }
}

static void *create(const char *data_dir) {
    (void)data_dir;
    peq_t *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    for (int i = 0; i < NP; i++) e->target[i] = PDEF[i];
    e->fx = calloc(1, sizeof *e->fx);   /* no memory: the EQ still works, the effects stay silent */
    e->sel = B_P3;
    e->wview = 1;
    e->wtime = 1;
    e->spec_on = 1;
    e->spec_rate = 1;
    for (int k = 0; k < WCOL; k++) e->spec_db[k] = -61;
    fft_init();
    e->fresh = 1;
    update(e);
    memcpy(e->disp, e->target, sizeof e->disp);   /* qv starts at 0: q_pos() moves each into its setting's zone */
    e->grel = e->grel_last = 0.5f;
    for (int k = 0; k < NCOL; k++) {   /* the same values every time, so a second instance rewriting them is harmless */
        double w = 2 * M_PI * to_hz((k + 0.5f) / NCOL) / SR;
        col_cos[k] = (float)cos(w); col_sin[k] = (float)sin(w); col_cos2[k] = (float)cos(2 * w); col_sin2[k] = (float)sin(2 * w);
    }
    e->view_dirty = 1;
    return e;
}

static void destroy(void *inst) {
    peq_t *e = inst;
    if (e) free(e->fx);
    free(e);
}

static void midi(void *inst, const uint8_t *msg, int len) { (void)inst; (void)msg; (void)len; }

/* sel_* -> the selected band's parameter index, -1 when that band has none, -2 for another key */
static int sel_target(const peq_t *e, const char *key) {
    const band_t *d = &BAND[e->sel];
    if (!strcmp(key, "sel_freq")) return d->freq;
    if (!strcmp(key, "sel_gain")) return d->gain;
    if (!strcmp(key, "sel_q")) return d->q;
    if (!strcmp(key, "sel_on")) return d->on;
    if (!strcmp(key, "sel_slope")) return d->slope;
    return -2;
}

static void reset_band(peq_t *e) {
    const band_t *d = &BAND[e->sel];
    const int idx[5] = { d->freq, d->gain, d->q, d->slope, d->on };
    for (int k = 0; k < 5; k++)
        if (idx[k] >= 0) set_value(e, idx[k], PDEF[idx[k]]);
}

/* The Q-Link versions of the short option lists (q_*): a continuous knob split into equal zones, one per option, so a
 * Q-Link turn registers every event and the setting changes where a zone boundary is crossed (an option list's own
 * Q-Link stepping counts events, which felt uneven). Touch keeps using the option lists. */
enum { QV_VIEW, QV_SPEED, QV_RATE, QV_SPEC, QV_ON, QV_SLOPE, NQ };
static const char *const QKEYS[NQ] = { "q_view", "q_speed", "q_rate", "q_spec", "q_on", "q_slope" };
static const int QN[NQ] = { 2, 3, 3, 2, 2, 3 };
static const char *const QLAB[NQ][3] = { { "PRE", "POST" }, { "SLOW", "MED", "FAST" }, { "4/S", "8/S", "15/S" },
                                         { "OFF", "ON" }, { "OFF", "ON" }, { "12", "24", "48" } };

static int qkey(const char *key) {
    for (int i = 0; i < NQ; i++)
        if (!strcmp(key, QKEYS[i])) return i;
    return -1;
}

/* the setting a q_* knob stands for, as an option index (-1: the selected band has none, e.g. a peak's slope) */
static int q_option(const peq_t *e, int i) {
    const band_t *d = &BAND[e->sel];
    switch (i) {
    case QV_VIEW: return e->wview;
    case QV_SPEED: return e->wtime;
    case QV_RATE: return e->spec_rate;
    case QV_SPEC: return e->spec_on;
    case QV_ON: return (int)e->target[d->on];
    default: return d->slope >= 0 ? (int)e->target[d->slope] : -1;
    }
}

static void set_param(void *inst, const char *key, const char *val);

static void q_apply(peq_t *e, int i, int opt) {
    char v[16];
    snprintf(v, sizeof v, "%d", opt);
    static const char *const SET[NQ] = { "wave_view", "wave_time", "spec_rate", "spec_on", "sel_on", "sel_slope" };
    set_param(e, SET[i], v);
}

/* the knob's position: its own while it agrees with the setting, else the middle of the setting's zone */
static float q_pos(peq_t *e, int i) {
    int cur = q_option(e, i);
    if (cur < 0) return e->qv[i];
    int zone = (int)(e->qv[i] * QN[i]);
    if (zone > QN[i] - 1) zone = QN[i] - 1;
    if (zone != cur) e->qv[i] = (cur + 0.5f) / QN[i];
    return e->qv[i];
}

static void set_param(void *inst, const char *key, const char *val) {
    peq_t *e = inst;
    if (!strcmp(key, "theme_sel") || !strcmp(key, "theme_apply")) return;   /* unused since 0.12.0: one look */
    if (!strcmp(key, "g_rel")) {   /* nudge the selected band by how far the control moved */
        float v = (float)atof(val);
        v = v < 0 ? 0 : v > 1 ? 1 : v;
        float dv = v - e->grel_last;
        const band_t *d = &BAND[e->sel];
        if (d->gain >= 0) set_value(e, d->gain, e->target[d->gain] + dv * GREL_SPAN);
        else if (d->q >= 0) set_value(e, d->q, e->target[d->q] + dv * GREL_Q_SPAN);
        e->grel = e->grel_last = v;
        e->grel_idle = 0;
        return;
    }
    int qi = qkey(key);
    if (qi >= 0) {
        float v = (float)atof(val);
        e->qv[qi] = v < 0 ? 0 : v > 1 ? 1 : v;
        int opt = (int)(e->qv[qi] * QN[qi]);
        if (opt > QN[qi] - 1) opt = QN[qi] - 1;
        int cur = q_option(e, qi);
        if (cur >= 0 && opt != cur) q_apply(e, qi, opt);
        return;
    }
    if (!strcmp(key, "state")) {   /* "key=value;key=value;..." from get_param("state"); unknown keys are skipped */
        char buf[8192], *save = NULL;   /* the wrapper's chunk size */
        snprintf(buf, sizeof buf, "%s", val);
        for (char *t = strtok_r(buf, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
            char *eq = strchr(t, '=');
            if (!eq) continue;
            *eq = 0;
            int i = key_index(t);
            if (i >= 0) set_value(e, i, (float)atof(eq + 1));
            else if (!strcmp(t, "sel")) set_param(e, "sel_band", eq + 1);
            else if (!strcmp(t, "wview")) set_param(e, "wave_view", eq + 1);
            else if (!strcmp(t, "wtime")) set_param(e, "wave_time", eq + 1);
            else if (!strcmp(t, "spec")) set_param(e, "spec_on", eq + 1);
            else if (!strcmp(t, "srate")) set_param(e, "spec_rate", eq + 1);
        }
        return;
    }
    if (!strcmp(key, "wave_view")) {   /* the other side shows from the next analysis */
        int v = atoi(val) ? 1 : 0;
        if (v != e->wview) { e->wview = v; e->rev++; }
        return;
    }
    if (!strcmp(key, "wave_time")) {
        int v = atoi(val);
        v = v < 0 ? 0 : v > 2 ? 2 : v;
        if (v != e->wtime) { e->wtime = v; e->rev++; }
        return;
    }
    if (!strcmp(key, "spec_on")) {   /* off: the bars clear and the analyser stops */
        int v = atoi(val) ? 1 : 0;
        if (v != e->spec_on) {
            e->spec_on = v;
            memset(e->spec, 0, sizeof e->spec);
            for (int k = 0; k < WCOL; k++) e->spec_db[k] = -61;
            e->rev++;
        }
        return;
    }
    if (!strcmp(key, "spec_rate")) {
        int v = atoi(val);
        e->spec_rate = v < 0 ? 0 : v > 2 ? 2 : v;
        return;
    }
    if (!strcmp(key, "wave_cap")) return;   /* unused since 0.5.0 */
    if (!strcmp(key, "sel_band")) {
        int s = atoi(val);
        s = s < 0 ? 0 : s > NB - 1 ? NB - 1 : s;
        if (s != e->sel) { e->sel = s; e->rev++; e->view_dirty = 1; }
        return;
    }
    if (!strcmp(key, "pad")) {
        int idx = atoi(val);
        if (idx < 0 || idx >= PAD_COLS * PAD_ROWS) return;
        int col = idx % PAD_COLS, row = idx / PAD_COLS;
        refresh_view(e);
        for (int b = 0; b < NB; b++)   /* a band's pill: select it (the selected band's own pill: leave it be) */
            if (e->pill[b] == row * PILL_COLS + col * PILL_COLS / PAD_COLS) {
                if (b != e->sel) { e->sel = b; e->rev++; e->view_dirty = 1; }
                return;
            }
        const band_t *d = &BAND[e->sel];
        set_value(e, d->freq, (col + 0.5f) / PAD_COLS);
        if (d->gain >= 0) set_value(e, d->gain, (PAD_ZERO - row) * 3.0f);
        set_value(e, d->on, 1);   /* placing a band that was off switches it on */
        e->rev++;                 /* the grid reads back where the band really is, even when nothing changed */
        return;
    }
    if (!strncmp(key, "pick_", 5)) {   /* a tap on a band's pill (momentary): select that band */
        if (atof(val) < 0.5) return;
        for (int b = 0; b < NB; b++)
            if (!strcmp(key + 5, BAND[b].mark + 2) && b != e->sel) { e->sel = b; e->rev++; e->view_dirty = 1; }
        return;
    }
    if (!strcmp(key, "band_reset")) {   /* momentary: acts on the press, not on the spring back to 0 */
        if (atof(val) >= 0.5) reset_band(e);
        return;
    }
    int i = sel_target(e, key);
    if (i == -2) i = key_index(key);
    if (i >= 0) set_value(e, i, (float)atof(val));   /* curve columns and markers are read-only: no index, ignored */
}

static int fmt_hz(char *buf, int len, float hz) {
    if (hz < 999.5f) return snprintf(buf, len, "%.0f Hz", hz);   /* 999.7 Hz would print as "1000 Hz" */
    if (hz < 9995.0f) return snprintf(buf, len, "%.2f kHz", hz / 1000.0f);
    return snprintf(buf, len, "%.1f kHz", hz / 1000.0f);
}

/* value text of a real parameter, 0 = let the wrapper format it */
static int display(const peq_t *e, int i, char *buf, int len) {
    float v = e->target[i];
    if (i == HP_F && v <= 0.0005f) return snprintf(buf, len, "Off");
    if (i == LP_F && v >= 0.9995f) return snprintf(buf, len, "Off");
    if (is_freq(i)) return fmt_hz(buf, len, to_hz(v));
    if (is_q(i)) return snprintf(buf, len, "%.2f", to_q(v));
    if (i >= FX0) {   /* the effects' settings in their own units */
        int k = (i - FX0) / 7, o = (i - FX0) % 7;
        if (o == FO_LO || o == FO_HI) return fmt_hz(buf, len, to_hz(v));
        if (o == FO_MIX) return snprintf(buf, len, "%.0f%%", v);
        if (o == FO_P1 && k == FX_DLY) return snprintf(buf, len, "%.0f ms", 20.0f * powf(75.0f, v));
        if (o == FO_P1 && k == FX_CHO) return snprintf(buf, len, "%.2f Hz", 0.05f * powf(100.0f, v));
        if (o == FO_P1 || o == FO_P2) return snprintf(buf, len, "%.0f%%", v * 100);
    }
    return 0;
}

static int get_param(void *inst, const char *key, char *buf, int len) {
    peq_t *e = inst;
    if (!strcmp(key, "state")) {
        int n = 0;
        for (int i = 0; i < NP && n < len; i++) n += snprintf(buf + n, len - n, "%s%s=%.5f", i ? ";" : "", KEYS[i], e->target[i]);
        if (n < len) n += snprintf(buf + n, len - n, ";sel=%d;wview=%d;wtime=%d;spec=%d;srate=%d", e->sel, e->wview, e->wtime,
                                   e->spec_on, e->spec_rate);
        return n < len ? n : 0;
    }
    if (!strcmp(key, "display_rev")) {   /* read every ~100 ms by the wrapper: the display's clock for the slides */
        if (disp_tick(e)) e->rev++;
        if (e->grel != 0.5f && ++e->grel_idle >= 4) {   /* g_rel left alone ~0.4 s: back to the middle */
            e->grel = e->grel_last = 0.5f;
            e->rev++;
        }
        return snprintf(buf, len, "%u", e->rev);
    }
    if (!strcmp(key, "g_rel")) return snprintf(buf, len, "%.5f", e->grel);
    if (!strcmp(key, "theme_sel") || !strcmp(key, "theme_apply")) return snprintf(buf, len, "0");   /* unused */
    if (!strcmp(key, "theme_status")) return snprintf(buf, len, "-");
    if (!strcmp(key, "pp_sel")) {   /* the selected band's ring: its pill's cell */
        refresh_view(e);
        return snprintf(buf, len, "%d", e->pill[e->sel]);
    }
    if (!strcmp(key, "g_rel_display")) {   /* the selected band's gain (a filter's resonance) */
        const band_t *d = &BAND[e->sel];
        if (d->gain >= 0) return snprintf(buf, len, "%+.1f dB", e->target[d->gain]);
        return snprintf(buf, len, "Q %.2f", to_q(e->target[d->q]));
    }
    int qi = qkey(key);
    if (qi >= 0) return snprintf(buf, len, "%.5f", q_pos(e, qi));
    size_t kl0 = strlen(key);
    if (kl0 > 10 && !strcmp(key + kl0 - 8, "_display") && !strncmp(key, "q_", 2)) {
        char base[16];
        snprintf(base, sizeof base, "%.*s", (int)(kl0 - 8), key);
        int i = qkey(base), cur = i >= 0 ? q_option(e, i) : -1;
        return snprintf(buf, len, "%s", cur >= 0 ? QLAB[i][cur] : "-");
    }
    if (!strncmp(key, "fxm_", 4) && strlen(key) == 10) {   /* fxm_<fx>_lo / _hi: an effect's range on the graph */
        static const char *const FXN[NFX] = { "rev", "dly", "cho", "shm" };
        for (int k = 0; k < NFX; k++) {
            if (strncmp(key + 4, FXN[k], 3)) continue;
            int hi = !strcmp(key + 7, "_hi");
            if (e->target[FXP(k, FO_ON)] < 0.5f) return snprintf(buf, len, "%d", FXM_COLS);   /* off: hidden */
            if (e->target[FXP(k, FO_FULL)] < 0.5f) return snprintf(buf, len, "%d", hi ? FXM_COLS - 1 : 0);   /* FULL: the ends */
            int c = (int)(e->target[FXP(k, hi ? FO_HI : FO_LO)] * FXM_COLS);
            return snprintf(buf, len, "%d", c < 0 ? 0 : c > FXM_COLS - 1 ? FXM_COLS - 1 : c);
        }
        return 0;
    }
    if (!strncmp(key, "pp_", 3)) {   /* a band's pill: its grid cell, or none */
        refresh_view(e);
        for (int b = 0; b < NB; b++)
            if (!strcmp(key + 3, BAND[b].mark + 2)) return snprintf(buf, len, "%d", e->pill[b]);
        return 0;
    }
    if (!strcmp(key, "wave_cap")) return snprintf(buf, len, "0");
    if (!strcmp(key, "wave_status")) return snprintf(buf, len, "-");   /* unused since 0.5.0; never changes */
    if (!strcmp(key, "wave_view")) return snprintf(buf, len, "%d", e->wview);
    if (!strcmp(key, "wave_time")) return snprintf(buf, len, "%d", e->wtime);
    if (!strcmp(key, "spec_on")) return snprintf(buf, len, "%d", e->spec_on);
    if (!strcmp(key, "spec_rate")) return snprintf(buf, len, "%d", e->spec_rate);
    if (!strcmp(key, "pad")) {   /* the cell the selected band is shown in (it slides with the pill) */
        const band_t *d = &BAND[e->sel];
        int col = (int)(e->disp[d->freq] * PAD_COLS);
        col = col < 0 ? 0 : col > PAD_COLS - 1 ? PAD_COLS - 1 : col;
        int row = d->gain >= 0 ? PAD_ZERO - (int)lroundf(e->disp[d->gain] / 3.0f) : PAD_ZERO;
        row = row < 0 ? 0 : row > PAD_ROWS - 1 ? PAD_ROWS - 1 : row;
        return snprintf(buf, len, "%d", row * PAD_COLS + col);
    }
    if (key[0] == 'w' && strlen(key) == 3 && key[1] >= '0' && key[1] <= '9') {
        int k = atoi(key + 1);
        if (k < 0 || k >= WCOL) return 0;
        return snprintf(buf, len, "%d", e->spec[k]);
    }
    if (!strcmp(key, "sel_band")) return snprintf(buf, len, "%d", e->sel);
    if (!strcmp(key, "band_reset") || !strcmp(key, "band_prev") || !strcmp(key, "band_next") || !strncmp(key, "pick_", 5))
        return snprintf(buf, len, "0");
    if (key[0] == 'c' && strlen(key) == 3 && key[1] >= '0' && key[1] <= '9') {
        int k = atoi(key + 1);
        if (k < 0 || k >= NCOL) return 0;
        refresh_view(e);
        return snprintf(buf, len, "%d", e->curve[k]);
    }
    if (!strcmp(key, "m_sel")) { refresh_view(e); return snprintf(buf, len, "%d", e->mark_sel); }
    for (int b = 0; b < NB; b++)
        if (!strcmp(key, BAND[b].mark)) { refresh_view(e); return snprintf(buf, len, "%d", e->mark[b]); }
    size_t kl = strlen(key);
    if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
        char base[32];
        if (kl - 8 >= sizeof base) return 0;
        memcpy(base, key, kl - 8);
        base[kl - 8] = 0;
        int i = sel_target(e, base);
        if (i == -1) return snprintf(buf, len, "-");   /* e.g. the gain of a high-pass filter */
        if (i == -2) i = key_index(base);
        if (i < 0) return 0;
        if (!strcmp(base, "sel_gain")) return snprintf(buf, len, "%+.1f dB", e->target[i]);
        return display(e, i, buf, len);
    }
    int i = sel_target(e, key);
    if (i == -1) {   /* nothing to edit on this band: report a resting value */
        if (!strcmp(key, "sel_gain") || !strcmp(key, "sel_slope")) return snprintf(buf, len, "0");
        return snprintf(buf, len, "%g", PDEF[P1_Q]);
    }
    if (i == -2) i = key_index(key);
    if (i < 0) return 0;
    if (is_opt(i)) return snprintf(buf, len, "%d", (int)e->target[i]);   /* option index */
    return snprintf(buf, len, "%.5f", e->target[i]);
}

static void render(void *inst, int16_t *out_lr, int frames) { (void)inst; memset(out_lr, 0, (size_t)frames * 4); }

static inline float biquad(const coef_t *q, float *z, float x) {   /* Direct Form II transposed */
    float y = q->b0 * x + z[0];
    z[0] = q->b1 * x - q->a1 * y + z[1];
    z[1] = q->b2 * x - q->a2 * y;
    return y;
}

static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0 : x; }   /* denormals are slow on the device's VFP */

/* one sample of one channel through a Freeverb: fb = comb feedback (room size), damp = comb low-pass */
static float rv_tick(reverb_t *r, int ch, float in, float fb, float damp) {
    float acc = 0;
    for (int k = 0; k < RV_COMBS; k++) {
        int len = RV_COMB_LEN[k] + (ch ? RV_SPREAD : 0), i = r->ci[ch][k];
        float y = r->comb[ch][k][i];
        r->cstore[ch][k] = flush(y * (1 - damp) + r->cstore[ch][k] * damp);
        r->comb[ch][k][i] = in + r->cstore[ch][k] * fb;
        r->ci[ch][k] = i + 1 < len ? i + 1 : 0;
        acc += y;
    }
    for (int k = 0; k < RV_APS; k++) {
        int len = RV_AP_LEN[k] + (ch ? RV_SPREAD : 0), i = r->ai[ch][k];
        float b = r->ap[ch][k][i];
        r->ap[ch][k][i] = flush(acc + b * 0.5f);
        r->ai[ch][k] = i + 1 < len ? i + 1 : 0;
        acc = b - acc;
    }
    return acc;
}

static inline float line_read(const float *buf, int size, int w, float delay) {   /* linear interpolation, delay in samples */
    float p = (float)w - delay;
    while (p < 0) p += size;
    int i0 = (int)p, i1 = i0 + 1 < size ? i0 + 1 : 0;
    float f = p - i0;
    return buf[i0] + (buf[i1] - buf[i0]) * f;
}

/* one stereo sample through the four effects, from the EQ'd signal; returns the wet signal to add */
static void fx_tick(peq_t *e, float dl, float dr, float *wl, float *wr) {
    fx_t *f = e->fx;
    const float *p = e->cur;
    float in[2] = { dl, dr }, out[2] = { 0, 0 };
    for (int k = 0; k < NFX; k++) {
        if (p[FXP(k, FO_ON)] < 0.5f || p[FXP(k, FO_MIX)] < 0.05f) continue;
        float s[2];
        for (int c = 0; c < 2; c++) {   /* the send: the whole signal, or only its band */
            s[c] = in[c];
            if (p[FXP(k, FO_FULL)] >= 0.5f) {
                s[c] = biquad(&f->bp[k][0], f->bz[k][0][c], s[c]);
                s[c] = biquad(&f->bp[k][1], f->bz[k][1][c], s[c]);
            }
        }
        float y[2], mix = p[FXP(k, FO_MIX)] * 0.01f, a = p[FXP(k, FO_P1)], b = p[FXP(k, FO_P2)];
        switch (k) {
        case FX_REV:
            for (int c = 0; c < 2; c++) y[c] = 3.0f * rv_tick(&f->rev, c, (s[0] + s[1]) * 0.0075f, 0.70f + 0.28f * a, 0.4f * b);
            break;
        case FX_DLY: {   /* stereo delay, its feedback darkened a little on each repeat */
            float d = 20.0f * powf(75.0f, a) * (SR / 1000.0f);
            if (d > DLY_MAX - 2) d = DLY_MAX - 2;
            for (int c = 0; c < 2; c++) {
                y[c] = line_read(f->dly[c], DLY_MAX, f->dly_w, d);
                f->dly_lp[c] = flush(f->dly_lp[c] + 0.35f * (y[c] - f->dly_lp[c]));
                f->dly[c][f->dly_w] = s[c] + f->dly_lp[c] * 0.9f * b;
            }
            f->dly_w = f->dly_w + 1 < DLY_MAX ? f->dly_w + 1 : 0;
            break;
        }
        case FX_CHO: {   /* two voices, their delays swept by quadrature LFOs (one per channel) */
            float rate = 0.05f * powf(100.0f, a), depth = (0.5f + 7.5f * b) * (SR / 1000.0f);
            f->cho_ph += rate / SR;
            if (f->cho_ph >= 1) f->cho_ph -= 1;
            for (int c = 0; c < 2; c++) {
                f->cho[c][f->cho_w] = s[c];
                float lfo = sinf(2 * (float)M_PI * (f->cho_ph + 0.25f * c));
                y[c] = line_read(f->cho[c], CHO_LEN, f->cho_w, 12.0f * (SR / 1000.0f) + depth * (0.5f + 0.5f * lfo));
            }
            f->cho_w = f->cho_w + 1 < CHO_LEN ? f->cho_w + 1 : 0;
            break;
        }
        default: {   /* shimmer: a reverb whose output comes back an octave up (two crossfading read heads) */
            const float win = 2048.0f;
            f->ps_ph += 1.0f / win;   /* reading twice as fast as writing: the delay shrinks by one sample a sample */
            if (f->ps_ph >= 1) f->ps_ph -= 1;
            float ph2 = f->ps_ph + 0.5f >= 1 ? f->ps_ph - 0.5f : f->ps_ph + 0.5f;
            float g1 = sinf((float)M_PI * f->ps_ph), g2 = sinf((float)M_PI * ph2);
            for (int c = 0; c < 2; c++) {
                float up = line_read(f->ps[c], PS_LEN, f->ps_w, (1 - f->ps_ph) * win) * g1 +
                           line_read(f->ps[c], PS_LEN, f->ps_w, (1 - ph2) * win) * g2;
                float rin = (s[0] + s[1]) * 0.0075f + up * 0.5f * b * 0.015f;
                y[c] = 3.0f * rv_tick(&f->shm, c, rin, 0.80f + 0.17f * a, 0.3f);
                f->ps[c][f->ps_w] = fmaxf(-2.0f, fminf(2.0f, y[c]));   /* the octave loop can never run away */
            }
            f->ps_w = f->ps_w + 1 < PS_LEN ? f->ps_w + 1 : 0;
            break;
        }
        }
        out[0] += y[0] * mix;
        out[1] += y[1] * mix;
    }
    *wl = out[0];
    *wr = out[1];
}

static void process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    peq_t *e = inst;
    update(e);
    float eq[2][128];
    if (frames > 128) frames = 128;   /* the wrapper's DSP block */
    for (int c = 0; c < 2; c++)   /* the EQ */
        for (int n = 0; n < frames; n++) {
            float x = in_lr[2 * n + c] * (1.0f / 32768.0f);
            for (int b = 0; b < NB; b++)
                for (int s = 0; s < e->ch[b].n; s++) x = biquad(&e->ch[b].c[s], e->z[b][s][c], x);
            eq[c][n] = x;
        }
    int any_fx = 0;
    for (int k = 0; k < NFX; k++) any_fx |= e->target[FXP(k, FO_ON)] >= 0.5f;
    for (int n = 0; n < frames; n++) {   /* the effects, added to it; then the output gain */
        float wl = 0, wr = 0;
        if (any_fx && e->fx) fx_tick(e, eq[0][n], eq[1][n], &wl, &wr);
        float y[2] = { (eq[0][n] + wl) * e->out_lin * 32768.0f, (eq[1][n] + wr) * e->out_lin * 32768.0f };
        for (int c = 0; c < 2; c++)
            out_lr[2 * n + c] = y[c] >= 32767.0f ? 32767 : y[c] <= -32768.0f ? -32768 : (int16_t)lrintf(y[c]);
    }
    if (e->spec_on) spec_block(e, in_lr, out_lr, frames);
    /* silence decays the filter state towards denormals, which are slow on the device's VFP: flush them */
    float *z = &e->z[0][0][0][0];
    for (size_t k = 0; k < sizeof e->z / sizeof e->z[0][0][0][0]; k++) z[k] = flush(z[k]);
    if (e->fx) {
        float *bz = &e->fx->bz[0][0][0][0];
        for (size_t k = 0; k < sizeof e->fx->bz / sizeof *bz; k++) bz[k] = flush(bz[k]);
    }
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render, process, NULL };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

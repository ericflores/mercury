/* test_ldpc_early_stop -- early termination of LDPC decodes that are going
 * nowhere (freedv_set_ldpc_early_stop, docs/improvements/23-rx-effort.md).
 *
 * What must hold:
 *  - off unless asked for: a decode of noise runs to the code's iteration
 *    limit, as always;
 *  - on, a decode of noise is abandoned within 30 iterations on the long
 *    codes, and runs to the limit as always on a short one;
 *  - on, a codeword that decodes comes out the same, in the same number of
 *    iterations, as with it off (the rule never fires on a decode that is
 *    converging).
 *
 * The no-loss check at each mode's cliff is a bench run, not a unit test:
 * docs/RX-EFFORT-FINDINGS.md.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "freedv_api.h"
#include "ldpc_codes.h"
#include "mpdecode_core.h"

void setUp(void) {}
void tearDown(void) {}

static uint64_t rng_s = 424242424242ULL;
static double uni(void)
{
    rng_s ^= rng_s >> 12; rng_s ^= rng_s << 25; rng_s ^= rng_s >> 27;
    return (double)((rng_s * 2685821657736338717ULL) >> 11) / 9007199254740992.0;
}
static double gauss(void)
{
    double u = uni(), v = uni();
    if (u < 1e-300) u = 1e-300;
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

/* The codes of DATAC1/DATAC3 and of DATAC17/QAM16C2. */
static const char *CODES[] = { "H_4096_8192_3d", "H_1024_2048_4f", "H_16200_9720" };

static void test_off_by_default(void)
{
    struct LDPC ldpc;
    ldpc_codes_setup(&ldpc, (char *)CODES[0]);
    TEST_ASSERT_EQUAL_INT(0, ldpc.early_stop);
    struct freedv *f = freedv_open(FREEDV_MODE_DATAC1);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, freedv_get_ldpc_early_stop(f));
    freedv_set_ldpc_early_stop(f, 1);
    TEST_ASSERT_EQUAL_INT(1, freedv_get_ldpc_early_stop(f));
    freedv_close(f);
}

static void test_noise_stops_early_only_when_on(void)
{
    for (unsigned c = 0; c < sizeof(CODES) / sizeof(CODES[0]); c++) {
        struct LDPC ldpc;
        ldpc_codes_setup(&ldpc, (char *)CODES[c]);
        int n = ldpc.CodeLength;
        float *llr = malloc(sizeof(float) * (size_t)n);
        uint8_t *out = malloc((size_t)n);
        for (int trial = 0; trial < 3; trial++) {
            for (int i = 0; i < n; i++) llr[i] = (float)(2.0 * gauss());
            int pcc = 0;
            ldpc.early_stop = 0;
            int it_off = run_ldpc_decoder(&ldpc, out, llr, &pcc);
            ldpc.early_stop = 1;
            int it_on = run_ldpc_decoder(&ldpc, out, llr, &pcc);
            char msg[120];
            snprintf(msg, sizeof(msg), "%s: noise ran %d iterations off, %d on (limit %d)", CODES[c], it_off, it_on,
                     ldpc.max_iter);
            TEST_ASSERT_EQUAL_INT_MESSAGE(ldpc.max_iter, it_off, msg);
            /* the rule is for the long codes; a short one runs to its limit */
            if (ldpc.NumberParityBits >= 2048)
                TEST_ASSERT_TRUE_MESSAGE(it_on <= 30, msg);
            else
                TEST_ASSERT_EQUAL_INT_MESSAGE(ldpc.max_iter, it_on, msg);
        }
        free(llr);
        free(out);
    }
}

/* The all-zero word is a codeword of every linear code.  Positive soft bits
 * mean 0; noise strong enough to need several iterations. */
static void test_converging_decode_is_the_same(void)
{
    for (unsigned c = 0; c < sizeof(CODES) / sizeof(CODES[0]); c++) {
        struct LDPC ldpc;
        ldpc_codes_setup(&ldpc, (char *)CODES[c]);
        int n = ldpc.CodeLength;
        float *llr = malloc(sizeof(float) * (size_t)n);
        uint8_t *a = malloc((size_t)n), *b = malloc((size_t)n);
        static const double SIGMA[] = { 0.80, 0.68, 0.62 };
        double sigma = SIGMA[c];
        int multi = 0, converged = 0;
        for (int trial = 0; trial < 8; trial++) {
            for (int i = 0; i < n; i++) llr[i] = (float)(2.0 * (1.0 + sigma * gauss()) / (sigma * sigma));
            int pa = 0, pb = 0;
            ldpc.early_stop = 0;
            int ia = run_ldpc_decoder(&ldpc, a, llr, &pa);
            ldpc.early_stop = 1;
            int ib = run_ldpc_decoder(&ldpc, b, llr, &pb);
            char msg[120];
            snprintf(msg, sizeof(msg), "%s trial %d: %d iterations off, %d on", CODES[c], trial, ia, ib);
            if (ia >= ldpc.max_iter) continue; /* did not decode: not this test's case */
            converged++;
            TEST_ASSERT_EQUAL_INT_MESSAGE(ia, ib, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(pa, pb, msg);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, (size_t)n, msg);
            if (ia > 3) multi++;
        }
        TEST_ASSERT_TRUE_MESSAGE(converged >= 3, "the noise is too heavy: too few decodes converged");
        TEST_ASSERT_TRUE_MESSAGE(multi > 0, "the noise is too light: every decode converged at once");
        free(llr);
        free(a);
        free(b);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default);
    RUN_TEST(test_noise_stops_early_only_when_on);
    RUN_TEST(test_converging_decode_is_the_same);
    return UNITY_END();
}

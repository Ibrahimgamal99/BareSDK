/**
 * @file test_mos.c  Unit tests for MOS calculation functions
 *
 * Tests both E-model and simplified MOS calculations with known inputs.
 * No external dependencies — tests the pure math directly.
 */

#include <stdio.h>
#include <math.h>
#include <string.h>

#define TEST_PASS 0
#define TEST_FAIL 1

static int g_pass = 0;
static int g_fail = 0;

static float mos_emodel(float loss_pct, float jitter_ms, float rtt_ms)
{
	float Ta = rtt_ms * 0.5f + 2.0f * jitter_ms;
	float Id = 0.024f * Ta
	         + 0.11f * (Ta - 177.3f) * (Ta > 177.3f ? 1.f : 0.f);
	float Ie = 7.0f + 30.0f * logf(1.0f + 15.0f * loss_pct / 100.0f);
	float R  = 93.2f - Id - Ie;
	if (R < 0.f)   R = 0.f;
	if (R > 100.f) R = 100.f;
	return 1.0f + 0.035f * R + 7e-6f * R * (R - 60.f) * (100.f - R);
}

static float mos_simplified(float loss_pct, float jitter_ms, float rtt_ms)
{
	float m = 4.5f
	        - 0.9f * (loss_pct / 100.f)
	        - 0.04f * (jitter_ms / 100.f)
	        - 0.002f * rtt_ms;
	if (m < 1.0f) m = 1.0f;
	if (m > 4.5f) m = 4.5f;
	return m;
}

#define ASSERT_RANGE(name, val, lo, hi, tol) do { \
	if ((val) < (lo) - (tol) || (val) > (hi) + (tol)) { \
		fprintf(stderr, "FAIL: %s = %.4f (expected [%.2f, %.2f])\n", \
		        name, (double)(val), (lo), (hi)); \
		g_fail++; \
	} else { \
		g_pass++; \
	} \
} while (0)

#define ASSERT_GT(name, val, bound) do { \
	if (!((val) > (bound))) { \
		fprintf(stderr, "FAIL: %s = %.4f (expected > %.2f)\n", \
		        name, (double)(val), (double)(bound)); \
		g_fail++; \
	} else { \
		g_pass++; \
	} \
} while (0)

static void test_emodel_perfect(void)
{
	float mos = mos_emodel(0.0f, 0.0f, 0.0f);
	ASSERT_GT("emodel(perfect)", mos, 4.0f);
}

static void test_emodel_moderate_loss(void)
{
	float mos = mos_emodel(5.0f, 20.0f, 100.0f);
	ASSERT_RANGE("emodel(5%loss)", mos, 3.0f, 4.5f, 0.5f);
}

static void test_emodel_high_loss(void)
{
	float mos = mos_emodel(30.0f, 100.0f, 300.0f);
	ASSERT_RANGE("emodel(30%loss)", mos, 1.0f, 3.0f, 0.5f);
}

static void test_emodel_extreme_loss(void)
{
	float mos = mos_emodel(100.0f, 500.0f, 1000.0f);
	ASSERT_RANGE("emodel(100%loss)", mos, 1.0f, 2.0f, 0.1f);
}

static void test_emodel_zero_loss_degrades_with_rtt(void)
{
	float mos_low_rtt = mos_emodel(0.0f, 0.0f, 50.0f);
	float mos_high_rtt = mos_emodel(0.0f, 0.0f, 500.0f);
	ASSERT_GT("emodel(0%loss,50ms) > emodel(0%loss,500ms)",
	          mos_low_rtt, mos_high_rtt);
}

static void test_emodel_jitter_degrades_mos(void)
{
	float mos_low  = mos_emodel(0.0f,  5.0f, 20.0f);
	float mos_high = mos_emodel(0.0f, 80.0f, 20.0f);
	ASSERT_GT("emodel(low jitter) > emodel(high jitter)", mos_low, mos_high);
}

static void test_emodel_ta_compound(void)
{
	/* Ta=10+80=90ms (high jitter) vs Ta=50ms (moderate RTT, no jitter) */
	float mos_jitter = mos_emodel(0.0f, 40.0f,  20.0f);
	float mos_rtt    = mos_emodel(0.0f,  0.0f, 100.0f);
	ASSERT_GT("emodel(Ta=50ms) > emodel(Ta=90ms)", mos_rtt, mos_jitter);
}

static void test_simplified_perfect(void)
{
	float mos = mos_simplified(0.0f, 0.0f, 0.0f);
	ASSERT_RANGE("simplified(perfect)", mos, 4.4f, 4.5f, 0.01f);
}

static void test_simplified_moderate_loss(void)
{
	float mos = mos_simplified(5.0f, 20.0f, 100.0f);
	ASSERT_RANGE("simplified(5%loss)", mos, 3.5f, 4.5f, 0.5f);
}

static void test_simplified_high_loss(void)
{
	float mos = mos_simplified(50.0f, 200.0f, 500.0f);
	ASSERT_RANGE("simplified(50%loss)", mos, 1.0f, 3.5f, 0.5f);
}

static void test_simplified_clamped(void)
{
	float mos = mos_simplified(100.0f, 10000.0f, 5000.0f);
	ASSERT_RANGE("simplified(extreme)", mos, 1.0f, 4.5f, 0.01f);
}

static void test_simplified_monotonic_loss(void)
{
	float m1 = mos_simplified(1.0f, 0.0f, 0.0f);
	float m5 = mos_simplified(5.0f, 0.0f, 0.0f);
	float m10 = mos_simplified(10.0f, 0.0f, 0.0f);
	if (m1 > m5 && m5 > m10) {
		g_pass++;
	} else {
		fprintf(stderr, "FAIL: simplified MOS not monotonically decreasing "
		        "with loss: %.3f > %.3f > %.3f\n", m1, m5, m10);
		g_fail++;
	}
}

int main(void)
{
	test_emodel_perfect();
	test_emodel_moderate_loss();
	test_emodel_high_loss();
	test_emodel_extreme_loss();
	test_emodel_zero_loss_degrades_with_rtt();
	test_emodel_jitter_degrades_mos();
	test_emodel_ta_compound();

	test_simplified_perfect();
	test_simplified_moderate_loss();
	test_simplified_high_loss();
	test_simplified_clamped();
	test_simplified_monotonic_loss();

	printf("MOS tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail > 0 ? TEST_FAIL : TEST_PASS;
}

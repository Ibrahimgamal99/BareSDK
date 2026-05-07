/**
 * @file test_mwi_parser.c  Unit tests for MWI body parser
 *
 * Tests RFC 3842 MWI NOTIFY body parsing logic.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>

#define TEST_PASS 0
#define TEST_FAIL 1

static int g_pass = 0;
static int g_fail = 0;

static void parse_mwi_body(const char *body,
                              bool *waiting,
                              uint32_t *new_voice, uint32_t *old_voice,
                              uint32_t *new_urgent, uint32_t *old_urgent)
{
	*waiting   = false;
	*new_voice = *old_voice = *new_urgent = *old_urgent = 0;

	const char *p = body;
	while (p && *p) {
		if (strncasecmp(p, "Messages-Waiting:", 17) == 0) {
			p += 17;
			while (*p == ' ') p++;
			*waiting = (strncasecmp(p, "yes", 3) == 0);
		} else if (strncasecmp(p, "Voice-Message:", 14) == 0) {
			p += 14;
			while (*p == ' ') p++;
			char *end;
			*new_voice  = (uint32_t)strtoul(p, &end, 10);
			if (*end == '/') *old_voice  = (uint32_t)strtoul(end+1, &end, 10);
			if (*end == ' ' && *(end+1) == '(') {
				end += 2;
				*new_urgent = (uint32_t)strtoul(end, &end, 10);
				if (*end == '/') *old_urgent = (uint32_t)strtoul(end+1, NULL, 10);
			}
		}
		p = strchr(p, '\n');
		if (p) p++;
	}
}

#define ASSERT_EQ_INT(name, a, b) do { \
	if ((a) != (b)) { \
		fprintf(stderr, "FAIL: %s = %u (expected %u)\n", name, \
		        (unsigned)(a), (unsigned)(b)); \
		g_fail++; \
	} else { g_pass++; } \
} while (0)

#define ASSERT_EQ_BOOL(name, a, b) do { \
	if ((a) != (b)) { \
		fprintf(stderr, "FAIL: %s = %s (expected %s)\n", name, \
		        (a) ? "true" : "false", (b) ? "true" : "false"); \
		g_fail++; \
	} else { g_pass++; } \
} while (0)

static void test_mwi_waiting_yes(void)
{
	bool waiting;
	uint32_t nv, ov, nu, ou;
	parse_mwi_body("Messages-Waiting: yes\nVoice-Message: 2/4 (0/1)\n",
	               &waiting, &nv, &ov, &nu, &ou);
	ASSERT_EQ_BOOL("waiting", waiting, true);
	ASSERT_EQ_INT("new_voice", nv, 2);
	ASSERT_EQ_INT("old_voice", ov, 4);
	ASSERT_EQ_INT("new_urgent", nu, 0);
	ASSERT_EQ_INT("old_urgent", ou, 1);
}

static void test_mwi_waiting_no(void)
{
	bool waiting;
	uint32_t nv, ov, nu, ou;
	parse_mwi_body("Messages-Waiting: no\nVoice-Message: 0/0 (0/0)\n",
	               &waiting, &nv, &ov, &nu, &ou);
	ASSERT_EQ_BOOL("waiting", waiting, false);
	ASSERT_EQ_INT("new_voice", nv, 0);
}

static void test_mwi_no_urgent(void)
{
	bool waiting;
	uint32_t nv, ov, nu, ou;
	parse_mwi_body("Messages-Waiting: yes\nVoice-Message: 5/3\n",
	               &waiting, &nv, &ov, &nu, &ou);
	ASSERT_EQ_BOOL("waiting", waiting, true);
	ASSERT_EQ_INT("new_voice", nv, 5);
	ASSERT_EQ_INT("old_voice", ov, 3);
	ASSERT_EQ_INT("new_urgent", nu, 0);
	ASSERT_EQ_INT("old_urgent", ou, 0);
}

static void test_mwi_empty(void)
{
	bool waiting;
	uint32_t nv, ov, nu, ou;
	parse_mwi_body("", &waiting, &nv, &ov, &nu, &ou);
	ASSERT_EQ_BOOL("waiting_empty", waiting, false);
	ASSERT_EQ_INT("new_voice_empty", nv, 0);
}

static void test_mwi_large_counts(void)
{
	bool waiting;
	uint32_t nv, ov, nu, ou;
	parse_mwi_body("Messages-Waiting: yes\nVoice-Message: 100/200 (50/75)\n",
	               &waiting, &nv, &ov, &nu, &ou);
	ASSERT_EQ_INT("new_voice_large", nv, 100);
	ASSERT_EQ_INT("old_voice_large", ov, 200);
	ASSERT_EQ_INT("new_urgent_large", nu, 50);
	ASSERT_EQ_INT("old_urgent_large", ou, 75);
}

static void test_mwi_only_waiting(void)
{
	bool waiting;
	uint32_t nv, ov, nu, ou;
	parse_mwi_body("Messages-Waiting: yes\n", &waiting, &nv, &ov, &nu, &ou);
	ASSERT_EQ_BOOL("waiting_only", waiting, true);
	ASSERT_EQ_INT("new_voice_only", nv, 0);
}

int main(void)
{
	test_mwi_waiting_yes();
	test_mwi_waiting_no();
	test_mwi_no_urgent();
	test_mwi_empty();
	test_mwi_large_counts();
	test_mwi_only_waiting();

	printf("MWI parser tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail > 0 ? TEST_FAIL : TEST_PASS;
}

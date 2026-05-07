/**
 * @file test_transport_str.c  Unit tests for transport/media-enc string mapping
 *
 * Tests the string conversion functions for transport and media encryption enums.
 */

#include <stdio.h>
#include <string.h>

#define TEST_PASS 0
#define TEST_FAIL 1

static int g_pass = 0;
static int g_fail = 0;

typedef enum {
	TRANSPORT_UDP = 0,
	TRANSPORT_TCP,
	TRANSPORT_TLS,
	TRANSPORT_WS,
	TRANSPORT_WSS,
} transport_t;

typedef enum {
	MEDIA_ENC_NONE = 0,
	MEDIA_ENC_SDES,
	MEDIA_ENC_DTLS_SRTP,
} media_enc_t;

static const char *transport_str(transport_t t)
{
	switch (t) {
	case TRANSPORT_UDP: return "udp";
	case TRANSPORT_TCP: return "tcp";
	case TRANSPORT_TLS: return "tls";
	case TRANSPORT_WS:  return "ws";
	case TRANSPORT_WSS: return "wss";
	default:            return NULL;
	}
}

static const char *mediaenc_str(media_enc_t enc)
{
	switch (enc) {
	case MEDIA_ENC_SDES:      return "sdes";
	case MEDIA_ENC_DTLS_SRTP: return "dtls_srtp";
	case MEDIA_ENC_NONE:      return NULL;
	default:                  return NULL;
	}
}

#define ASSERT_EQ_STR(name, a, b) do { \
	if (strcmp((a), (b)) != 0) { \
		fprintf(stderr, "FAIL: %s = \"%s\" (expected \"%s\")\n", name, (a), (b)); \
		g_fail++; \
	} else { g_pass++; } \
} while (0)

#define ASSERT_NULL(name, a) do { \
	if ((a) != NULL) { \
		fprintf(stderr, "FAIL: %s = \"%s\" (expected NULL)\n", name, (a)); \
		g_fail++; \
	} else { g_pass++; } \
} while (0)

static void test_transport_all(void)
{
	ASSERT_EQ_STR("udp", transport_str(TRANSPORT_UDP), "udp");
	ASSERT_EQ_STR("tcp", transport_str(TRANSPORT_TCP), "tcp");
	ASSERT_EQ_STR("tls", transport_str(TRANSPORT_TLS), "tls");
	ASSERT_EQ_STR("ws", transport_str(TRANSPORT_WS), "ws");
	ASSERT_EQ_STR("wss", transport_str(TRANSPORT_WSS), "wss");
}

static void test_transport_invalid(void)
{
	const char *s = transport_str((transport_t)99);
	ASSERT_NULL("invalid_transport", s);
}

static void test_mediaenc_sdes(void)
{
	ASSERT_EQ_STR("sdes", mediaenc_str(MEDIA_ENC_SDES), "sdes");
}

static void test_mediaenc_dtls(void)
{
	ASSERT_EQ_STR("dtls_srtp", mediaenc_str(MEDIA_ENC_DTLS_SRTP), "dtls_srtp");
}

static void test_mediaenc_none(void)
{
	const char *s = mediaenc_str(MEDIA_ENC_NONE);
	ASSERT_NULL("none_enc", s);
}

static void test_mediaenc_invalid(void)
{
	const char *s = mediaenc_str((media_enc_t)99);
	ASSERT_NULL("invalid_enc", s);
}

int main(void)
{
	test_transport_all();
	test_transport_invalid();
	test_mediaenc_sdes();
	test_mediaenc_dtls();
	test_mediaenc_none();
	test_mediaenc_invalid();

	printf("Transport string tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail > 0 ? TEST_FAIL : TEST_PASS;
}

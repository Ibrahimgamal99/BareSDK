/**
 * @file error.c  Human-readable error strings for BARESDK_ERR_* codes
 */

#include "baresdk_internal.h"

const char *baresdk_strerror(int err)
{
	switch ((baresdk_error_t)err) {
	case BARESDK_OK:                       return "ok";
	case BARESDK_ERR_INVAL:                return "invalid argument";
	case BARESDK_ERR_NOMEM:                return "out of memory";
	case BARESDK_ERR_STATE:                return "wrong lifecycle state";
	case BARESDK_ERR_DNS:                  return "DNS resolution failed";
	case BARESDK_ERR_TRANSPORT:            return "transport error";
	case BARESDK_ERR_AUTH:                 return "authentication failed";
	case BARESDK_ERR_SERVER_5XX:           return "server error (5xx)";
	case BARESDK_ERR_WS_PROTOCOL_REJECTED: return "WebSocket protocol rejected";
	case BARESDK_ERR_TIMEOUT:              return "timeout";
	case BARESDK_ERR_ALREADY:              return "already initialized";
	default: {
		static char buf[32];
		re_snprintf(buf, sizeof(buf), "unknown error %d", err);
		return buf;
	}
	}
}

/**
 * @file error.c  Human-readable error strings for VOXSDK_ERR_* codes
 */

#include "voxsdk_internal.h"

const char *voxsdk_strerror(int err)
{
	switch ((voxsdk_error_t)err) {
	case VOXSDK_OK:                       return "ok";
	case VOXSDK_ERR_INVAL:                return "invalid argument";
	case VOXSDK_ERR_NOMEM:                return "out of memory";
	case VOXSDK_ERR_STATE:                return "wrong lifecycle state";
	case VOXSDK_ERR_DNS:                  return "DNS resolution failed";
	case VOXSDK_ERR_TRANSPORT:            return "transport error";
	case VOXSDK_ERR_AUTH:                 return "authentication failed";
	case VOXSDK_ERR_SERVER_5XX:           return "server error (5xx)";
	case VOXSDK_ERR_WS_PROTOCOL_REJECTED: return "WebSocket protocol rejected";
	case VOXSDK_ERR_TIMEOUT:              return "timeout";
	case VOXSDK_ERR_ALREADY:              return "already initialized";
	default: {
		static char buf[32];
		re_snprintf(buf, sizeof(buf), "unknown error %d", err);
		return buf;
	}
	}
}

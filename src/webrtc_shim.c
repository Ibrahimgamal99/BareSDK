/* Lazy-loads libwebrtc-audio-processing-1 via dlopen so voxsdk.so has no
 * hard DT_NEEDED entry for it.  If the library is absent at runtime, AEC
 * init returns ENODEV (AudioProcessingBuilder::Create returns NULL, which
 * aec.cpp already handles).  Only active when the webrtc_aec module is built.
 */
#ifdef VOXSDK_HAS_WEBRTC_AEC

#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>

static void          *s_lib;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void load_lib(void)
{
	static const char *const names[] = {
		"libwebrtc-audio-processing-1.so.3",
		"libwebrtc-audio-processing-1.so.1",
		"libwebrtc-audio-processing-1.so.0",
		"libwebrtc-audio-processing-1.so",
		NULL
	};
	for (int i = 0; names[i]; i++) {
		s_lib = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
		if (s_lib)
			return;
	}
}

static void *get_sym(const char *sym)
{
	pthread_once(&s_once, load_lib);
	return s_lib ? dlsym(s_lib, sym) : NULL;
}

/* ---- webrtc::AudioProcessingBuilder --------------------------------------- */

void webrtc_apb_ctor(void *self)
	__asm__("_ZN6webrtc22AudioProcessingBuilderC1Ev");
void webrtc_apb_ctor(void *self)
{
	typedef void (*fn_t)(void *);
	fn_t f = (fn_t)get_sym("_ZN6webrtc22AudioProcessingBuilderC1Ev");
	if (f) f(self);
}

void webrtc_apb_dtor(void *self)
	__asm__("_ZN6webrtc22AudioProcessingBuilderD1Ev");
void webrtc_apb_dtor(void *self)
{
	typedef void (*fn_t)(void *);
	fn_t f = (fn_t)get_sym("_ZN6webrtc22AudioProcessingBuilderD1Ev");
	if (f) f(self);
}

void *webrtc_apb_create(void *self)
	__asm__("_ZN6webrtc22AudioProcessingBuilder6CreateEv");
void *webrtc_apb_create(void *self)
{
	typedef void *(*fn_t)(void *);
	fn_t f = (fn_t)get_sym("_ZN6webrtc22AudioProcessingBuilder6CreateEv");
	return f ? f(self) : NULL;
}

/* ---- webrtc::Config ------------------------------------------------------- */

void webrtc_cfg_ctor(void *self)
	__asm__("_ZN6webrtc6ConfigC1Ev");
void webrtc_cfg_ctor(void *self)
{
	typedef void (*fn_t)(void *);
	fn_t f = (fn_t)get_sym("_ZN6webrtc6ConfigC1Ev");
	if (f) f(self);
}

void webrtc_cfg_dtor(void *self)
	__asm__("_ZN6webrtc6ConfigD1Ev");
void webrtc_cfg_dtor(void *self)
{
	typedef void (*fn_t)(void *);
	fn_t f = (fn_t)get_sym("_ZN6webrtc6ConfigD1Ev");
	if (f) f(self);
}

#endif /* VOXSDK_HAS_WEBRTC_AEC */

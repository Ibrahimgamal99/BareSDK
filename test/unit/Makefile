CC      ?= gcc
CFLAGS  := -Wall -Wextra -std=c11 -O2 -g
LDFLAGS := -lm

TESTS := test_mos test_url_parser test_mwi_parser test_codec_str test_transport_str

# test_ws_pin links the real src/ws_path.c, so it needs the re/baresip headers
# and libs a host build installs. Built only when that sysroot is present
# (SYSROOT=... to point elsewhere); the other tests stay standalone.
SYSROOT ?= ../../build/linux-x86_64/sysroot
ifneq ($(wildcard $(SYSROOT)/include/re/re.h),)
TESTS += test_ws_pin test_audio_external test_fmtp_bitrate test_ice_deadline
endif

.PHONY: all clean test

all: $(TESTS)

test_mos: test_mos.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_url_parser: test_url_parser.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_mwi_parser: test_mwi_parser.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_codec_str: test_codec_str.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_transport_str: test_transport_str.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# --wrap is what renames the wrapper on Linux; keep it so the test exercises
# the same symbol the shipped library links.
test_ws_pin: test_ws_pin.c ../../src/ws_path.c
	$(CC) $(CFLAGS) -Wno-unused-parameter \
	  -I$(SYSROOT)/include -I$(SYSROOT)/include/re -I../../include \
	  -o $@ $^ -L$(SYSROOT)/lib -Wl,--wrap=sip_transp_send -Wl,--wrap=sip_dialog_route \
	  -lbaresip -lre -lssl -lcrypto -lpthread -lz \
	  $(LDFLAGS)

# Deliberately does NOT link libbaresip: baresip_ausrcl() sits in the same
# object as the player and recorder, so pulling it in drags every audio backend
# the sysroot happens to have been built with. The test provides the device
# registry itself and links libre alone.
test_audio_external: test_audio_external.c ../../src/audio_external.c
	$(CC) $(CFLAGS) -Wno-unused-parameter \
	  -I$(SYSROOT)/include -I$(SYSROOT)/include/re -I../../include \
	  -o $@ $^ -L$(SYSROOT)/lib -lre -lssl -lcrypto -lpthread -lz \
	  $(LDFLAGS)

# Links libre but not libbaresip: baresip_mnatl() needs a full baresip_init(),
# and the test only wants a media-NAT registry entry to interpose on, so it
# supplies its own along with the three other baresip symbols ice_shim.c uses.
test_ice_deadline: test_ice_deadline.c ../../src/ice_shim.c
	$(CC) $(CFLAGS) -Wno-unused-parameter \
	  -I$(SYSROOT)/include -I$(SYSROOT)/include/re -I../../include \
	  -o $@ $^ -L$(SYSROOT)/lib -lre -lssl -lcrypto -lpthread -lz \
	  $(LDFLAGS)

# Links libre but not libbaresip, for the reason given on test_audio_external:
# the test stubs the handful of baresip symbols src/adapt.c references.
test_fmtp_bitrate: test_fmtp_bitrate.c ../../src/adapt.c
	$(CC) $(CFLAGS) -Wno-unused-parameter \
	  -I$(SYSROOT)/include -I$(SYSROOT)/include/re -I../../include \
	  -o $@ $^ -L$(SYSROOT)/lib -lre -lssl -lcrypto -lpthread -lz \
	  $(LDFLAGS)

test: all
	@failed=0; \
	for t in $(TESTS); do \
		echo "Running $$t..."; \
		./$$t || failed=$$((failed + 1)); \
	done; \
	if [ $$failed -eq 0 ]; then \
		echo "All unit tests passed."; \
	else \
		echo "$$failed test(s) failed."; \
		exit 1; \
	fi

clean:
	rm -f $(TESTS)

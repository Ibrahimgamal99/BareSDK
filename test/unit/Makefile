CC      ?= gcc
CFLAGS  := -Wall -Wextra -std=c11 -O2 -g
LDFLAGS := -lm

TESTS := test_mos test_url_parser test_mwi_parser test_codec_str test_transport_str

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

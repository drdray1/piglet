# Minimum-viable host-side test harness for Piglet.
# See docs/TEST_PLAN.md for the full plan.
# Builds with clang++ -std=c++17 — no Arduino toolchain required.

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wno-unused-parameter -O0 -g
# stubs/ first so <Arduino.h> and <esp_wifi_types.h> resolve to our host-side
# shims rather than failing or pulling the real headers in.
INCLUDES := -I stubs -I ../Arduino\ Files/Piglet -I .

SOURCES := test_main.cpp test_scanner.cpp test_webui_masking.cpp test_wigle_upload.cpp test_channel_classification.cpp test_ble_csv.cpp test_ble_dedupe.cpp test_wifi_dedupe.cpp test_jcmk_ble.cpp test_node_role.cpp
BINARY  := tests.out

.PHONY: test build clean

test: build
	./$(BINARY)

build: $(BINARY)

$(BINARY): $(SOURCES) doctest.h stubs/Arduino.h stubs/esp_wifi_types.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SOURCES)

clean:
	rm -f $(BINARY)

# Makefile for AdvDeck Agent host tests
#
# Usage:
#   make           # build ./run_tests
#   make test      # build and run
#   make schemas   # regenerate include/advdeck/schema_embed.h
#   make clean     # remove artifacts
#
# Requires: g++ with C++17 support, GNU make. The vendored
# third_party/nlohmann/nlohmann/json.hpp is the only third-party
# dependency. The schema_embed header is regenerated automatically
# when a vendored schema (or the script itself) changes; it is
# also committed so the firmware build does not need Python.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O0 -g

ROOT     := $(abspath $(CURDIR)/../..)
INCLUDES := -I $(ROOT)/include -I $(ROOT)/src -I $(ROOT)/third_party/nlohmann

# Pure C++17 service sources. The host-only HostStorage is here;
# SdStorage is excluded (it requires Arduino headers).
SERVICE_SRCS := \
	$(ROOT)/src/services/slug.cpp \
	$(ROOT)/src/services/project_store.cpp \
	$(ROOT)/src/services/task_store.cpp \
	$(ROOT)/src/services/calendar_store.cpp \
	$(ROOT)/src/services/outbox_queue.cpp \
	$(ROOT)/src/services/bridge_import.cpp \
	$(ROOT)/src/services/bridge_log.cpp \
	$(ROOT)/src/platform/host_storage.cpp

# UI sources that have no Arduino / M5 dependency. text_editor.cpp
# is fully host-portable: it has the firmware build wrapped in
# #ifdef ADVDECK_FIRMWARE, so the host build links the buffer-only
# paths cleanly.
UI_SRCS := \
	$(ROOT)/src/ui/text_editor.cpp

TEST_SRCS := \
	test_main.cpp \
	test_slug.cpp \
	test_storage_paths.cpp \
	test_project_store.cpp \
	test_task_store.cpp \
	test_calendar_store.cpp \
	test_outbox_queue.cpp \
	test_bridge_import.cpp \
	test_bridge_log.cpp

# Optional: test_text_editor.cpp is added by A03 when the editor lands.
EXTRA_TEST_SRCS := $(wildcard test_text_editor.cpp)
ifeq ($(strip $(EXTRA_TEST_SRCS)),)
TEST_SRCS_ALL := $(TEST_SRCS)
else
TEST_SRCS_ALL := $(TEST_SRCS) $(EXTRA_TEST_SRCS)
endif

BIN := run_tests
OBJS := $(TEST_SRCS_ALL:.cpp=.o) $(SERVICE_SRCS:.cpp=.o) $(UI_SRCS:.cpp=.o)

# Generated schema header. Re-run the embed script whenever one of
# the two vendored schemas (or the script itself) changes. The
# header is committed so the firmware build does not need Python;
# `make schemas` is a developer affordance for when a schema
# file is updated.
SCHEMA_EMBED := $(ROOT)/include/advdeck/schema_embed.h
SCHEMA_DEPS := \
	$(ROOT)/schemas/pending-request.schema.json \
	$(ROOT)/schemas/result-manifest.schema.json \
	$(ROOT)/scripts/build_schema_embed.py

.PHONY: all test clean schemas

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(SCHEMA_EMBED): $(SCHEMA_DEPS)
	python3 $(ROOT)/scripts/build_schema_embed.py \
		--schema-dir $(ROOT)/schemas \
		--out $(SCHEMA_EMBED)

schemas: $(SCHEMA_EMBED)

test: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(OBJS)

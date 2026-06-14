# Makefile for AdvDeck Agent host tests
#
# Usage:
#   make           # build ./run_tests
#   make test      # build and run
#   make clean     # remove artifacts
#
# Requires: g++ with C++17 support, GNU make. The vendored
# third_party/nlohmann/nlohmann/json.hpp is the only third-party dep.

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
	$(ROOT)/src/platform/host_storage.cpp

TEST_SRCS := \
	test_main.cpp \
	test_slug.cpp \
	test_storage_paths.cpp \
	test_project_store.cpp \
	test_task_store.cpp \
	test_calendar_store.cpp

# Optional: test_text_editor.cpp is added by A03 when the editor lands.
EXTRA_TEST_SRCS := $(wildcard test_text_editor.cpp)
ifeq ($(strip $(EXTRA_TEST_SRCS)),)
TEST_SRCS_ALL := $(TEST_SRCS)
else
TEST_SRCS_ALL := $(TEST_SRCS) $(EXTRA_TEST_SRCS)
endif

BIN := run_tests
OBJS := $(TEST_SRCS_ALL:.cpp=.o) $(SERVICE_SRCS:.cpp=.o)

.PHONY: all test clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

test: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(OBJS)

GIT_SHA=$(shell git rev-parse --short HEAD)
DISABLED_WARNINGS=-Wno-switch -Wno-pointer-sign -Wno-tautological-constant-out-of-range-compare -Wno-tautological-compare -Wno-macro-redefined
LDFLAGS=-pthread -ldl -lm -lstdc++
CFLAGS=-std=c++14 -DGIT_SHA=\"$(GIT_SHA)\"
CFLAGS:=$(CFLAGS) -DODIN_VERSION_RAW=\"dev-$(shell date +"%Y-%m")\"
CC=clang

OS=$(shell uname)

ifeq ($(OS), Darwin)
    LLVM_CONFIG=llvm-config
    ifneq ($(shell llvm-config --version | grep '^12\.'),)
        LLVM_CONFIG=llvm-config
    else
        $(error "Requirement: llvm-config must be version 12.0.1")
    endif

    LDFLAGS:=$(LDFLAGS) -liconv
    CFLAGS:=$(CFLAGS) $(shell $(LLVM_CONFIG) --cxxflags --ldflags)
    LDFLAGS:=$(LDFLAGS) -lLLVM-C
endif
ifeq ($(OS), Linux)
    LLVM_CONFIG=llvm-config-12
    ifneq ($(shell which llvm-config-12 2>/dev/null),)
        LLVM_CONFIG=llvm-config-12
    else
        ifneq ($(shell llvm-config --version | grep '^12\.'),)
            LLVM_CONFIG=llvm-config
        else
            $(error "Requirement: llvm-config must be version 12.0.1")
        endif
    endif

    CFLAGS:=$(CFLAGS) $(shell $(LLVM_CONFIG) --cxxflags --ldflags)
    LDFLAGS:=$(LDFLAGS) $(shell $(LLVM_CONFIG) --libs core native --system-libs)
endif

all: debug demo

demo:
	./odin run examples/demo/demo.odin

debug:
	$(CC) src/main.cpp $(DISABLED_WARNINGS) $(CFLAGS) -g $(LDFLAGS) -o odin

release:
	$(CC) src/main.cpp $(DISABLED_WARNINGS) $(CFLAGS) -O3 -march=native $(LDFLAGS) -o odin

nightly:
	$(CC) src/main.cpp $(DISABLED_WARNINGS) $(CFLAGS) -DNIGHTLY -O3 $(LDFLAGS) -o odin




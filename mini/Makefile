# Departure HUD (mini) — direct Wayland, no Qt.
#
#   make            build ./departure-hud-mini
#   make run        build + launch
#   make clean

CC        ?= cc
PKG_DEPS  := wayland-client freetype2 pixman-1 alsa
CFLAGS    ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
CFLAGS    += $(shell pkg-config --cflags $(PKG_DEPS))
LDLIBS    += $(shell pkg-config --libs   $(PKG_DEPS)) -lrt -lm -ldl

WS        ?= wayland-scanner

PROTO_DIR := protocols
GEN_DIR   := src/gen

PROTOS    := xdg-shell wlr-layer-shell-unstable-v1
GEN_H     := $(addprefix $(GEN_DIR)/,$(addsuffix .h,$(PROTOS)))
GEN_C     := $(addprefix $(GEN_DIR)/,$(addsuffix .c,$(PROTOS)))

SRC       := $(wildcard src/*.c) $(GEN_C)
OBJ       := $(SRC:.c=.o)
TARGET    := departure-hud-mini

.PHONY: all run clean
all: $(TARGET)

$(TARGET): $(GEN_H) $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDLIBS)

# Every object depends on the generated client headers existing first.
$(OBJ): $(GEN_H)

$(GEN_DIR):
	@mkdir -p $@

$(GEN_DIR)/%.h: $(PROTO_DIR)/%.xml | $(GEN_DIR)
	$(WS) client-header $< $@

$(GEN_DIR)/%.c: $(PROTO_DIR)/%.xml | $(GEN_DIR)
	$(WS) private-code $< $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)
	rm -rf $(GEN_DIR)

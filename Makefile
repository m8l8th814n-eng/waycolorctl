PREFIX ?= /usr/local
CC ?= cc
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

BUILD_DIR := build
SRC_DIR := src
PROTO_DIR := protocols
BIN := waycolorctl

CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic
CFLAGS += $(shell $(PKG_CONFIG) --cflags wayland-client)
LDFLAGS += $(shell $(PKG_CONFIG) --libs wayland-client)

GEN_C := $(BUILD_DIR)/wlr-gamma-control-protocol.c
GEN_H := $(BUILD_DIR)/wlr-gamma-control-client-protocol.h
OBJS := $(BUILD_DIR)/main.o $(BUILD_DIR)/wlr-gamma-control-protocol.o

.PHONY: all clean install

all: $(BUILD_DIR)/$(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(GEN_H): $(PROTO_DIR)/wlr-gamma-control-unstable-v1.xml | $(BUILD_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(GEN_C): $(PROTO_DIR)/wlr-gamma-control-unstable-v1.xml | $(BUILD_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c $(GEN_H) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -c $< -o $@

$(BUILD_DIR)/wlr-gamma-control-protocol.o: $(GEN_C) $(GEN_H) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -c $(GEN_C) -o $@

$(BUILD_DIR)/$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

install: $(BUILD_DIR)/$(BIN)
	install -Dm755 $(BUILD_DIR)/$(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -rf $(BUILD_DIR)

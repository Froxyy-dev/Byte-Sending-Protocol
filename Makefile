CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LFLAGS =

.PHONY: all clean

BIN_DIR = bin
BUILD_DIR = build
SRC_DIR = src
INCLUDE_DIR = include

TARGET1 = $(BIN_DIR)/ppcbc
TARGET2 = $(BIN_DIR)/ppcbs

# Source files
SRC1 = $(SRC_DIR)/ppcbc.c
SRC2 = $(SRC_DIR)/ppcbs.c
COMMON_SRC = $(SRC_DIR)/ppcb-common.c $(SRC_DIR)/ppcb-udp.c $(SRC_DIR)/ppcb-udpr.c $(SRC_DIR)/ppcb-tcp.c $(SRC_DIR)/err.c

# Object files
OBJ1 = $(BUILD_DIR)/ppcbc.o
OBJ2 = $(BUILD_DIR)/ppcbs.o
COMMON_OBJ = $(BUILD_DIR)/ppcb-common.o $(BUILD_DIR)/ppcb-udp.o $(BUILD_DIR)/ppcb-udpr.o $(BUILD_DIR)/ppcb-tcp.o $(BUILD_DIR)/err.o

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(OBJ1) $(COMMON_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(TARGET2): $(OBJ2) $(COMMON_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I $(INCLUDE_DIR) -c $< -o $@

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)

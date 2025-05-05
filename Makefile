# Directories
SRC_DIR = src
INCLUDE_DIR = $(SRC_DIR)/include
LIB_DIR = lib
LIB_NAME = liblogging.so

# Compiler and flags
CC = gcc
CFLAGS = -I$(INCLUDE_DIR) -Wall -Wno-parentheses -Werror -O3 -shared -fPIC -march=native -mtune=native

all: $(wildcard $(SRC_DIR)/*.c) | $(LIB_DIR)
	$(CC) $(CFLAGS) -o $(LIB_DIR)/$(LIB_NAME) $^

# Create binary directory if it doesn't exist
$(LIB_DIR):
	mkdir -p $(LIB_DIR)

clean:
	rm -rf $(LIB_DIR)

# Phony targets
.PHONY: all clean

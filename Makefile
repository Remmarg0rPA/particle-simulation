CC := gcc
CFLAGS := -Wall -Wextra -Werror -O3 -march=native -fopenmp
LDFLAGS := -flto -fopenmp -lpthread

SRC := src
BUILD := build
BIN := main

C_SRC_FILES := $(wildcard $(SRC)/*.c)
C_OBJ_FILES := $(patsubst $(SRC)/%.c, $(BUILD)/%.o, $(C_SRC_FILES))

$(BIN): $(C_OBJ_FILES)
	$(CC) $^ -o $@ $(LDFLAGS)

$(C_OBJ_FILES): $(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CC) -c $^ -o $@ $(CFLAGS)

.PHONY: bench clean
bench: $(BIN)
	hyperfine --min-runs 100 --warmup 25 --shell=none './$(BIN) data/positions.xyz' './$(BIN) data/positions_large.xyz'

clean:
	rm -rf $(BUILD)

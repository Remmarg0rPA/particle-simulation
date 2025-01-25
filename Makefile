CC := gcc
SRC := src
BUILD := build
BIN := main

CPPFLAGS := -MMD -MP
CFLAGS := -Wall -Wextra -Werror -O3 -march=native -std=c11
LDFLAGS += -flto -lpthread

C_SRC_FILES := $(wildcard $(SRC)/*.c)
C_OBJ_FILES := $(patsubst $(SRC)/%.c, $(BUILD)/%.o, $(C_SRC_FILES))
DEP_FILES   := $(patsubst $(BUILD)/%.o, $(BUILD)/%.d, $(C_OBJ_FILES))

$(BIN): $(C_OBJ_FILES)
	$(CC) $^ -o $@ $(LDFLAGS)

$(C_OBJ_FILES): $(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: bench
bench: $(BIN)
	hyperfine --min-runs 100 --warmup 25 --shell=none './$(BIN) -f data/positions.xyz -c 4 -p 4' './$(BIN) -f data/positions_large.xyz -c 4 -p 4'

.PHONY: clean
clean:
	rm -rf $(BUILD)

-include $(DEP_FILES)

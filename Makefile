CC:=gcc
SRC:=./src
BUILD:=./build
BIN:=main

CPPFLAGS:=-MMD -MP
CFLAGS:=-Wall -Wextra -Werror -O3 -march=native -std=c11 -pthread
LDFLAGS:=-flto -lpthread -pthread

DEBUG ?= 0
PGO ?= 0
ifeq ($(DEBUG), 1)
# Add debug flags to the build
DEBUGFLAGS := -g -fsanitize=address -fsanitize=undefined
CFLAGS += $(DEBUGFLAGS)
LDFLAGS += $(DEBUGFLAGS)
BUILD := $(BUILD)/debug

else ifeq ($(PGO), 1)
# Add flags for generating code for profile guided optimization
CFLAGS  += -fprofile-generate=$(BUILD)/pgo/data
LDFLAGS += -fprofile-generate=$(BUILD)/pgo/data
BUILD := $(BUILD)/pgo

else ifeq ($(PGO), 2)
# Add flags to use data collected for profile guided optimization
CFLAGS  += -fprofile-use=$(BUILD)/pgo/data
LDFLAGS += -fprofile-use=$(BUILD)/pgo/data
BUILD := $(BUILD)/pgo
# Remove old object files to make gcc and make happy and rebuild the binary
$(shell rm $(BUILD)/*.o)
endif

C_SRC_FILES := $(wildcard $(SRC)/*.c)
C_OBJ_FILES := $(patsubst $(SRC)/%.c, $(BUILD)/%.o, $(C_SRC_FILES))
DEP_FILES   := $(patsubst $(BUILD)/%.o, $(BUILD)/%.d, $(C_OBJ_FILES))

$(BIN): $(C_OBJ_FILES)
	$(CC) $^ -o $@ $(LDFLAGS)

$(C_OBJ_FILES): $(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: debug
debug:
	make DEBUG=1

.PHONY: pgo
pgo:
	make PGO=1
	./$(BIN) -f data/positions_large.xyz -c 12 -p 12
	make PGO=2

.PHONY: bench
bench: $(BIN)
	hyperfine --min-runs 100 --warmup 25 --shell=none './$(BIN) -f data/positions.xyz -c 4 -p 4' './$(BIN) -f data/positions_large.xyz -c 4 -p 4'

.PHONY: clean
clean:
	rm -rf $(BUILD)

-include $(DEP_FILES)

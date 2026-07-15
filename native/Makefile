CC          = gcc
WINDRES     = windres
SRC_DIR     = src
VENDOR_DIR  = vendor
TEST_DIR    = tests
BUILD_DIR   = build
TEST_BUILD_DIR = build/test
OUT         = forzapainter6.exe
TEST_OUT    = forzapainter6_test.exe
RC          = app.rc
RES         = $(BUILD_DIR)/app.res.o

MARCH ?= x86-64-v2

CFLAGS_COMMON  = -std=c11 -Wall -Wextra -I$(SRC_DIR) -I$(VENDOR_DIR) -DUNICODE -D_UNICODE
CFLAGS_RELEASE = $(CFLAGS_COMMON) -O3 -fno-math-errno -fno-trapping-math -march=$(MARCH) -flto -DNDEBUG
CFLAGS_DEBUG   = $(CFLAGS_COMMON) -g -O0

LDFLAGS     = -lgdi32 -luser32 -lcomdlg32 -lmsimg32

SRCS  = $(wildcard $(SRC_DIR)/*.c)
OBJS  = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

TEST_SRCS = $(filter-out $(SRC_DIR)/main.c,$(SRCS)) $(TEST_DIR)/test_main.c
TEST_OBJS = $(patsubst %.c,$(TEST_BUILD_DIR)/%.o,$(notdir $(TEST_SRCS)))
vpath %.c $(SRC_DIR) $(TEST_DIR)

.PHONY: release debug clean run test
release: CFLAGS = $(CFLAGS_RELEASE)
release: LDFLAGS += -mwindows
release: $(OUT)

debug: CFLAGS = $(CFLAGS_DEBUG)
debug: $(OUT)

test: CFLAGS = $(CFLAGS_RELEASE)
test: $(TEST_OUT)
	./$(TEST_OUT)

$(OUT): $(OBJS) $(RES)
	$(CC) $(CFLAGS) $(OBJS) $(RES) -o $@ $(LDFLAGS)

$(TEST_OUT): $(TEST_OBJS)
	$(CC) $(CFLAGS) $(TEST_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)/%.o: %.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(RES): $(RC) icon.ico app.manifest | $(BUILD_DIR)
	$(WINDRES) $(RC) -O coff -o $@

$(BUILD_DIR):
	mkdir -p $@

$(TEST_BUILD_DIR):
	mkdir -p $@

run: $(OUT)
	./$(OUT)

clean:
	rm -rf $(BUILD_DIR) $(OUT) $(TEST_OUT)

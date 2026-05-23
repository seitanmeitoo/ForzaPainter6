CC          = gcc
WINDRES     = windres
SRC_DIR     = src
VENDOR_DIR  = vendor
BUILD_DIR   = build
OUT         = vinyl-painter.exe
RC          = app.rc
RES         = $(BUILD_DIR)/app.res.o

CFLAGS_COMMON  = -std=c11 -Wall -Wextra -I$(SRC_DIR) -I$(VENDOR_DIR) -DUNICODE -D_UNICODE
CFLAGS_RELEASE = $(CFLAGS_COMMON) -O3 -ffast-math -march=native -flto -DNDEBUG
CFLAGS_DEBUG   = $(CFLAGS_COMMON) -g -O0

LDFLAGS     = -lgdi32 -luser32 -lcomdlg32 -lmsimg32

SRCS  = $(wildcard $(SRC_DIR)/*.c)
OBJS  = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: release debug clean run
release: CFLAGS = $(CFLAGS_RELEASE)
release: LDFLAGS += -mwindows
release: $(OUT)

debug: CFLAGS = $(CFLAGS_DEBUG)
debug: $(OUT)

$(OUT): $(OBJS) $(RES)
	$(CC) $(CFLAGS) $(OBJS) $(RES) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(RES): $(RC) icon.ico app.manifest | $(BUILD_DIR)
	$(WINDRES) $(RC) -O coff -o $@

$(BUILD_DIR):
	mkdir -p $@

run: $(OUT)
	./$(OUT)

clean:
	rm -rf $(BUILD_DIR) $(OUT)

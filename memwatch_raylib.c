//gcc -O3 memwatch_raylib.c -lraylib -o memwatch_raylib
#define _GNU_SOURCE
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/uio.h>

// Some colors from old Tango palette, underscored to avoid clashing with Raylib builtins
Color _FG    = { 0xd3, 0xd7, 0xcf, 0xff };
Color _BG    = { 0x00, 0x00, 0x00, 0xff };
Color _GRAY  = { 0x55, 0x57, 0x53, 0xff };
Color _RED   = { 0xa0, 0x00, 0x00, 0xff };
Color _BRED  = { 0xef, 0x29, 0x29, 0xff };
Color _BLUE  = { 0x34, 0x65, 0xa4, 0xff };
Color _BBLUE = { 0x72, 0x9f, 0xcf, 0xff };
Color _GOLD  = { 0xc4, 0xa0, 0x00, 0xff };

#define FADE_TIME 0x30
#define SPACING 4
#define MYCHARS "0123456789+- _/.,:@#abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ┌│"

struct Counter {
  unsigned int untouched : 1;
  unsigned int direction : 1;  // set when increased, unset when decreased
  unsigned int counter : 6;
};


volatile struct Counter INIT_COUNT = {
  .untouched = 1,
  .direction = 0,
  .counter = FADE_TIME,
};


volatile struct {
  // Printout globals
  size_t columns;
  size_t rows;

  // Window drawing related
  size_t width;
  size_t height;
  char layout_changed;
  char running;
  Font font;

  // Font metrics
  size_t chr_w;
  size_t chr_h;

  // Memory window
  pid_t pid;
  size_t size;
  uintptr_t addr;
  uintptr_t d_addr;
  uint8_t *buffer;
  uint8_t *prev;
  struct Counter *counters;

} G = {
    .running = 1,
    .columns = 16,
    .size = 0x100,
    .buffer   = NULL,
    .prev     = NULL,
    .counters = NULL,
};


ssize_t ReadMemory(uint8_t* buffer) {

  struct iovec local  = {
    .iov_base = buffer,
    .iov_len  = G.size};

  struct iovec remote = {
    .iov_base = (void *)G.addr,
    .iov_len  = G.size};

  return process_vm_readv(G.pid, &local, 1, &remote, 1, 0);
}


void UpdateBuffers() {
    memcpy(G.prev, G.buffer, G.size);
    ReadMemory(G.buffer);
}


void ResetStates() {
  for (size_t i = 0; i < G.size; i++) G.counters[i] = INIT_COUNT;
}


void AllocateBuffers() {

  if (G.buffer   != NULL) free(G.buffer);
  if (G.prev     != NULL) free(G.prev);
  if (G.counters != NULL) free(G.counters);

  G.buffer = calloc(G.size, 1);
  G.prev = calloc(G.size, 1);

  G.counters = calloc(G.size, sizeof(struct Counter));
  ResetStates();
}


void PrintUsage(const char *progname) {
    printf("Usage: %s [-f <font.ttf>] [-l <font_size>] -p <PID> -a <addr> [-d <disp_addr>]\n", progname);
    puts("  -f <font_file>   Path to font file (defaults to ./font.ttf)");
    puts("  -l <font_size>   Font size in pixels (default: 8)");
    puts("  -p <PID>         PID to read from");
    puts("  -s <size>        Initial buffer size (default: 0x100)");
    puts("  -a <addr>        Memory location (hex or dec)");
    puts("  -d <d_addr>      Displayed address (hex or dec)");
}


void SetupWindow() {
  G.rows = (G.size + G.columns-1) / G.columns;  // Ceil
  // 8 chars for address + separator + columns*2 for each byte...
  // + 2 character space + columns*2px for spacing, but no spacing for last char
  G.width  = G.chr_w * (G.columns*2 + 8 + 1) + G.columns*SPACING;
  G.height = G.chr_h * (G.rows + 1); // +1 for header

  SetWindowSize(G.width, G.height);
  SetTargetFPS(120);  // Should be safe enough when capturing 50/60Hz based engines
}

void DrawHeader() {

  Vector2 pos = {.x = 0, .y = 0};

  // Buffer size
  char header[16];
  if (G.size < 0x1000)
    snprintf(header, sizeof(header), "W_SZ:%3lX┌", G.size);
  else if (G.size < 0x10000)
    snprintf(header, sizeof(header), "WSZ:%4lX┌",  G.size);
  else
    snprintf(header, sizeof(header), "%08lX┌",      G.size);

  DrawTextEx(G.font, header, pos, G.font.baseSize, 0, _FG);

  // Hex offsets
  char hex[4];
  pos.x = G.chr_w * 9 + 2;
  for (uint col = 0; col < G.columns; col++) {
    sprintf(hex, "%02X", col);
    DrawTextEx(G.font, hex, pos, G.font.baseSize, 0, _GOLD);
    pos.x += G.chr_w * 2 + SPACING;
  }
}


void DrawAddr() {
  // Draws addresses starting from second line
  Vector2 pos = {.x = 0, .y = G.chr_h};

  char addr[16];
  for (size_t row = 0; row < G.rows; row++) {
    size_t offset = G.d_addr + (row * G.columns);
    snprintf(addr, sizeof(addr), "%08lX", offset);
    DrawTextEx(G.font, addr, pos, G.font.baseSize, 0, _GOLD);

    pos.x += G.chr_w * 8;
    DrawTextEx(G.font, "│", pos, G.font.baseSize, 0, _FG);

    pos.x = 0;
    pos.y += G.chr_h;
  }
}


void DrawHex() {
  // Start at column 9, second line
  Vector2 pos;

  char byte[4];
  Color color;
  for (size_t i = 0; i < G.size; i++) {
    if (i % G.columns == 0) {
      pos.x = G.chr_w * 9 + 2;
      pos.y += G.chr_h;
    }
    sprintf(byte, "%02X", G.buffer[i]);

    if (G.counters[i].untouched)  // Not changed once, print with FG or GRAY
      color = G.buffer[i] ?  _FG : _GRAY;
    else {
      if (G.counters[i].counter)  // Counter is nonzero, print bright
        color = G.counters[i].direction ? _BRED : _BBLUE;
      else                        // Counter is zero, print usual
        color = G.counters[i].direction ? _RED : _BLUE;
    }

    DrawTextEx(G.font, byte, pos, G.font.baseSize, 0, color);
    pos.x += G.chr_w * 2 + SPACING;
  }
}


void RefreshCounters() {
  for (size_t i=0; i < G.size; i++) {

    if (!G.counters[i].untouched && G.counters[i].counter)
      G.counters[i].counter--;

    if (G.buffer[i] != G.prev[i]) {
      G.counters[i].untouched = 0;
      G.counters[i].counter = FADE_TIME;
      G.counters[i].direction = G.buffer[i] > G.prev[i] ? 1 : 0;
    }
  }
}

void HandleInput() {
  static char repeat_counter = 0;
  static char delay_counter = 0;

  // Slow down key repeat rate for fuck's sake
  repeat_counter++;
  if (repeat_counter % 2) return;

  bool any_key_down = false;
  int key;
  for (key = 32; key <= 348; key++) {
    if (IsKeyDown(key)) {
      any_key_down = true;
      break;
    }
  }

  if (any_key_down) {
    delay_counter++;
  } else {
    delay_counter = 0;
  }

  if (delay_counter != 1 && delay_counter < 12) return;

  switch (key) {

  case KEY_Q: // Exit
    G.running = 0;
    break;

  case KEY_SPACE: // Clear diff mask
    ResetStates();
    UpdateBuffers();
    break;

  case KEY_R: // Set current offset as 0
    G.d_addr = 0;
    break;

  case KEY_SEMICOLON: // Shift current displayed addess below
    G.d_addr--;
    break;

  case KEY_APOSTROPHE: // Shift current displayed addess ahead
    G.d_addr++;
    break;

  case KEY_LEFT_BRACKET: // Remove column
    if (G.columns > 2) G.columns--;
    G.layout_changed = 1;
    break;

  case KEY_RIGHT_BRACKET: // Add column
    G.columns++;
    G.layout_changed = 1;
    break;

  case KEY_COMMA: // Shrink buffer
    if (G.size > 2) G.size--;
    AllocateBuffers();
    ReadMemory(G.buffer);
    UpdateBuffers();
    G.layout_changed = 1;
    break;

  case KEY_PERIOD: // Grow buffer
    G.size++;
    AllocateBuffers();
    ReadMemory(G.buffer);
    UpdateBuffers();
    G.layout_changed = 1;
    break;

  case KEY_MINUS: // Shrink buffer by row
    if (G.size > G.columns+1) G.size -= G.columns;
    AllocateBuffers();
    ReadMemory(G.buffer);
    UpdateBuffers();
    G.layout_changed = 1;
    break;

  case KEY_EQUAL: // Grow buffer by row
    G.size += G.columns;
    AllocateBuffers();
    ReadMemory(G.buffer);
    UpdateBuffers();
    G.layout_changed = 1;
    break;

  case KEY_UP:
    if (G.d_addr < G.columns) break;  // Forbid negatives
    G.addr -= G.columns;
    G.d_addr -= G.columns;
    ResetStates();
    UpdateBuffers();
    break;

  case KEY_DOWN:
    G.addr += G.columns;
    G.d_addr += G.columns;
    ResetStates();
    UpdateBuffers();
    break;

  case KEY_RIGHT:
    G.addr++;
    G.d_addr++;
    ResetStates();
    UpdateBuffers();
    break;

  case KEY_LEFT:
    if (!G.d_addr) break;
    G.addr--;
    G.d_addr--;
    ResetStates();
    UpdateBuffers();
    break;

  case KEY_HOME:
    G.addr -= G.d_addr;
    G.d_addr = 0;
    ResetStates();
    UpdateBuffers();
    break;

  case KEY_PAGE_UP:
    if (G.d_addr < G.size) break;
    G.addr -= G.size;
    G.d_addr -= G.size;
    ResetStates();
    UpdateBuffers();
    break;

  case KEY_PAGE_DOWN:
    G.addr += G.size;
    G.d_addr += G.size;
    ResetStates();
    UpdateBuffers();
    break;
  }
}


int main(int argc, char *argv[]) {

  // Commandline setup fluff
  char *font_file = "./font.ttf";
  int font_size = 8;
  char d_addr_set = 0;  // Guard to safely detext 0x00 as explicitly set d_addr
  char opt;

  while ((opt = getopt(argc, argv, "f:l:p:s:a:d:")) != -1) {
    switch (opt) {
      case 'f': font_file = optarg; break;
      case 'l': font_size = atoi(optarg); break;
      case 'p': G.pid     = strtoul(optarg, NULL, 0); break;
      case 's': G.size    = strtoul(optarg, NULL, 0); break;
      case 'a': G.addr    = strtoul(optarg, NULL, 0); break;
      case 'd':
        G.d_addr = strtoul(optarg, NULL, 0);
        d_addr_set = 1;
        break;
      default: PrintUsage(argv[0]); return 1;
    }
  }

  if (!G.pid || !G.addr) {
    PrintUsage(argv[0]);
    return 2;
  }

  if (!d_addr_set) G.d_addr = G.addr;

  // Initial window context setup
  InitWindow(320, 320, "Memwatch");
  int count;
  int *codepoints = LoadCodepoints(MYCHARS, &count);
  G.font = LoadFontEx(font_file, font_size, codepoints, count);

  Vector2 font_metrics = MeasureTextEx(G.font, "A", font_size, 1);
  G.chr_w = font_metrics.x;
  G.chr_h = font_metrics.y;

  // Init
  AllocateBuffers();

  // Prepare buffer, fail early on memory read error
  if (ReadMemory(G.buffer) < 0) {
    perror("process_vm_readv");
    UnloadFont(G.font);
    CloseWindow();
    return 1;
  }

  SetupWindow();
  SetTraceLogLevel(LOG_WARNING);

  while (!WindowShouldClose() && G.running) {

    BeginDrawing();

    ClearBackground(BLACK);
    DrawHeader();
    DrawAddr();
    DrawHex();
    HandleInput();

    EndDrawing();
    UpdateBuffers();
    RefreshCounters();

    if (G.layout_changed) {
      G.layout_changed = 0;
      SetupWindow();
    }
  }

  UnloadFont(G.font);
  CloseWindow();
  return 0;
}

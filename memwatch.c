#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>

#define GRAY  "\033[90m"
#define RED   "\033[31m"
#define GOLD  "\033[33m"
#define BRED  "\033[91m"
#define BLUE  "\033[34m"
#define BBLUE "\033[94m"
#define RESET "\033[0m"

#define COLUMNS 16
#define DELAY 8333 // Approx 120Hz
#define FADE_TIME 0x30

typedef struct {
    unsigned int untouched : 1;
    unsigned int direction : 1;  // set when increased, unset when decreased
    unsigned int counter : 6;
} State;

volatile int running = 1;
volatile State initial = {
  .untouched = 1,
  .direction = 0,
  .counter = FADE_TIME
};

void handle_sigint(int sig) {
  running = 0;
}

void hex_dump(
  uint8_t *buf,
  uint8_t *prev,
  State *states,
  size_t len,
  uintptr_t start_addr) {

  // Count rows in actual offset from memory to simplify math
  for (size_t row=0; row<len; row+=COLUMNS) {

    // Address line
    printf(GOLD "%12lx" RESET "│", start_addr + row);

    for (int column = 0; column < COLUMNS; column++) {
      size_t i = row + column;

      if (i > len) break;  // break early when we overflow

      // Reset state on slot change
      if (buf[i] != prev[i]) {
        states[i].untouched = 0;
        states[i].counter = FADE_TIME;
        states[i].direction = buf[i] > prev[i] ? 1 : 0;
      }

      // Not changed once, gray out zeroes
      if (states[i].untouched)
        buf[i] ? printf("%02x ", buf[i]) : printf(GRAY "%02x" RESET " ", buf[i]);
      else if (states[i].counter) // counter is nonzero, print bright
        states[i].direction
          ? printf(BRED  "%02x" RESET " ", buf[i])
          : printf(BBLUE "%02x" RESET " ", buf[i]);
      else
        states[i].direction
          ? printf(RED  "%02x" RESET " ", buf[i])
          : printf(BLUE "%02x" RESET " ", buf[i]);

      if (!states[i].untouched && states[i].counter)
        states[i].counter--;
    }

    puts("");
  }
}

ssize_t read_memory(pid_t pid, uintptr_t remote_addr, void *buf, size_t len) {

  struct iovec local  = {
    .iov_base = buf,
    .iov_len = len };
  struct iovec remote = {
    .iov_base = (void *)remote_addr,
    .iov_len = len };

  return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

void allocate_buffers(size_t size, uint8_t **buffer, uint8_t **prev, State **states) {
  *buffer = malloc(size);
  *prev = malloc(size);

  // Prepare color fade buffer
  *states = calloc(size, sizeof(State));

  for (size_t i = 0; i < size; i++) {
    (*states)[i] = initial;
  }
}

int main(int argc, char *argv[]) {

  if (argc != 4) {
    printf(
      "Usage: %s <pid> <hex_address> <size>\n",
      argv[0]);

    return 1;
  }

  signal(SIGINT, handle_sigint);

  pid_t pid = atoi(argv[1]);
  uintptr_t addr = strtoul(argv[2], NULL, 16);
  size_t size = strtoul(argv[3], NULL, 0);

  size_t lines = (size + COLUMNS-1) / COLUMNS;

  uint8_t *buffer = NULL;
  uint8_t *prev = NULL;
  State *states = NULL;

  allocate_buffers(size, &buffer, &prev, &states);

  // Move cursor to the top-right,  clear screen, disable cursor
  printf("\033[H\033[2J\033[?25l");
  fflush(stdout);

  // Print header line
  printf("PID:%8u┌", pid);
  for (int col = 0; col < COLUMNS; col++) {
    printf(GOLD "%02x" RESET " ", col);
  }
  puts("\n");

  // Prepare old buffer
  if (read_memory(pid, addr, prev, size) < 0) {
    perror("process_vm_readv");
    return 1;
  }

  while (running) {
    if (read_memory(pid, addr, buffer, size) < 0) {
      perror("process_vm_readv");
      break;
    }

    printf("\033[2;0H"); // Move to line 2
    hex_dump(buffer, prev, states, size, addr);
    fflush(stdout);

    memcpy(prev, buffer, size);
    usleep(DELAY);
  }

  printf("\nExiting.\033[?25h\n");
  fflush(stdout);

  free(buffer);
  free(prev);
  free(states);

  return 0;
}

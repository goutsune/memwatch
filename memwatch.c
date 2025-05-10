#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>

#define GRAY  "\033[90m"
#define RED   "\033[31m"
#define GOLD  "\033[33m"
#define BRED  "\033[91m"
#define BLUE  "\033[34m"
#define BBLUE "\033[94m"
#define RESET "\033[0m"

#define DELAY 8333 // Approx. 120Hz
#define FADE_TIME 0x30


typedef struct {
    unsigned int untouched : 1;
    unsigned int direction : 1;  // set when increased, unset when decreased
    unsigned int counter : 6;
} State;


volatile int running = 1;
volatile int columns = 16;
volatile size_t size = 0;

volatile State initial = {
  .untouched = 1,
  .direction = 0,
  .counter = FADE_TIME
};


void handle_sigint(int sig) {
  running = 0;
}


void set_nonblocking_input() {
  struct termios t;
  tcgetattr(STDIN_FILENO, &t);
  t.c_lflag &= ~(ICANON | ECHO); // raw mode, no echo
  tcsetattr(STDIN_FILENO, TCSANOW, &t);

  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}


void restore_input_mode() {
  struct termios t;
  tcgetattr(STDIN_FILENO, &t);
  t.c_lflag |= ICANON | ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
}


void hex_dump(uint8_t *buf, uint8_t *prev, State *states, uintptr_t start_addr, long int disp_addr) {

  // Count rows in actual offset from memory to simplify math
  for (size_t i=0; i<size; i++) {

  // Address line
  if (i % columns == 0 )                // The below keeps only 8 digits for the sake of readability.
    printf("\n" GOLD "%08lx" RESET "│", abs(disp_addr%100000000) + i);

  // Reset state on slot change
  if (buf[i] != prev[i]) {
    states[i].untouched = 0;
    states[i].counter = FADE_TIME;
    states[i].direction = buf[i] > prev[i] ? 1 : 0;
  }

  // Not changed once, gray out zeroes
  if (states[i].untouched)
    buf[i] ? printf(" %02x", buf[i]) : printf(" " GRAY "%02x" RESET, buf[i]);
  else if (states[i].counter) // counter is nonzero, print bright
    states[i].direction
      ? printf(" " BRED  "%02x" RESET, buf[i])
      : printf(" " BBLUE "%02x" RESET, buf[i]);
  else
    states[i].direction
      ? printf(" " RED  "%02x" RESET, buf[i])
      : printf(" " BLUE "%02x" RESET, buf[i]);

  if (!states[i].untouched && states[i].counter)
    states[i].counter--;
  }
}


ssize_t read_memory(pid_t pid, uintptr_t remote_addr, void *buf, size_t size) {

  struct iovec local  = {
    .iov_base = buf,
    .iov_len = size };
  struct iovec remote = {
    .iov_base = (void *)remote_addr,
    .iov_len = size };

  return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}


void reset_states(State **states) {
  // Prepare color fade buffer
  *states = calloc(size, sizeof(State));

  for (size_t i = 0; i < size; i++) {
    (*states)[i] = initial;
  }
}

void setup_terminal() {
  size_t lines = (size + columns - 1) / columns; // Ceil
  // Set xterm size, add extra line for header
  printf("\033[8;%d;%dt", lines + 1, columns*3 + 9);
  // Clear screen, disable cursor, disable wrapping, move cursor to the top-right
  printf("\033[2J\033[?25l\033[?7l\033[H");
  fflush(stdout);

  // Print header line
  printf("W_SZ:%3x┌", size);
  for (int col = 0; col < columns; col++) {
    printf(" " GOLD "%02x" RESET, col);
  }
}


void allocate_buffers(uint8_t **buffer, uint8_t **prev, State **states) {

  if (*buffer != NULL) free(*buffer);
  if (*prev   != NULL) free(*prev);
  if (*states != NULL) free(*states);

  *buffer = calloc(size, 1);
  *prev = calloc(size, 1);

  reset_states(states);
}


void handle_resize(int sig) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  columns = (w.ws_col - 9) / 3;
  setup_terminal();
}


int main(int argc, char *argv[]) {

  if (argc < 4) {
    printf(
      "Usage: %s <pid> <hex_address> <size> [display_addr]\n",
      argv[0]);

    return 1;
  }

  signal(SIGINT, handle_sigint);
  // signal(SIGWINCH, handle_resize);  // Does not seem to play nice when resize events pile up

  pid_t pid = atoi(argv[1]);
  uintptr_t addr = strtoul(argv[2], NULL, 16);
  size = strtoul(argv[3], NULL, 0);
  long int d_addr;
  if (argc == 5)
    d_addr = strtol(argv[4], NULL, 16);
  else
    d_addr = (long int)addr;

  uint8_t *buffer = NULL;
  uint8_t *prev = NULL;
  State *states = NULL;

  allocate_buffers(&buffer, &prev, &states);

  // Prepare old buffer
  if (read_memory(pid, addr, prev, size) < 0) {
    perror("process_vm_readv");
    return 1;
  }

  set_nonblocking_input();
  setup_terminal();

  // Input sequence buffer for up to 3 bytes to handle arrows
  char input_seq[4] = {0};
  ssize_t seq_len = 0;

  while (running) {

    // Read early to reallocate buffers before read
    seq_len = read(STDIN_FILENO, input_seq, sizeof(input_seq));

    // Input processing tree
    if (seq_len > 0) {
      switch (input_seq[0]) {

        case 'q':  // Exit
          running = 0;
          break;

        case '[': // Remove column
          if (columns > 2) columns--;
          setup_terminal();
          break;

        case ']': // Add column
          columns++;
          setup_terminal();
          break;

        case ',': // Shrink buffer
          if (size > 2) size--;
          allocate_buffers(&buffer, &prev, &states);
          read_memory(pid, addr, prev, size);
          setup_terminal(size);
          break;
        case '.': // Grow buffer
          size++;
          allocate_buffers(&buffer, &prev, &states);
          read_memory(pid, addr, prev, size);
          setup_terminal();
          break;

        case '\033': // ESC
          switch (input_seq[2]) {

            case 'A':  // Up
              if (d_addr < columns) break;  // Forbid negatives
              addr -= columns;
              d_addr -= columns;
              break;

            case 'B': // Down
              addr += columns;
              d_addr += columns;
              break;

            case 'C': // Right
              addr++;
              d_addr++;
              break;

            case 'D': // Left
              if (!d_addr) break;
              addr--;
              d_addr--;
              break;

          }
          // Reset colors on any esc input stroke for the sake of simplicity
          reset_states(&states);
          read_memory(pid, addr, prev, size);
          break;
      }

      memset(input_seq, 0, sizeof(input_seq));
    }

    if (read_memory(pid, addr, buffer, size) < 0) {
      perror("process_vm_readv");
      break;
    }

    printf("\033[1;0H"); // Move to line 2, so that header remains

    // DO BARREL ROLL
    hex_dump(buffer, prev, states, addr, d_addr);
    memcpy(prev, buffer, size);
    fflush(stdout);

    usleep(DELAY);
  }

  printf("\nExiting.\033[?25h\033[?7h\n");
  restore_input_mode();
  fflush(stdout);

  free(buffer);
  free(prev);
  free(states);

  return 0;
}

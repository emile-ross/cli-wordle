#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "valid_word_list.h"
#include "word_list.h"

// ====================== Terminal / Input ======================

#define ANSI_RESET "\033[0m"
#define ANSI_BRIGHT "\033[1m"
#define FG_DEFAULT "\033[39m"
#define FG_BLACK "\033[30m"
#define FG_RED "\033[31m"
#define FG_GREEN "\033[32m"
#define FG_YELLOW "\033[33m"
#define FG_BLUE "\033[34m"

static struct termios orig_termios;

static void disable_raw_mode(void) {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void) {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disable_raw_mode);

  struct termios raw = orig_termios;
  raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static char getch(void) {
  char c;
  if (read(STDIN_FILENO, &c, 1) != 1)
    return 0;
  return c;
}

static void get_terminal_size(int *w, int *h) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    *w = ws.ws_col;
    *h = ws.ws_row;
  } else {
    *w = 80;
    *h = 24;
  }
}

static void move_cursor(int x, int y) {
  printf("\033[%d;%dH", y + 1, x + 1);
  fflush(stdout);
}

static void erase_screen(void) {
  printf("\033[2J\033[H");
  fflush(stdout);
}

static void reset_attributes(void) {
  printf(ANSI_RESET);
  fflush(stdout);
}

// ====================== Game Types & Logic ======================

#define WORD_LEN 5
#define MAX_GUESSES 6
#define ALPHABET 26

// The order here defines priority in process_guess so do not reorder this enum
typedef enum {
  LETTER_DEFAULT,
  LETTER_UNUSED,
  LETTER_POSSIBLE,
  LETTER_CORRECT
} LetterState;

typedef struct {
  char chr;
  LetterState state;
} Letter;

typedef struct {
  int w, h;
  int cursor_y;
  Letter letters[ALPHABET];
  Letter guesses[MAX_GUESSES][WORD_LEN];
  int guess_count;
  char wordle[WORD_LEN + 1];
  char last_error[64];
} GameState;

static int char_to_index(char c) {
  if (!isalpha((unsigned char)c))
    return -1;
  return tolower((unsigned char)c) - 'a';
}

static int compare_str(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

static bool guess_valid(const char *guess) {
  char lower[WORD_LEN + 1];
  memcpy(lower, guess, WORD_LEN + 1);
  for (int i = 0; i < WORD_LEN; i++)
    lower[i] = (char)tolower((unsigned char)lower[i]);

  const char *key = lower;
  if (bsearch(&key, word_list, (size_t)word_list_len, sizeof(char *), compare_str))
    return true;
  if (bsearch(&key, valid_word_list, (size_t)valid_word_list_len, sizeof(char *),
              compare_str))
    return true;
  return false;
}

static bool process_guess(GameState *gs, const char *guess_upper) {
  Letter guess_letters[WORD_LEN];
  int remaining[ALPHABET] = {0};
  int correct_total = 0;

  for (int i = 0; i < WORD_LEN; i++) {
    int idx = char_to_index(gs->wordle[i]);
    if (idx < 0)
      continue;
    remaining[idx]++;
  }

  for (int i = 0; i < WORD_LEN; i++) {
    guess_letters[i].chr = guess_upper[i];
    guess_letters[i].state = LETTER_UNUSED;

    if (guess_upper[i] == gs->wordle[i]) {
      guess_letters[i].state = LETTER_CORRECT;
      correct_total++;
      remaining[char_to_index(guess_upper[i])]--;
    }
  }

  for (int i = 0; i < WORD_LEN; i++) {
    if (guess_letters[i].state == LETTER_CORRECT)
      continue;

    int idx = char_to_index(guess_letters[i].chr);
    if (idx < 0)
      continue;
    if (remaining[idx] > 0) {
      guess_letters[i].state = LETTER_POSSIBLE;
      remaining[idx]--;
    }
  }

  for (int i = 0; i < WORD_LEN; i++) {
    int idx = char_to_index(guess_letters[i].chr);
    if (idx < 0)
      continue;
    if (gs->letters[idx].state != LETTER_CORRECT &&
        guess_letters[i].state > gs->letters[idx].state) {
      gs->letters[idx].state = guess_letters[i].state;
    }
  }

  memcpy(gs->guesses[gs->guess_count], guess_letters, sizeof(guess_letters));
  gs->guess_count++;
  return correct_total == WORD_LEN;
}

// ====================== Drawing functions =======================

static void write_colored_letter(char c, LetterState state) {
  const char *color = FG_DEFAULT;
  switch (state) {
  case LETTER_DEFAULT:
    color = FG_DEFAULT ANSI_BRIGHT;
    break;
  case LETTER_UNUSED:
    color = FG_BLACK ANSI_BRIGHT;
    break;
  case LETTER_POSSIBLE:
    color = FG_YELLOW ANSI_BRIGHT;
    break;
  case LETTER_CORRECT:
    color = FG_GREEN ANSI_BRIGHT;
    break;
  }
  printf("%s%c%s ", color, c, ANSI_RESET);
  fflush(stdout);
}

static void draw_keyboard(GameState *gs) {
  const char *rows[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  int offsets[3] = {10, 9, 7};
  for (int r = 0; r < 3; r++) {
    move_cursor((gs->w / 2) - offsets[r], gs->cursor_y);
    for (int i = 0; rows[r][i]; i++) {
      write_colored_letter(rows[r][i],
                           gs->letters[char_to_index(rows[r][i])].state);
    }
    gs->cursor_y++;
  }
}

static void draw_guesses(GameState *gs) {
  int x = (gs->w / 2) - WORD_LEN + 1;
  for (int i = 0; i < gs->guess_count; i++) {
    move_cursor(x, gs->cursor_y);
    printf("%d. ", i + 1);
    for (int j = 0; j < WORD_LEN; j++) {
      write_colored_letter(gs->guesses[i][j].chr, gs->guesses[i][j].state);
    }
    gs->cursor_y++;
  }
}

static void write_centered(GameState *gs, const char *color, const char *text) {
  int x = (gs->w - (int)strlen(text)) / 2;
  // Prevent negative values on tiny terminals
  if (x < 0)
    x = 0;
  move_cursor(x, gs->cursor_y);
  printf("%s%s%s", color, text, ANSI_RESET);
  fflush(stdout);
}

static void write_line_centered(GameState *gs, const char *color,
                                const char *text) {
  write_centered(gs, color, text);
  printf("\n");
  gs->cursor_y++;
}

static void draw_prompt(GameState *gs) {
  erase_screen();
  gs->cursor_y = gs->h / 2;
  write_line_centered(gs, FG_BLUE ANSI_BRIGHT, "CLI Wordle!");
  write_line_centered(gs, FG_DEFAULT, "");
  draw_keyboard(gs);
  write_line_centered(gs, FG_DEFAULT, "");
  if (gs->guess_count > 0) {
    draw_guesses(gs);
    write_line_centered(gs, FG_DEFAULT, "");
  }
  if (gs->last_error[0]) {
    write_line_centered(gs, FG_RED ANSI_BRIGHT, gs->last_error);
    gs->last_error[0] = '\0';
  }
}

static void draw_won(GameState *gs) {
  draw_prompt(gs);
  write_line_centered(gs, FG_GREEN ANSI_BRIGHT, "A winrar is you!");
  char buf[64];
  snprintf(buf, sizeof(buf), "Won in %d guesses", gs->guess_count);
  write_line_centered(gs, FG_DEFAULT, buf);
}

static void draw_lost(GameState *gs) {
  draw_prompt(gs);
  write_line_centered(gs, FG_RED ANSI_BRIGHT, "Better luck next time, kid!");
  char buf[64];
  snprintf(buf, sizeof(buf), "Word was %s", gs->wordle);
  write_line_centered(gs, FG_DEFAULT, buf);
}

static bool get_and_process_guess(GameState *gs) {
  draw_prompt(gs);
  write_centered(gs, FG_DEFAULT, "Enter a guess: ");
  fflush(stdout);

  char guess[WORD_LEN + 1] = {0};
  int len = 0;

  while (true) {
    char ch = getch();
    if (ch == 3)
      exit(0);

    if (ch == '\r' || ch == '\n') {
      if (len == WORD_LEN && guess_valid(guess)) {
        return process_guess(gs, guess);
      } else {
        snprintf(gs->last_error, sizeof(gs->last_error), "%s",
                 "Invalid word or wrong length");
        draw_prompt(gs);
        write_centered(gs, FG_DEFAULT, "Enter a guess: ");
        len = 0;
        memset(guess, 0, sizeof(guess));
      }
    } else if ((ch == '\b' || ch == 127) && len > 0) {
      len--;
      printf("\b \b");
      fflush(stdout);
    } else if (isalpha((unsigned char)ch) && len < WORD_LEN) {
      guess[len++] = (char)toupper((unsigned char)ch);
      printf("%c", (char)toupper((unsigned char)ch));
      fflush(stdout);
    }
  }
}

// ====================== Main Loop =======================

static bool run_game(GameState *gs) {
  bool won = false;
  while (!won && gs->guess_count < MAX_GUESSES) {
    won = get_and_process_guess(gs);
  }
  if (won)
    draw_won(gs);
  else
    draw_lost(gs);

  printf("\nPlay again? [Y/n] ");
  fflush(stdout);
  char again = getch();
  return (again == 'y' || again == 'Y' || again == '\r' || again == '\n');
}

static void new_game(GameState *gs) {
  memset(gs, 0, sizeof(*gs));
  get_terminal_size(&gs->w, &gs->h);

  if (gs->w < 40 || gs->h < 20) {
    fprintf(stderr, "Terminal too small. Need at least 40x20\n");
    exit(1);
  }

  const char *qwerty = "QWERTYUIOPASDFGHJKLZXCVBNM";
  for (int i = 0; i < ALPHABET; i++) {
    gs->letters[i].chr = qwerty[i];
    gs->letters[i].state = LETTER_DEFAULT;
  }

  if (word_list_len <= 0) {
    fprintf(stderr,
            "Error: word_list is empty. Check the generated headers.\n");
    exit(1);
  }

  int idx = rand() % word_list_len;
  memcpy(gs->wordle, word_list[idx], WORD_LEN + 1);
  for (int i = 0; gs->wordle[i]; i++)
    gs->wordle[i] = (char)toupper((unsigned char)gs->wordle[i]);
}

static void handle_sigint(int sig) {
  (void)sig;
  exit(0);
}

int main(void) {
  signal(SIGINT, handle_sigint);
  atexit(erase_screen);
  atexit(reset_attributes);
  srand(time(NULL));

  enable_raw_mode();
  bool play_again = true;
  while (play_again) {
    GameState game;
    new_game(&game);
    play_again = run_game(&game);
  }
  return 0;
}

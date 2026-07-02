#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static char **word_list = NULL;
static int word_list_len = 0;

static char **valid_word_list = NULL;
static int valid_word_list_len = 0;

// ====================== Word list loading ======================
static int compare_str(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

static bool load_word_list(const char *filename, char ***list, int *len) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    fprintf(stderr, "Error: Could not open %s\n", filename);
    return false;
  }

  char *line = NULL;
  size_t len_line = 0;
  ssize_t read;
  int capacity = 1024;
  *list = malloc(capacity * sizeof(char *));
  *len = 0;

  while ((read = getline(&line, &len_line, f)) != -1) {
    // Trim newline and whitespace
    line[strcspn(line, "\r\n")] = '\0';
    for (int i = strlen(line) - 1; i >= 0 && isspace(line[i]); i--)
      line[i] = '\0';

    if (strlen(line) != 5)
      continue; // Only accept 5-letter words

    if (*len >= capacity) {
      capacity *= 2;
      *list = realloc(*list, capacity * sizeof(char *));
    }

    (*list)[*len] = strdup(line);
    (*len)++;
  }

  free(line);
  fclose(f);

  // Sort the list (so bsearch works even if the file wasn't sorted)
  qsort(*list, *len, sizeof(char *), compare_str);

  return true;
}

static void free_word_lists(void) {
  for (int i = 0; i < word_list_len; i++)
    free(word_list[i]);
  free(word_list);

  for (int i = 0; i < valid_word_list_len; i++)
    free(valid_word_list[i]);
  free(valid_word_list);
}

// ====================== Terminal / Input ======================
#define ANSI_RESET "\033[0m"
#define ANSI_BRIGHT "\033[1m"
#define FG_DEFAULT "\033[39m"
#define FG_BLACK "\033[30m"
#define FG_RED "\033[31m"
#define FG_GREEN "\033[32m"
#define FG_YELLOW "\033[33m"

static struct termios orig_termios;

static void disable_raw_mode(void) {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void) {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disable_raw_mode);
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
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

// ====================== Game Types ======================
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
  Letter letters[26];
  Letter guesses[6][5];
  int guess_count;
  char wordle[6];
  int chr_max_total[26];
  char last_error[64];
} GameState;

// ====================== Game Logic ======================
static int char_to_index(char c) { return tolower(c) - 'a'; }

static bool guess_valid(const char *guess) {
  char lower[6];
  strncpy(lower, guess, 5);
  lower[5] = '\0';
  for (int i = 0; i < 5; i++)
    lower[i] = tolower((unsigned char)lower[i]);

  const char *key = lower;

  if (bsearch(&key, word_list, word_list_len, sizeof(char *), compare_str))
    return true;
  if (bsearch(&key, valid_word_list, valid_word_list_len, sizeof(char *),
              compare_str))
    return true;
  return false;
}

static Letter check_letter(GameState *gs, char c, int pos) {
  Letter l = {.chr = c, .state = LETTER_UNUSED};
  if (gs->wordle[pos] == c)
    l.state = LETTER_CORRECT;
  else if (strchr(gs->wordle, (unsigned char)c))
    l.state = LETTER_POSSIBLE;
  return l;
}

static bool process_guess(GameState *gs, const char *guess_upper) {
  Letter guess_letters[5];
  int correct_total = 0;
  int chr_correct[26] = {0};
  int chr_possible[26] = {0};

  for (int i = 0; i < 5; i++) {
    char c = guess_upper[i];
    guess_letters[i] = check_letter(gs, c, i);
    int idx = char_to_index(c);

    if (guess_letters[i].state == LETTER_CORRECT) {
      correct_total++;
      chr_correct[idx]++;
    } else if (guess_letters[i].state == LETTER_POSSIBLE) {
      chr_possible[idx]++;
    }
  }

  for (int i = 4; i >= 0; i--) {
    if (guess_letters[i].state == LETTER_POSSIBLE) {
      int idx = char_to_index(guess_letters[i].chr);
      if (chr_possible[idx] > gs->chr_max_total[idx] - chr_correct[idx]) {
        chr_possible[idx]--;
        guess_letters[i].state = LETTER_UNUSED;
      }
    }
  }

  for (int i = 0; i < 5; i++) {
    int idx = char_to_index(guess_letters[i].chr);
    if (gs->letters[idx].state != LETTER_CORRECT) {
      gs->letters[idx].state = guess_letters[i].state;
    }
  }

  memcpy(gs->guesses[gs->guess_count], guess_letters, sizeof(guess_letters));
  gs->guess_count++;
  return correct_total == 5;
}

// ====================== Drawing ======================
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
  int x = (gs->w / 2) - 6;
  for (int i = 0; i < gs->guess_count; i++) {
    move_cursor(x, gs->cursor_y);
    printf("%d. ", i + 1);
    for (int j = 0; j < 5; j++) {
      write_colored_letter(gs->guesses[i][j].chr, gs->guesses[i][j].state);
    }
    gs->cursor_y++;
  }
}

static void write_centered(GameState *gs, const char *color, const char *text) {
  int x = (gs->w / 2) - (strlen(text) / 2);
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
  write_line_centered(gs, FG_RED ANSI_BRIGHT, "CLI Wordle!");
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
  write_line_centered(gs, FG_GREEN ANSI_BRIGHT, "Better luck next time, kid!");
  char buf[64];
  snprintf(buf, sizeof(buf), "Word was %s", gs->wordle);
  write_line_centered(gs, FG_DEFAULT, buf);
}

// ====================== Game Loop ======================
static bool get_and_process_guess(GameState *gs) {
  draw_prompt(gs);
  write_centered(gs, FG_DEFAULT, "Enter a guess: ");
  fflush(stdout);

  char guess[6] = {0};
  int len = 0;

  while (true) {
    char ch = getch();
    if (ch == 3)
      exit(0);

    if (ch == '\r' || ch == '\n') {
      if (len == 5 && guess_valid(guess)) {
        return process_guess(gs, guess);
      } else {
        strncpy(gs->last_error, "Invalid word or wrong length",
                sizeof(gs->last_error) - 1);
        draw_prompt(gs);
        write_centered(gs, FG_DEFAULT, "Enter a guess: ");
        len = 0;
        memset(guess, 0, sizeof(guess));
      }
    } else if ((ch == '\b' || ch == 127) && len > 0) {
      len--;
      printf("\b \b");
      fflush(stdout);
    } else if (isalpha(ch) && len < 5) {
      guess[len++] = toupper(ch);
      printf("%c", toupper(ch));
      fflush(stdout);
    }
  }
}

static bool run_game(GameState *gs) {
  bool won = false;
  while (!won && gs->guess_count < 6) {
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

  const char *qwerty = "QWERTYUIOPASDFGHJKLZXCVBNM";
  for (int i = 0; i < 26; i++) {
    gs->letters[i].chr = qwerty[i];
    gs->letters[i].state = LETTER_DEFAULT;
  }

  int idx = rand() % word_list_len;
  strncpy(gs->wordle, word_list[idx], 5);
  gs->wordle[5] = '\0';
  for (int i = 0; gs->wordle[i]; i++)
    gs->wordle[i] = toupper(gs->wordle[i]);

  for (int i = 0; gs->wordle[i]; i++) {
    int c = char_to_index(gs->wordle[i]);
    if (gs->chr_max_total[c] == 0) {
      for (int j = 0; gs->wordle[j]; j++)
        if (char_to_index(gs->wordle[j]) == c)
          gs->chr_max_total[c]++;
    }
  }
}

int main(void) {
  if (!load_word_list("word_list.txt", &word_list, &word_list_len) ||
      !load_word_list("valid_word_list.txt", &valid_word_list,
                      &valid_word_list_len)) {
    return 1;
  }
  atexit(free_word_lists);

  enable_raw_mode();
  atexit(erase_screen);

  srand(time(NULL));

  bool play_again = true;
  while (play_again) {
    GameState game;
    new_game(&game);
    play_again = run_game(&game);
  }
  return 0;
}

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "valid_word_list.h"

typedef struct {
  char chr;
  int pos; // 0-based position
} PossibleLetter;

// Parse string like "e2a4" into PossibleLetter array
static void parse_possible(const char *input, PossibleLetter *possibles,
                           int *count) {
  *count = 0;
  size_t len = strlen(input);

  for (size_t i = 0; i + 1 < len && *count < 10; i += 2) {
    char c = (char)tolower((unsigned char)input[i]);
    if (!isalpha(c))
      continue;

    int pos = input[i + 1] - '1';
    if (pos < 0 || pos > 4)
      pos = -1;

    possibles[*count].chr = c;
    possibles[*count].pos = pos;
    (*count)++;
  }
}

// Check if word matches the "correct" pattern (e.g. _e___)
static bool matches_correct(const char *word, const char correct[5]) {
  for (int i = 0; i < 5; i++) {
    if (correct[i] != '_' && tolower(word[i]) != correct[i]) {
      return false;
    }
  }
  return true;
}

// Check that word contains none of the excluded letters
static bool has_no_excluded(const char *word, const char *excluded) {
  for (int i = 0; excluded[i]; i++) {
    if (strchr(word, tolower(excluded[i]))) {
      return false;
    }
  }
  return true;
}

// Check "possible" constraints: letter must exist, but not in forbidden
// position
static bool satisfies_possible(const char *word,
                               const PossibleLetter *possibles, int count) {
  for (int i = 0; i < count; i++) {
    char c = possibles[i].chr;
    int forbidden_pos = possibles[i].pos;

    // Letter must appear somewhere in the word
    if (!strchr(word, c)) {
      return false;
    }

    // If position is specified, it must NOT be in that position
    if (forbidden_pos >= 0 && tolower(word[forbidden_pos]) == c) {
      return false;
    }
  }
  return true;
}

static int compare_strings(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

static void find_candidates(const char correct[5],
                            const PossibleLetter *possibles, int pcount,
                            const char *excluded) {
  char *candidates[2048]; // plenty of space
  int candidate_count = 0;

  // Check valid guess words
  for (int i = 0; i < valid_word_list_len; i++) {
    const char *w = valid_word_list[i];
    if (matches_correct(w, correct) && has_no_excluded(w, excluded) &&
        satisfies_possible(w, possibles, pcount)) {
      candidates[candidate_count++] = (char *)w;
    }
  }

  // Sort and print
  qsort(candidates, (size_t)candidate_count, sizeof(char *), compare_strings);

  printf("Possible candidates (%d):\n", candidate_count);
  for (int i = 0; i < candidate_count; i++) {
    for (int j = 0; j < 5; j++) {
      putchar(toupper(candidates[i][j]));
    }
    putchar(' ');
  }
  putchar('\n');
}

static void prompt_and_read(const char *prompt, char *buffer, int size) {
  printf("%s", prompt);
  fflush(stdout);

  if (!fgets(buffer, size, stdin)) {
    buffer[0] = '\0';
    return;
  }
  buffer[strcspn(buffer, "\r\n")] = '\0';
}

int main(void) {
  char correct[16] = {0};
  char possible[32] = {0};
  char excluded[32] = {0};

  prompt_and_read("Enter correct letters (e.g. _e___): ", correct,
                  sizeof(correct));

  if (strlen(correct) != 5) {
    fprintf(stderr, "Correct must be exactly 5 characters\n");
    return 1;
  }

  // Convert correct pattern to lowercase
  for (int i = 0; i < 5; i++) {
    if (correct[i] != '_') {
      correct[i] = (char)tolower((unsigned char)correct[i]);
    }
  }

  prompt_and_read("Enter possible letters with positions (e.g. e2a4): ",
                  possible, sizeof(possible));

  prompt_and_read("Enter excluded letters: ", excluded, sizeof(excluded));

  PossibleLetter possibles[16];
  int pcount = 0;
  parse_possible(possible, possibles, &pcount);

  find_candidates(correct, possibles, pcount, excluded);

  return 0;
}

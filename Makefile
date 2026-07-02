.PHONY: all clean

all: wordle solver

word_list.txt valid_word_list.txt:
	@echo "Generating word list text files..."
	python3 generate_word_lists.py

word_list.h valid_word_list.h: word_list.txt valid_word_list.txt
	@echo "Generating C header files..."
	python3 generate_headers.py

wordle: wordle.c word_list.h valid_word_list.h
	gcc -o wordle wordle.c -Wall -Wextra
	@echo "Build complete: ./wordle"

solver: solver.c word_list.h valid_word_list.h
	gcc -o solver solver.c -Wall -Wextra
	@echo "Build complete: ./solver"

clean:
	rm -f wordle
	rm -f word_list.txt valid_word_list.txt
	rm -f word_list.h valid_word_list.h
	@echo "Cleaned up generated files."

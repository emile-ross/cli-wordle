#!/usr/bin/env python3
"""
Downloads the original Wordle word lists and generates:
- word_list.txt       → Possible secret words (~2,315)
- valid_word_list.txt → All acceptable guesses (~12,972)
"""

import urllib.request
import urllib.error

# Official sources (widely used and stable)
SOLUTIONS_URL = "https://gist.githubusercontent.com/cfreshman/a03ef2cba789d8cf00c08f767e0fad7b/raw/"
ALLOWED_URL = "https://gist.githubusercontent.com/cfreshman/cdcdf777450c5b5301e439061d29694c/raw/"


def download_list(url: str) -> list[str]:
    """Download a word list from URL and return list of 5-letter lowercase words."""
    print(f"Downloading from {url}...")
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            content = response.read().decode("utf-8")
    except urllib.error.URLError as e:
        print(f"Error downloading: {e}")
        return []

    words = []
    for line in content.splitlines():
        word = line.strip().lower()
        if len(word) == 5 and word.isalpha():
            words.append(word)
    return words


def main():
    # Download solutions
    solutions = download_list(SOLUTIONS_URL)
    if not solutions:
        print("Failed to download solutions list. Exiting.")
        return

    print(f"Loaded {len(solutions)} solution words.")

    # Download allowed guesses
    allowed = download_list(ALLOWED_URL)
    if not allowed:
        print("Failed to download allowed guesses list. Exiting.")
        return

    print(f"Loaded {len(allowed)} allowed guess words.")

    # Create valid_word_list = solutions + allowed (deduplicated)
    valid_set = set(solutions) | set(allowed)
    valid_words = sorted(valid_set)

    solutions_sorted = sorted(solutions)

    # Write files
    with open("word_list.txt", "w") as f:
        f.write("\n".join(solutions_sorted) + "\n")
    print(f"Created word_list.txt with {len(solutions_sorted)} words.")

    with open("valid_word_list.txt", "w") as f:
        f.write("\n".join(valid_words) + "\n")
    print(f"Created valid_word_list.txt with {len(valid_words)} words.")

    print("\nDone! You can now compile and run your C Wordle game.")


if __name__ == "__main__":
    main()

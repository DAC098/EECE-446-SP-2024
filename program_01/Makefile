# EECE-446-SP-2024 | David Cathers | Madison Webb

EXE = h1-counter
CFLAGS = -Wall -Werror
CC = gcc

.PHONY: all
all: $(EXE)

h1-counter: h1-counter.c
	$(CC) $(CFLAGS) h1-counter.c -o h1-counter

.PHONY: clean
clean:
	rm -f $(EXE)

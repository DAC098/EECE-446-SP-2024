EXE = h1-counter
CFLAGS = -Wall
CXXFLAGS = -Wall
LDLIBS =
CC = gcc
CXX = g++

.PHONY: all
all: $(EXE)

# Implicit rules defined by Make, but you can redefine if needed
#
#h1-counter: h1-counter.c
#	$(CC) $(CFLAGS) h1-counter.c $(LDLIBS) -o h1-counter
#
# OR
#
#h1-counter: h1-counter.cc
#	$(CXX) $(CXXFLAGS) h1-counter.cc $(LDLIBS) -o h1-counter

.PHONY: clean
clean:
	rm -f $(EXE)

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude

LIBS = -lSDL2 -lm
SRC = src/main.c src/parser.c src/audio.c src/sequencer.c src/dawn_format.c
OBJ = $(SRC:.c=.o)

TARGET = dawn

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run

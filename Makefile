CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags gtk+-3.0` -lm
LDFLAGS = `pkg-config --libs gtk+-3.0` -lm

TARGET = splashy_c
SRC = splashy.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean

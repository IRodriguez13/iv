# Makefile para iv - editor minimalista modular

CC = gcc
CFLAGS = -Wall -O2
TARGET = iv

SRCS = main.c view.c edit.c range.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c iv.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	sudo cp $(TARGET) /usr/bin/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all install clean

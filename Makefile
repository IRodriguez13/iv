# Makefile for iv - minimal modular editor

CC = gcc
CFLAGS = -Wall -O2
TARGET = iv
PREFIX = /usr
BINDIR = $(PREFIX)/bin
# Usa pkg-config para detectar la ubicación correcta (recomendado)
COMPLETION_DIR = $(shell pkg-config --variable=completionsdir bash-completion 2>/dev/null || echo /etc/bash_completion.d)

SRCS = main.c view.c edit.c range.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c iv.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET) install-completions
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo ""
	@echo "Completions installed. They will auto-load in NEW shells."
	@echo "To activate in current shell: source $(COMPLETION_DIR)/iv"

install-completions:
	@if [ -f completions/iv.bash ]; then \
		install -d $(DESTDIR)$(COMPLETION_DIR); \
		install -m 644 completions/iv.bash $(DESTDIR)$(COMPLETION_DIR)/iv; \
		echo "✓ Completions installed to $(COMPLETION_DIR)/iv"; \
	else \
		echo "Warning: completions/iv.bash not found"; \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(COMPLETION_DIR)/iv

check-completions:
	@bash -n $(COMPLETION_DIR)/iv && echo "✓ Syntax OK" || echo "✗ Syntax error"

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all install install-completions uninstall check-completions clean
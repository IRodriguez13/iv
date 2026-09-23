PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1
MANESDIR ?= $(PREFIX)/share/man/es/man1
BASH_COMPLETION_DIR ?= $(PREFIX)/share/bash-completion/completions
ZSH_COMPLETION_DIR ?= $(PREFIX)/share/zsh/site-functions
FISH_COMPLETION_DIR ?= $(PREFIX)/share/fish/vendor_completions.d

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

SRCS = main.c view.c edit.c range.c
OBJS = $(SRCS:.c=.o)
TARGET = iv

.PHONY: all clean install uninstall test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c iv.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -d "$(BINDIR)" "$(MANDIR)" "$(MANESDIR)" \
		"$(BASH_COMPLETION_DIR)" "$(ZSH_COMPLETION_DIR)" "$(FISH_COMPLETION_DIR)"
	install -m 755 $(TARGET) "$(BINDIR)/$(TARGET)"
	install -m 644 iv.1 "$(MANDIR)/iv.1"
	install -m 644 man/es/iv.1 "$(MANESDIR)/iv.1"
	install -m 644 completions/bash/iv "$(BASH_COMPLETION_DIR)/iv"
	install -m 644 completions/zsh/_iv "$(ZSH_COMPLETION_DIR)/_iv"
	install -m 644 completions/fish/iv.fish "$(FISH_COMPLETION_DIR)/iv.fish"
	-command -v mandb >/dev/null 2>&1 && mandb -q "$(MANDIR)" 2>/dev/null || true

uninstall:
	rm -f "$(BINDIR)/$(TARGET)" "$(MANDIR)/iv.1" "$(MANESDIR)/iv.1" \
		"$(BASH_COMPLETION_DIR)/iv" \
		"$(ZSH_COMPLETION_DIR)/_iv" \
		"$(FISH_COMPLETION_DIR)/iv.fish"

test: $(TARGET)
	@./tests/smoke.sh "$(CURDIR)/$(TARGET)"
	@./tests/completions.sh

clean:
	rm -f $(TARGET) $(OBJS)

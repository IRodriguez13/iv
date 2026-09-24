PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1
MANESDIR ?= $(PREFIX)/share/man/es/man1
BASH_COMPLETION_DIR ?= $(PREFIX)/share/bash-completion/completions
ZSH_COMPLETION_DIR ?= $(PREFIX)/share/zsh/site-functions
FISH_COMPLETION_DIR ?= $(PREFIX)/share/fish/vendor_completions.d

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

SRCDIR = src
BUILDDIR = build
SRCS = $(SRCDIR)/main.c $(SRCDIR)/view.c $(SRCDIR)/edit.c \
	$(SRCDIR)/range.c $(SRCDIR)/write.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
TARGET = iv

.PHONY: all clean install uninstall test test-musl

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/iv.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

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
	@./tests/safety.sh "$(CURDIR)/$(TARGET)"
	@./tests/completions.sh
	@./tests/run-misc.sh "$(CURDIR)/$(TARGET)"

# Same suite against a musl-linked binary (reviewer libc matrix).
test-musl:
	$(MAKE) clean
	$(MAKE) CC=musl-gcc TARGET=iv-musl
	@./tests/smoke.sh "$(CURDIR)/iv-musl"
	@./tests/safety.sh "$(CURDIR)/iv-musl"
	@./tests/run-misc.sh "$(CURDIR)/iv-musl"

clean:
	rm -f $(TARGET) iv-musl tests/helpers/eintr_read.so
	rm -rf $(BUILDDIR)

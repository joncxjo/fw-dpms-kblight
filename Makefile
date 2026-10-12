CC = gcc

CFLAGS = -g -O2 -std=c99 -Wall -Wextra -Wpedantic
# https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html
CFLAGS += -D_FORTIFY_SOURCE=2 -fstack-clash-protection -fstack-protector-strong -fPIE -pie
CFLAGS += -Wl,-z,nodlopen -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -Wl,--as-needed -Wl,--no-copy-dt-needed-entries
# https://github.com/Cyan4973/Writing_Safer_C_code/blob/master/4_compiler_warnings.md
CFLAGS += -Wcast-qual -Wcast-align -Wstrict-aliasing -Wpointer-arith -Winit-self -Wshadow -Wswitch-enum
CFLAGS += -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls -Wfloat-equal -Wundef -Wvla
CFLAGS += -Wformat=2 -Wconversion

DESTDIR ?=
PREFIX ?= /usr/local

PROG = fw-dpms-kblight

ifeq ($(PREFIX),/usr/local)
	systemd_unit_path = /usr/local/lib/systemd/system
else
	systemd_unit_path = /etc/systemd/system
endif

$(PROG):
	$(CC) $(CFLAGS) $(PROG).c -o $(PROG)

ci:
	$(CC) $(CFLAGS) -Werror -pedantic-errors -static $(PROG).c -o $(PROG)

install: $(PROG)
	install -d $(DESTDIR)$(PREFIX)/libexec
	install -m 755 $(PROG) $(DESTDIR)$(PREFIX)/libexec/$(PROG)
	install -d $(DESTDIR)$(systemd_unit_path)
	sed "s#/usr/local/#$(PREFIX)/#g" < $(PROG).service > $(PROG).service.temp
	install -m 755 $(PROG).service.temp $(DESTDIR)$(systemd_unit_path)/$(PROG).service

uninstall:
	rm -fv $(DESTDIR)$(systemd_unit_path)/$(PROG).service
	rm -fv $(DESTDIR)$(PREFIX)/libexec/$(PROG)

clean:
	rm -fv $(PROG)
	rm -fv $(PROG).service.temp

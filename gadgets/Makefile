# nezd gadgets
# (c) 2007 Robert Manea

include config.mk

SRC = dbar.c dbar-main.c gdbar.c gcpubar.c textwidth.c
OBJ = ${SRC:.c=.o}

all: options dbar gdbar gcpubar textwidth

options:
	@echo nezd gadgets build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"
#	@echo "LD       = ${LD}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: dbar.h config.mk

dbar: ${OBJ}
	@echo LD $@
	@${LD} -o $@ dbar-main.o dbar.o ${LDFLAGS}
	@strip $@

gdbar: ${OBJ}
	@echo LD $@
#	@${LD} -o $@ gdbar.o dbar.o ${LDFLAGS} -L${X11LIB} -lX11
	@${LD} -o $@ gdbar.o dbar.o ${LDFLAGS}
	@strip $@

gcpubar: ${OBJ}
	@echo LD $@
#	@${LD} -o $@ gcpubar.o dbar.o ${LDFLAGS} -L${X11LIB}
	@${LD} -o $@ gcpubar.o dbar.o ${LDFLAGS}
	@strip $@

textwidth: ${OBJ}
	@echo LD $@
	@${LD} -o $@ textwidth.o ${LDFLAGS} -L${X11LIB} -lX11
	@strip $@

clean:
	@echo cleaning
	@rm -f ${OBJ} dbar
	@rm -f ${OBJ} gdbar
	@rm -f ${OBJ} gcpubar
	@rm -f ${OBJ} textwidth

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f dbar ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dbar
	@cp -f gdbar ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/gdbar
	@cp -f gcpubar ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/gcpubar
	@cp -f textwidth ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/textwidth
	@echo install man pages to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@cp -f dbar.1 ${DESTDIR}${MANPREFIX}/man1/dbar.1
	@cp -f gdbar.1 ${DESTDIR}${MANPREFIX}/man1/gdbar.1
	@cp -f gcpubar.1 ${DESTDIR}${MANPREFIX}/man1/gcpubar.1
	@cp -f textwidth.1 ${DESTDIR}${MANPREFIX}/man1/textwidth.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/dbar
	@rm -f ${DESTDIR}${PREFIX}/bin/gdbar
	@rm -f ${DESTDIR}${PREFIX}/bin/gcpubar
	@rm -f ${DESTDIR}${PREFIX}/bin/textwidth
	@echo removing man pages from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dbar.1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/gdbar.1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/gcpubar.1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/textwidth.1

.PHONY: all options clean install uninstall

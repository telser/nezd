include config.mk

SRC = draw.c main.c util.c action.c
OBJ = ${SRC:.c=.o}

all: options nezd

options:
	@echo nezd build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"
#	@echo "LD       = ${LD}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: nezd.h action.h config.mk

nezd: ${OBJ}
	@echo LD $@
	@${LD} -o $@ ${OBJ} ${LDFLAGS}
	@strip $@
	@echo "Run ./help for documentation"

gadgets: $(MAKE) -C gadgets

clean:
	@echo cleaning
	@rm -f nezd ${OBJ} nezd-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p nezd-${VERSION}
	@mkdir -p nezd-${VERSION}/gadgets
	@mkdir -p nezd-${VERSION}/bitmaps
	@cp -R CREDITS LICENSE Makefile INSTALL README.nezd README help config.mk action.h nezd.h ${SRC} nezd-${VERSION}
	@cp -R gadgets/Makefile  gadgets/config.mk gadgets/README.dbar gadgets/textwidth.c gadgets/README.textwidth gadgets/dbar.c gadgets/gdbar.c gadgets/README.gdbar gadgets/gcpubar.c gadgets/README.gcpubar gadgets/kittscanner.sh gadgets/README.kittscanner gadgets/noisyalert.sh nezd-${VERSION}/gadgets
	@cp -R bitmaps/alert.xbm bitmaps/ball.xbm bitmaps/battery.xbm bitmaps/envelope.xbm bitmaps/volume.xbm bitmaps/pause.xbm bitmaps/play.xbm bitmaps/music.xbm  nezd-${VERSION}/bitmaps
	@tar -cf nezd-${VERSION}.tar nezd-${VERSION}
	@gzip nezd-${VERSION}.tar
	@rm -rf nezd-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f nezd ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/nezd
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@cp -f nezd.1 ${DESTDIR}${MANPREFIX}/man1/nezd.1

install-gadgets: gadgets
	$(MAKE) -c gadgets install

uninstall-gadgets: gadgets
	$(MAKE) -c gadgets uninstall

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/nezd
	@rm -f ${DESTDIR}${MANPREFIX}/man1/nezd.1

.PHONY: all options clean dist install uninstall gadgets install-gadgets uninstall-gadgets

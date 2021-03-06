# nezd version
VERSION = 0.1.0

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib
INCS = -I. -I${X11INC}

LIBS = -L${X11LIB} -lX11 -lXinerama -lXpm `pkg-config --libs xft`
CFLAGS += -Wall -Os ${INCS} -DVERSION=\"${VERSION}\" `pkg-config --cflags xft`

# END of feature configuration

LDFLAGS += ${LIBS}

# Solaris, uncomment for Solaris
#CFLAGS = -fast ${INCS} -DVERSION=\"${VERSION}\"
#LDFLAGS = ${LIBS}
#CFLAGS += -xtarget=ultra

# Debugging
#CFLAGS = ${INCS} -DVERSION=\"${VERSION}\" -std=gnu89 -pedantic -Wall -W -Wundef -Wendif-labels -Wshadow -Wpointer-arith -Wbad-function-cast -Wcast-align -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes -Wnested-externs -Winline -Wdisabled-optimization -O2 -pipe `pkg-config --cflags xft`
#LDFLAGS = ${LIBS}

# compiler and linker
CC ?= cc
LD = ${CC}

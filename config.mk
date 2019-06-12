# nezd version
VERSION = 0.0.1-development

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib
INCS = -I. -I${X11INC}

# Configure the features you want to be supported
# Only one of the following options has to be uncommented,
# all others must be commented!
#
# Uncomment: Remove # from the beginning of respective lines
# Comment  : Add # to the beginning of the respective lines

## Option 1: No Xinerama no XFT
#LIBS = -L${X11LIB} -lX11
#CFLAGS += -Wall -Os ${INCS} -DVERSION=\"${VERSION}\"

# Option 2: With Xinerama
#LIBS = -L${X11LIB} -lX11 -lXinerama
#CFLAGS += -Wall -Os ${INCS} -DVERSION=\"${VERSION}\" -DNEZD_XINERAMA

## Option 3: With XFT
#LIBS = -L${X11LIB} -lX11 `pkg-config --libs xft`
#CFLAGS += -Wall -Os ${INCS} -DVERSION=\"${VERSION}\" -DNEZD_XFT `pkg-config --cflags xft`

## Option 4: With Xinerama and XFT
LIBS = -L${X11LIB} -lX11 -lXinerama -lXpm `pkg-config --libs xft`
CFLAGS += -Wall -Os ${INCS} -DVERSION=\"${VERSION}\" -DNEZD_XINERAMA -DNEZD_XFT `pkg-config --cflags xft`

# END of feature configuration

LDFLAGS += ${LIBS}

# Solaris, uncomment for Solaris
#CFLAGS = -fast ${INCS} -DVERSION=\"${VERSION}\"
#LDFLAGS = ${LIBS}
#CFLAGS += -xtarget=ultra

# Debugging
#CFLAGS = ${INCS} -DVERSION=\"${VERSION}\" -std=gnu89 -pedantic -Wall -W -Wundef -Wendif-labels -Wshadow -Wpointer-arith -Wbad-function-cast -Wcast-align -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes -Wnested-externs -Winline -Wdisabled-optimization -O2 -pipe -DNEZD_XFT `pkg-config --cflags xft`
#LDFLAGS = ${LIBS}

# compiler and linker
CC ?= cc
LD = ${CC}

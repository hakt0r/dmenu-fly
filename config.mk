# dmenu version
VERSION = 4.0-fly

# Customize below to fit your system

# paths
PREFIX = /usr
MANPREFIX = ${PREFIX}/share/man

X11INC = /usr/X11R7/include
X11LIB = /usr/X11R7/lib

# Xinerama, comment if you don't want it
XINERAMALIBS = -L${X11LIB} -lXinerama
XINERAMAFLAGS = -DXINERAMA

# Xft, comment if you don't want it
XFTINCS = `pkg-config --cflags xft`
XFTLIBS = `pkg-config --libs xft`
XFTFLAGS = -DXFT

# includes and libs
INCS = -I. -I/usr/include -I${X11INC} ${XFTINCS}
LIBS = -L/usr/lib -lc -L${X11LIB} -lX11 ${XINERAMALIBS} ${XFTLIBS}

# flags
CPPFLAGS = -D_BSD_SOURCE -DVERSION=\"${VERSION}\" ${XINERAMAFLAGS} ${XFTFLAGS}
CFLAGS = -std=c99 -pedantic -Wall -Os ${INCS} ${CPPFLAGS}
LDFLAGS = -s ${LIBS}

# Solaris
#CFLAGS = -fast ${INCS} -DVERSION=\"${VERSION}\"
#LDFLAGS = ${LIBS}

# compiler and linker
CC = cc

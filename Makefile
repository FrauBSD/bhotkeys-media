############################################################ LICENSE
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
#
############################################################ IDENT(1)
#
# $Title: bhotkeys-media - play, rewind, and fast forward $
# $Copyright: 2026 Devin Teske. All rights reserved. $
# $FrauBSD: bhotkeys-media/Makefile 2026-10-05 19:44:02 -0700 Devin Teske $
#
############################################################ PATHS

PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
PLUGDIR?=	${PREFIX}/share/bhotkeys/plugins.d
MANDIR?=	${PREFIX}/share/man/man1

############################################################ PKG-CONFIG

PKG_CONFIG?=	pkg-config
PKG_CFLAGS!=	${PKG_CONFIG} --cflags dbus-1
PKG_LIBS!=	${PKG_CONFIG} --libs dbus-1

############################################################ COMPILER

CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra

############################################################ FILES

BIN=		bin/mpris-control
PLUG=		plugins.d/fn-play plugins.d/fn-prev plugins.d/fn-next
MAN1=		mpris-control

############################################################ TARGETS

.PHONY: all

all: ${BIN}

${BIN}: src/mpris-control.c
	${CC} ${CFLAGS} ${PKG_CFLAGS} -o ${.TARGET} src/mpris-control.c \
	    ${PKG_LIBS} -lkvm

.PHONY: install

install: all
	mkdir -p ${DESTDIR}${BINDIR} ${DESTDIR}${PLUGDIR} \
	    ${DESTDIR}${MANDIR}
	install -m 755 ${BIN} ${DESTDIR}${BINDIR}
	install -m 644 ${PLUG} ${DESTDIR}${PLUGDIR}
.for m in ${MAN1}
	gzip -cn man/${m}.1 > ${DESTDIR}${MANDIR}/${m}.1.gz
	chmod 444 ${DESTDIR}${MANDIR}/${m}.1.gz
.endfor

.PHONY: clean

clean:
	rm -f ${BIN}

################################################################################
# END
################################################################################

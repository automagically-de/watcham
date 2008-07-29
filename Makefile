CC = gcc
PKGS = gtk+-2.0
INCS = `pkg-config ${PKGS} --cflags`
LIBS = `pkg-config ${PKGS} --libs`
CFLAGS = -Wall -ansi -pedantic ${INCS}
BIN = watcham
OBJS = main.o

.c.o:
	${CC} -c ${CFLAGS} -o $@ $<

${BIN}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LIBS}

clean:
	rm -f ${BIN} ${OBJS}

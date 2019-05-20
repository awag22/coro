CC = gcc

# unix commands
RM  = /bin/rm -f

ifeq (${OSTYPE},Darwin)
        AR = libtool -o
        CFLAGS = -g -std=gnu11 -Wall -Qunused-arguments -Wno-implicit-function-declaration -Wno-implicit-int -Wno-unused-function -Wno-parentheses-equality -Wno-tautological-compare -Wno-self-assign
        OPTFLAGS  =   -g -Wpointer-arith
else
        CFLAGS =  -g -std=gnu11 -Wall -Wpointer-arith -Wno-return-type -Wno-implicit-function-declaration -Wno-implicit-int -Wno-unused-function
        OPTFLAGS  =  -g -Wpointer-arith
        AR = ar rcs
endif
LIB = -L.

.PREFIXES = .c .o         
.c.o:
	${CC} -c ${CFLAGS} ${INCLUDES} $<

TARGETS =  libut.a libchan.a
TLIB = uthread.o uthread_mutex_cond.o uthread_sem.o
CLIB = chan.o queue.o uthread.o uthread_mutex_cond.o uthread_sem.o

all: $(TLIB) $(CLIB) $(TARGETS)

libut.a: $(TLIB)
	${AR} $@ $^
	ranlib $@

libchan.a: $(CLIB)
	${AR} $@ $^
	ranlib $@

clean:
	$(RM) *.o *.a $(TARGETS)
tidy:
	$(RM) *.o


CC = cc
CFLAGS = -std=c89 -Wno-implicit-function-declaration -Wno-deprecated-non-prototype -I../pflayer

# PF layer objects
PF_OBJS = ../pflayer/pf.o ../pflayer/buf.o ../pflayer/hash.o ../pflayer/rm.o

# AM layer objects
AM_OBJS = \
    am.o amfns.o aminsert.o amsearch.o amscan.o amprint.o \
    amstack.o amglobals.o misc.o ambuild.o ambulk.o amstats.o ambench.o

# Default target
all: am_test amlayer.o

# Build benchmark executable
amtest: $(AM_OBJS) ../pflayer/pf.o ../pflayer/buf.o ../pflayer/hash.o ../pflayer/rm.o
	$(CC) $(CFLAGS) -I../pflayer -o amtest $(AM_OBJS) ../pflayer/pf.o ../pflayer/buf.o ../pflayer/hash.o ../pflayer/rm.o

# Relocatable module for linking with DB project
amlayer.o: $(AM_OBJS)
	ld -r $(AM_OBJS) -o amlayer.o

# --- Compile rules ---

am.o: am.c am.h ../pflayer/pf.h
	$(CC) $(CFLAGS) -c am.c

amfns.o: amfns.c am.h ../pflayer/pf.h
	$(CC) $(CFLAGS) -c amfns.c

aminsert.o: aminsert.c am.h ../pflayer/pf.h
	$(CC) $(CFLAGS) -c aminsert.c

amsearch.o: amsearch.c am.h ../pflayer/pf.h
	$(CC) $(CFLAGS) -c amsearch.c

amscan.o: amscan.c am.h ../pflayer/pf.h
	$(CC) $(CFLAGS) -c amscan.c

amprint.o: amprint.c am.h ../pflayer/pf.h
	$(CC) $(CFLAGS) -c amprint.c

amstack.o: amstack.c am.h ../pflayer/pf.h
	$(CC) $(CFLAGS) -c amstack.c

amglobals.o: amglobals.c am.h
	$(CC) $(CFLAGS) -c amglobals.c

misc.o: misc.c am.h
	$(CC) $(CFLAGS) -c misc.c

ambuild.o: ambuild.c am.h ../pflayer/pf.h amstats.h
	$(CC) $(CFLAGS) -I../pflayer -c ambuild.c

ambulk.o: ambulk.c am.h ../pflayer/pf.h amstats.h
	$(CC) $(CFLAGS) -I../pflayer -c ambulk.c

ambench.o: ambench.c am.h ../pflayer/pf.h amstats.h
	$(CC) $(CFLAGS) -I../pflayer -c ambench.c

amstats.o: amstats.c amstats.h
	$(CC) $(CFLAGS) -c amstats.c

# Clean rule
clean:
	rm -f *.o amlayer.o am_test

#PUBLICDIR= /usr0/cs564/public/project
CFLAGS = -std=c89 -Wno-implicit-function-declaration -Wno-deprecated-non-prototype
SRC= buf.c hash.c pf.c
OBJ= buf.o hash.o pf.o
HDR = pftypes.h pf.h 

# --- Explicit rule to compile .c files ---
# $@ means the target (e.g., buf.o)
# $< means the first dependency (e.g., buf.c)
%.o: %.c $(HDR)
	cc $(CFLAGS) -c -o $@ $<

# --- Targets ---

pflayer.o: $(OBJ)
	ld -r -o pflayer.o $(OBJ)

tests: testhash testpf

testpf: testpf.o pflayer.o
	cc $(CFLAGS) -o testpf testpf.o pflayer.o -lm

testhash: testhash.o pflayer.o
	cc $(CFLAGS) -o testhash testhash.o pflayer.o -lm

lint: 
	lint $(SRC)

install: pflayer.o 

clean:
	rm -f *.o pflayer.o testpf testhash
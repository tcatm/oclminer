INCLUDEPATHS=-I${ATISTREAMSDKROOT}/include/ -L${ATISTREAMSDKROOT}/

LIBS=-lOpenCL -ljansson -lcurl

DEFS=
DEBUGFLAGS=-g
CFLAGS=-O3 -Wformat $(DEBUGFLAGS) $(DEFS) $(INCLUDEPATHS)
HEADERS=

OBJS=miner.o ocl.o findnonce.o util.o

all: oclminer

%.o: %.c $(HEADERS)
	gcc -c $(CFLAGS) -o $@ $<

oclminer: $(OBJS) 
	gcc $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	-rm *.o oclminer

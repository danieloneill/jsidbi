JSIDIR=../src
DBICFLAGS=`pkg-config --cflags dbi`
DBILDFLAGS=`pkg-config --cflags --libs dbi`
ACFLAGS= -g -Wall -fPIC -I$(JSIDIR) -I.. -DJSI__MEMDEBUG=1 $(DBICFLAGS)
LDFLAGS=-lm -ldl -lpthread $(DBILDFLAGS)
SHFLAGS=-shared -fPIC 
ALLDEPS=Makefile

all: dbi.so

dbi.so: dbi.o $(ALLDEPS)
	$(LD) $(LDFLAGS) $(SHFLAGS) -o dbi.so dbi.o

dbi.o: dbi.c
	$(CC) $(ACFLAGS) -I$(JSIDIR) -c dbi.c -o dbi.o

clean:
	rm -rf *.so *.o


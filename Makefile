CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LFLAGS =

.PHONY: all clean

TARGET1 = ppcbc
TARGET2 = ppcbs

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(TARGET1).o ppcb-common.o ppcb-udp.o ppcb-udpr.o ppcb-tcp.o err.o
$(TARGET2): $(TARGET2).o ppcb-common.o ppcb-udp.o ppcb-udpr.o ppcb-tcp.o err.o

# To są zależności wygenerowane automatycznie za pomocą polecenia `gcc -MM *.c`.

err.o: err.c err.h
ppcbc.o: ppcbc.c err.h ppcb-common.h ppcb-tcp.h ppcb-udp.h ppcb-udpr.h
ppcb-common.o: ppcb-common.c ppcb-common.h err.h protconst.h
ppcbs.o: ppcbs.c ppcb-common.h err.h ppcb-tcp.h ppcb-udp.h ppcb-udpr.h
ppcb-tcp.o: ppcb-tcp.c ppcb-tcp.h err.h ppcb-common.h protconst.h
ppcb-udp.o: ppcb-udp.c ppcb-udp.h err.h ppcb-common.h
ppcb-udpr.o: ppcb-udpr.c ppcb-udpr.h err.h ppcb-common.h protconst.h


clean:
	rm -f $(TARGET1) $(TARGET2) *.o *~

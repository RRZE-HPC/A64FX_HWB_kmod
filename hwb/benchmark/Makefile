#
BHOME	=$(PWD)
#
CC	= gcc
#
#
COMP	= -fopenmp
COPTS	=  -O3 -fno-inline
#
#
LINKF=-lm
NOLINK= -c
#

all:	barrier.exe barrier_hwb.exe

barrier.exe: barrier.o timing.o
	$(CC) $(COMP) -o barrier.exe $^ $(LINKF)

barrier_hwb.exe: barrier_hwb.o timing.o
	$(CC) $(COMP) -I ${HOME}/a64fx_modules/hwb/ulib/include -L ${HOME}/a64fx_modules/hwb/ulib/BUILD/src/ -o barrier_hwb.exe $^ $(LINKF) -lFJhwb

%.o:  %.c
	$(CC) $(COPTS) $(COMP) -I ${HOME}/a64fx_modules/hwb/ulib/include $(NOLINK) $<

clean:
	rm -f *.o *.exe


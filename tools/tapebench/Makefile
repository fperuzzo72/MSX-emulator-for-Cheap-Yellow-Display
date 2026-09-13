# Runs the Spectrum tape generator against the real ROM and the real Z80,
# on this machine, with no board involved.
#
#   make
#   ./loader /path/to/48.rom "/path/to/game.tap"    # does the ROM accept it
#   ./load   /path/to/48.rom "/path/to/game.tap"    # does the game load
#
# No ROM and no tape are in this repository; supply your own.

ROOT  := ../..
CFLAGS := -O2 -Wall -DEXECZ80 -DLSB_FIRST \
          -I$(ROOT)/lib/z80 -I$(ROOT)/src/spectrum -I.
SRC   := bench.c tapefile.c $(ROOT)/src/spectrum/spectrum_tape.c $(ROOT)/lib/z80/Z80.c

all: loader load

loader: loader.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

load: load.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f loader load

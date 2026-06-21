#--------------------------------------------------------------------------
# luasocket makefile
# Test executable for luasocket library
# Diego Nehab, 29/8/1999
#--------------------------------------------------------------------------
CC = gcc

WARNINGS = -Wall -Wshadow -Wpointer-arith -Waggregate-return -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wnested-externs

CFLAGS = $(WARNINGS) -O2 
# to run the test scripts
# CFLAGS = $(WARNINGS) -g -D_DEBUG

# set this to your Lua-4.0 directory
LUA = /home/i/diego/lib/lua
LUALIB = $(LUA)/lib
LUAINC = $(LUA)/include

INC = -I$(LUAINC)

LIB = $(LUALIB)/liblualib.a $(LUALIB)/liblua.a -lm
# depending on your platform, you might need extra libraries
# LIB = $(LUA_LIB)/liblualib.a $(LUA_LIB)/liblua.a -lm -lsocket -lnsl
# LIB = $(LUA_LIB)/liblualib.a $(LUA_LIB)/liblua.a -lm -lnsl

luasocket: luasocket.o lua.o
	$(CC) $(CFLAGS) -o luasocket lua.o luasocket.o $(LIB)

lua.o: lua.c luasocket.h
	$(CC) -c $(CFLAGS) $(INC) -o lua.o lua.c

luasocket.o: luasocket.c luasocket.h
	$(CC) -c $(CFLAGS) $(INC) -o luasocket.o luasocket.c

# clean all trash
clean:
	rm -f lua.o luasocket.o
	rm -f luasocket

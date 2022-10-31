###############################
# Custom configuration values #
###############################
SO_BLOCK_SIZE = 5
SO_REGISTRY_SIZE = 2

# MACRO DECLARATION
# Custom configuration
PROJ_CONF = -D SO_BLOCK_SIZE=$(SO_BLOCK_SIZE) -D SO_REGISTRY_SIZE=$(SO_REGISTRY_SIZE)

# Project-defined configurations
CONF1 = -D SO_BLOCK_SIZE=100 -D SO_REGISTRY_SIZE=1000
CONF2 = -D SO_BLOCK_SIZE=10 -D SO_REGISTRY_SIZE=10000
CONF3 = -D SO_BLOCK_SIZE=10 -D SO_REGISTRY_SIZE=1000

# CONFIGURATION FLEXIBILITY
ifeq ($(cfg), 1)
override PROJ_CONF = $(CONF1)
endif

ifeq ($(cfg), 2)
override PROJ_CONF = $(CONF2)
endif

ifeq ($(cfg), 3)
override PROJ_CONF = $(CONF3)
endif

#####################
# COMPILER SETTINGS #
#####################
CC = gcc

# COMPILER FLAGS
CFLAGS = -std=c89 -pedantic -O2
LDFLAGS =

# DEBUG COMPILER FLAGS
CFLAGS_DBG = -std=c89 -pedantic -O0 -g -D DEBUG

# DEBUGGING FLEXIBILITY
ifeq ($(debug), 1)
override CFLAGS = $(CFLAGS_DBG)
endif

###########################################
# Other dependencies -- default behaviour #
###########################################
INCLUDES = src/*.h
COMMON_DEPS = $(INCLUDES) makefile

###############################################
# all, clean, run, debug, conf1, conf2, conf3 #
###############################################
all: check_folders bin/master bin/node bin/user

build/%.o: src/%.c $(COMMON_DEPS)
	$(CC) $(CFLAGS) $(PROJ_CONF) -c $< -o $@ $(LDFLAGS)

bin/master: build/common.o build/master.o $(COMMON_DEPS)
	$(CC) $(CFLAGS) $(PROJ_CONF) -o bin/master build/master.o build/common.o $(LDFLAGS)

bin/node: build/node.o build/common.o $(COMMON_DEPS)
	$(CC) $(CFLAGS) $(PROJ_CONF) -o bin/node build/node.o build/common.o $(LDFLAGS)

bin/user: build/user.o build/common.o $(COMMON_DEPS)
	$(CC) $(CFLAGS) $(PROJ_CONF) -o bin/user build/user.o build/common.o $(LDFLAGS)

clean:
	rm -f build/* bin/*

run: all
	./bin/master

check_folders: 
	mkdir -p build
	mkdir -p bin

###############################
# Custom configuration values #
###############################
SO_BLOCK_SIZE = 10
SO_REGISTRY_SIZE = 100

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
all: check_folders bin/master

build/%.o: src/%.c $(COMMON_DEPS)
	$(CC) $(CFLAGS) $(PROJ_CONF) -c $< -o $@ $(LDFLAGS)

bin/master: build/master.o build/common.o $(COMMON_DEPS)
	$(CC) $(CFLAGS) $(PROJ_CONF) -o bin/master build/master.o build/common.o $(LDFLAGS)

clean:
	rm -f build/* bin/*

run: all
	./bin/master
	
# Debug with custom settings
debug: 
	$(CC) $(CFLAGS_DBG) $(PROJ_CONF) $(TARGET_SOURCE) -o $(TARGET)

# Use project-defined configurations
conf1: 
	$(CC) $(CFLAGS) $(CONF1) $(TARGET_SOURCE) -o $(TARGET)

conf2: 
	$(CC) $(CFLAGS) $(CONF2) $(TARGET_SOURCE) -o $(TARGET)

conf3: 
	$(CC) $(CFLAGS) $(CONF3) $(TARGET_SOURCE) -o $(TARGET)

check_folders: 
	mkdir -p build
	mkdir -p bin

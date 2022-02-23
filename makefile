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

# COMPILER FLAGS
CFLAGS = -std=c89 -pedantic -O2
# QUICK NOTE:
# 	while i was studying BOF in Windows Environments 
# 	i found out a unique feature of the -O2 optimization flag:
#	Buffers are pushed into the stack after all the other local variables
#	to avoid the overwriting of those variables when exploiting
#   a buffer overflow vulnerability. (You could still do a lot of damage tho)
#   More -> https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
CFLAGS_DBG = -std=c89 -pedantic -O0 -g

###########################################
# Other dependencies -- default behaviour #
###########################################
# todo

##########
# Master #
##########
TARGET = master
TARGET_SOURCE = master.c

# Used with -> make all
$(TARGET): 
	$(CC) $(CFLAGS) $(PROJ_CONF) $(TARGET_SOURCE) -o $(TARGET)

###############################################
# all, clean, run, debug, conf1, conf2, conf3 #
###############################################
all: $(TARGET)

clean:
	rm -f *.o $(TARGET)

run: $(TARGET)
	./$(TARGET)
	
# Debug with custom settings
debug: 
	$(CC) $(CFLAGS_DBG) $(PROJ_CONF) -D DEBUG $(TARGET_SOURCE) -o $(TARGET)

# Use project-defined configurations
conf1: 
	$(CC) $(CFLAGS) $(CONF1) $(TARGET_SOURCE) -o $(TARGET)

conf2: 
	$(CC) $(CFLAGS) $(CONF2) $(TARGET_SOURCE) -o $(TARGET)

conf3: 
	$(CC) $(CFLAGS) $(CONF3) $(TARGET_SOURCE) -o $(TARGET)

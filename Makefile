CC=gcc
CFLAGS := -Wall -Wextra -pedantic
BIN_DIR := bin
BUILD_DIR := build
SERVER_DIR := server
CLIENT_DIR := client
LIB_DIR := lib


CLIENT_OBJS := $(patsubst %.cpp,%.o, $(wildcard $(CLIENT_DIR)/*.cpp) $(wildcard $(CLIENT_DIR)/**/*.hpp))
SERVER_OBJS := $(patsubst %.cpp,%.o, $(wildcard $(SERVER_DIR)/*.cpp) $(wildcard $(SERVER_DIR)/**/*.hpp))

default: all

all: server client

server: dir $(LIB_OBJS) $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/server $(patsubst %, build/%, $(SERVER_OBJS))

client: dir $(LIB_OBJS) $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/client $(patsubst %, build/%, $(CLIENT_OBJS))

$(CLIENT_OBJS): dir
	@mkdir -p $(BUILD_DIR)/$(@D)
	@$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ -c $*.cpp

$(SERVER_OBJS): dir
	@mkdir -p $(BUILD_DIR)/$(@D)
	@$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ -c $*.cpp

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

dir:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

re: fclean all

.PHONY: all client server fclean re lib

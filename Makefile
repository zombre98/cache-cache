CC=gcc
CFLAGS := -Wall -Wextra -pedantic
BIN_DIR := bin
BUILD_DIR := build
SERVER_DIR := server
CLIENT_DIR := client
LIB_DIR := lib


CLIENT_OBJS := $(patsubst %.cpp,%.o, $(wildcard $(CLIENT_DIR)/*.cpp) $(wildcard $(LIB_DIR)/**/*.cpp))
SERVER_OBJS := $(patsubst %.cpp,%.o, $(wildcard $(SERVER_DIR)/*.cpp) $(wildcard $(LIB_DIR)/**/*.cpp))


default: all

all: server client

server: dir $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/server $(patsubst %, build/%, $(SERVER_OBJS))

client: dir $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/client $(patsubst %, build/%, $(CLIENT_OBJS))

$(CLIENT_OBJS): dir
	@mkdir -p $(BUILD_DIR)/$(@D)
	@$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ -c $*.cpp

$(SERVER_OBJS): dir
	@mkdir -p $(BUILD_DIR)/$(@D)
	@$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ -c $*.cpp

lib:
	$(CC) -c $(LIB_DIR)/*.hpp

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

dir:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

re: fclean all

.PHONY: all client server fclean re lib

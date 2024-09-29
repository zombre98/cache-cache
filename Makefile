CC = gcc
CFLAGS = -Wall -fPIC
LDFLAGS = -shared

SERVER = server
CLIENT = client
LIBRARY = libshared.so

SRCDIR_SERVER = server
SRCDIR_CLIENT = client
SRCDIR_SHARED = lib
OBJDIR = obj
BINDIR = bin

SERVER_SRC = $(wildcard $(SRCDIR_SERVER)/*.cpp)
CLIENT_SRC = $(wildcard $(SRCDIR_CLIENT)/*.cpp)
SHARED_SRC = $(wildcard $(SRCDIR_SHARED)/*.cpp)

SERVER_OBJS = $(patsubst $(SRCDIR_SERVER)/%.cpp, $(OBJDIR)/server_%.o, $(SERVER_SRC))
CLIENT_OBJS = $(patsubst $(SRCDIR_CLIENT)/%.cpp, $(OBJDIR)/client_%.o, $(CLIENT_SRC))
SHARED_OBJS = $(patsubst $(SRCDIR_SHARED)/%.cpp, $(OBJDIR)/shared_%.o, $(SHARED_SRC))

BLUE = \033[1;34m
GREEN = \033[1;32m
YELLOW = \033[1;33m
RESET = \033[0m

ARROW = ➜
CHECK = ✔

all: server client

server: $(BINDIR)/$(SERVER)

$(BINDIR)/$(SERVER): $(SERVER_OBJS) $(BINDIR)/$(LIBRARY)
	@mkdir -p $(BINDIR)
	@echo "$(BLUE)$(ARROW) Linking server executable...$(RESET)"
	$(CC) $(SERVER_OBJS) -L$(BINDIR) -lshared -o $@
	@echo "$(GREEN)$(CHECK) Server build complete!$(RESET)"

client: $(BINDIR)/$(CLIENT)

$(BINDIR)/$(CLIENT): $(CLIENT_OBJS) $(BINDIR)/$(LIBRARY)
	@mkdir -p $(BINDIR)
	@echo "$(BLUE)$(ARROW) Linking client executable...$(RESET)"
	$(CC) $(CLIENT_OBJS) -L$(BINDIR) -lshared -o $@
	@echo "$(GREEN)$(CHECK) Client build complete!$(RESET)"

$(BINDIR)/$(LIBRARY): $(SHARED_OBJS)
	@mkdir -p $(BINDIR)
	@echo "$(YELLOW)$(ARROW) Building shared library...$(RESET)"
	$(CC) $(LDFLAGS) -o $(BINDIR)/$(LIBRARY) $(SHARED_OBJS)
	@echo "$(GREEN)$(CHECK) Shared library build complete!$(RESET)"

$(OBJDIR)/server_%.o: $(SRCDIR_SERVER)/%.cpp $(SRCDIR_SHARED)/lib.hpp
	@mkdir -p $(OBJDIR)
	@echo "$(BLUE)$(ARROW) Compiling server source: $<$(RESET)"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/client_%.o: $(SRCDIR_CLIENT)/%.cpp $(SRCDIR_SHARED)/lib.hpp
	@mkdir -p $(OBJDIR)
	@echo "$(BLUE)$(ARROW) Compiling client source: $<$(RESET)"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/shared_%.o: $(SRCDIR_SHARED)/%.cpp $(SRCDIR_SHARED)/lib.hpp
	@mkdir -p $(OBJDIR)
	@echo "$(YELLOW)$(ARROW) Compiling shared source: $<$(RESET)"
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "$(YELLOW)$(ARROW) Cleaning up...$(RESET)"
	rm -rf $(OBJDIR) $(BINDIR)
	@echo "$(GREEN)$(CHECK) Clean complete!$(RESET)"

distclean: clean

.PHONY: all clean distclean server client


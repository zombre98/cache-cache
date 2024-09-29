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

all: server client

server: $(BINDIR)/$(SERVER)

$(BINDIR)/$(SERVER): $(SERVER_OBJS) $(BINDIR)/$(LIBRARY)
	@mkdir -p $(BINDIR)
	$(CC) $(SERVER_OBJS) -L$(BINDIR) -lshared -o $@

client: $(BINDIR)/$(CLIENT)

$(BINDIR)/$(CLIENT): $(CLIENT_OBJS) $(BINDIR)/$(LIBRARY)
	@mkdir -p $(BINDIR)
	$(CC) $(CLIENT_OBJS) -L$(BINDIR) -lshared -o $@

$(BINDIR)/$(LIBRARY): $(SHARED_OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(LDFLAGS) -o $(BINDIR)/$(LIBRARY) $(SHARED_OBJS)

$(OBJDIR)/server_%.o: $(SRCDIR_SERVER)/%.cpp $(SRCDIR_SHARED)/logging.hpp
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/client_%.o: $(SRCDIR_CLIENT)/%.cpp $(SRCDIR_SHARED)/logging.hpp
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/shared_%.o: $(SRCDIR_SHARED)/%.cpp $(SRCDIR_SHARED)/logging.hpp
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)

distclean: clean

.PHONY: all clean distclean


CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -lm
INCLUDES = -I./include
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# 源文件
COMMON_SRC = $(SRC_DIR)/common.c
SERVER_SRC = $(SRC_DIR)/server.c
CLIENT_SRC = $(SRC_DIR)/client.c

# 目标文件
COMMON_OBJ = $(OBJ_DIR)/common.o
SERVER_OBJ = $(OBJ_DIR)/server.o
CLIENT_OBJ = $(OBJ_DIR)/client.o

# 可执行文件
SERVER_BIN = $(BIN_DIR)/udp_server
CLIENT_BIN = $(BIN_DIR)/udp_client

.PHONY: all clean directories

all: directories $(SERVER_BIN) $(CLIENT_BIN)

directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SERVER_BIN): $(SERVER_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

install: all
	@echo "Installing binaries..."
	@mkdir -p /usr/local/bin
	@cp $(SERVER_BIN) /usr/local/bin/
	@cp $(CLIENT_BIN) /usr/local/bin/
	@echo "Installation complete!"

uninstall:
	@echo "Uninstalling binaries..."
	@rm -f /usr/local/bin/udp_server
	@rm -f /usr/local/bin/udp_client
	@echo "Uninstallation complete!"


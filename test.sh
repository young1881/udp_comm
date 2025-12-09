#!/bin/bash

# UDP通信测试脚本
# 使用方法：
#   ./test.sh server    # 启动服务器
#   ./test.sh client <server_ip> [packet_count] [packet_size]  # 启动客户端测试

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"

if [ ! -d "$BIN_DIR" ] || [ ! -f "$BIN_DIR/udp_server" ] || [ ! -f "$BIN_DIR/udp_client" ]; then
    echo "Error: Binaries not found. Please run 'make' first."
    exit 1
fi

case "$1" in
    server)
        echo "Starting UDP Server..."
        echo "Press Ctrl+C to stop"
        echo ""
        "$BIN_DIR/udp_server" -p 8888 -t
        ;;
    client)
        if [ -z "$2" ]; then
            echo "Usage: $0 client <server_ip> [packet_count] [packet_size]"
            echo "Example: $0 client 192.168.1.100 1000 1024"
            exit 1
        fi
        
        SERVER_IP="$2"
        PACKET_COUNT="${3:-1000}"
        PACKET_SIZE="${4:-1024}"
        
        echo "Starting UDP Client..."
        echo "Server IP: $SERVER_IP"
        echo "Packet count: $PACKET_COUNT"
        echo "Packet size: $PACKET_SIZE bytes"
        echo ""
        
        "$BIN_DIR/udp_client" -i "$SERVER_IP" -p 8888 -t -n "$PACKET_COUNT" -s "$PACKET_SIZE"
        ;;
    *)
        echo "Usage: $0 {server|client} [options]"
        echo ""
        echo "Examples:"
        echo "  $0 server                    # Start server on port 8888"
        echo "  $0 client 192.168.1.100     # Start client with default test (1000 packets, 1024 bytes)"
        echo "  $0 client 192.168.1.100 5000 2048  # Start client with custom test"
        exit 1
        ;;
esac


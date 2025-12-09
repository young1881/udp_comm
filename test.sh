#!/bin/bash

# UDP通信测试脚本
# 使用方法：
#   ./test.sh server    # 启动服务器（从TC3接收UDP报文）
#   ./test.sh client <tc3_ip> [packet_count] [packet_size]  # 启动客户端测试（发送UDP报文）

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"

if [ ! -d "$BIN_DIR" ] || [ ! -f "$BIN_DIR/udp_server" ] || [ ! -f "$BIN_DIR/udp_client" ]; then
    echo "Error: Binaries not found. Please run 'make' first."
    exit 1
fi

case "$1" in
    server)
        echo "Starting UDP Server (receiving from TC3)..."
        echo "Press Ctrl+C to stop"
        echo ""
        "$BIN_DIR/udp_server" -i 0.0.0.0 -p 8888 -t
        ;;
    client)
        if [ -z "$2" ]; then
            echo "Usage: $0 client <tc3_ip> [packet_count] [packet_size]"
            echo "Example: $0 client 192.168.1.100 1000 0"
            exit 1
        fi
        
        TC3_IP="$2"
        PACKET_COUNT="${3:-1000}"
        PACKET_SIZE="${4:-0}"
        
        echo "Starting UDP Client (sending to TC3)..."
        echo "TC3 IP: $TC3_IP"
        echo "Packet count: $PACKET_COUNT"
        echo "Packet size: $PACKET_SIZE bytes (0 = max UDP size)"
        echo ""
        
        "$BIN_DIR/udp_client" -i "$TC3_IP" -p 8888 -t -n "$PACKET_COUNT" -s "$PACKET_SIZE"
        ;;
    *)
        echo "Usage: $0 {server|client} [options]"
        echo ""
        echo "Examples:"
        echo "  $0 server                  # Start server on port 8888"
        echo "  $0 client 192.168.1.100      # Start client with default test (1000 packets, max size)"
        echo "  $0 client 192.168.1.100 5000 2048  # Start client with custom test"
        exit 1
        ;;
esac


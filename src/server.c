#include "../include/common.h"

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;  // 避免未使用参数警告
    running = 0;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[MAX_BUFFER_SIZE];
    int port = DEFAULT_PORT;
    const char *bind_ip = DEFAULT_SERVER_IP;
    int perf_test_mode = 0;
    stats_t stats = {0};
    uint32_t expected_seq = 0;
    
    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "hp:i:t")) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'p':
                port = atoi(optarg);
                break;
            case 'i':
                bind_ip = optarg;
                break;
            case 't':
                perf_test_mode = 1;
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 创建socket
    sockfd = create_udp_socket();
    if (sockfd < 0) {
        return 1;
    }
    
    // 绑定socket
    if (bind_socket(sockfd, bind_ip, port) < 0) {
        close(sockfd);
        return 1;
    }
    
    printf("UDP Server started on %s:%d\n", bind_ip, port);
    printf("Waiting for UDP packets from TC3...\n");
    if (perf_test_mode) {
        printf("Performance test mode: Receiving packets only (no echo)\n");
    } else {
        printf("Interactive mode: Receiving packets only (no echo)\n");
    }
    printf("Press Ctrl+C to stop\n\n");
    
    gettimeofday(&stats.start_time, NULL);
    
    // 主循环：只接收数据
    while (running) {
        ssize_t recv_len = recvfrom(sockfd, buffer, MAX_BUFFER_SIZE, 0,
                                    (struct sockaddr *)&client_addr, &client_len);
        
        if (recv_len < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom failed");
            continue;
        }
        
        stats.packets_received++;
        stats.bytes_received += recv_len;
        
        // 打印接收信息
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        
        // 如果是性能测试模式
        if (perf_test_mode && recv_len >= (ssize_t)sizeof(perf_packet_t)) {
            perf_packet_t *pkt = (perf_packet_t *)buffer;
            
            // 丢包检测：通过序列号判断
            if (pkt->seq_num == expected_seq) {
                expected_seq++;
            } else if (pkt->seq_num > expected_seq) {
                stats.packets_lost += (pkt->seq_num - expected_seq);
                expected_seq = pkt->seq_num + 1;
            }
            
            // 计算延迟（从发送时间戳到接收时间的延迟）
            struct timeval recv_time;
            gettimeofday(&recv_time, NULL);
            double recv_time_ms = recv_time.tv_sec * 1000.0 + recv_time.tv_usec / 1000.0;
            double send_time_ms = pkt->timestamp_sec * 1000.0 + pkt->timestamp_usec / 1000.0;
            double latency_ms = recv_time_ms - send_time_ms;
            
            if (latency_ms > 0) {
                if (stats.min_latency_ms == 0 || latency_ms < stats.min_latency_ms) {
                    stats.min_latency_ms = latency_ms;
                }
                if (latency_ms > stats.max_latency_ms) {
                    stats.max_latency_ms = latency_ms;
                }
                stats.total_latency_ms += latency_ms;
                stats.avg_latency_ms = stats.total_latency_ms / stats.packets_received;
            }
            
            // 性能测试模式下，每100个包显示一次进度
            if (stats.packets_received % 100 == 0) {
                printf("[RECV] From %s:%d, Packet #%u, Size: %zd bytes, "
                       "Loss: %lu, Avg Latency: %.3f ms\n", 
                       client_ip, ntohs(client_addr.sin_port), pkt->seq_num, recv_len,
                       stats.packets_lost, stats.avg_latency_ms);
            }
        } else {
            // 交互模式或非性能测试包：显示每次接收
            printf("[RECV] From %s:%d, Size: %zd bytes\n", 
                   client_ip, ntohs(client_addr.sin_port), recv_len);
        }
    }
    
    gettimeofday(&stats.end_time, NULL);
    
    printf("\nServer shutting down...\n");
    print_stats(&stats);
    
    close(sockfd);
    return 0;
}


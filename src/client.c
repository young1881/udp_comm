#include "../include/common.h"

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;  // 避免未使用参数警告
    running = 0;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[MAX_BUFFER_SIZE];
    const char *server_ip = DEFAULT_CLIENT_IP;
    int port = DEFAULT_PORT;
    int perf_test_mode = 0;
    int test_packet_count = 1000;
    int packet_size = 0;  // 0表示使用最大UDP包大小
    int iterations = 1;   // 迭代轮数，默认为1
    stats_t stats = {0};
    uint32_t seq_num = 0;
    
    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "hp:i:tn:s:r:")) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'p':
                port = atoi(optarg);
                break;
            case 'i':
                server_ip = optarg;
                break;
            case 't':
                perf_test_mode = 1;
                break;
            case 'n':
                test_packet_count = atoi(optarg);
                break;
            case 's':
                packet_size = atoi(optarg);
                if (packet_size < 0) {
                    packet_size = 0;  // 0表示使用最大UDP包大小
                } else if (packet_size > (int)(MAX_BUFFER_SIZE - sizeof(perf_packet_t))) {
                    packet_size = (int)(MAX_BUFFER_SIZE - sizeof(perf_packet_t));
                }
                break;
            case 'r':
                iterations = atoi(optarg);
                if (iterations < 1) {
                    iterations = 1;
                }
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // 检查必需参数
    if (!server_ip) {
        fprintf(stderr, "Error: Server IP address required (use -i option)\n");
        fprintf(stderr, "Use -h for help\n");
        return 1;
    }
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 创建socket
    sockfd = create_udp_socket();
    if (sockfd < 0) {
        return 1;
    }
    
    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_aton(server_ip, &server_addr.sin_addr) == 0) {
        fprintf(stderr, "Invalid server IP address: %s\n", server_ip);
        close(sockfd);
        return 1;
    }
    
    if (perf_test_mode) {
        // 性能测试模式：发送数据包到TC3
        // 如果packet_size为0或未指定，使用最大UDP包大小
        if (packet_size <= 0) {
            packet_size = MAX_BUFFER_SIZE - sizeof(perf_packet_t);
        }
        
        printf("UDP Client sending to %s:%d\n", server_ip, port);
        printf("Performance test mode: Send only, no echo expected\n");
        printf("Packet count per iteration: %d\n", test_packet_count);
        printf("Packet size: %d bytes\n", packet_size);
        printf("Number of iterations: %d\n", iterations);
        printf("Press Ctrl+C to stop\n\n");
        
        // 分配多轮测试统计结构
        multi_iteration_stats_t multi_stats = {0};
        multi_stats.iteration_count = iterations;
        multi_stats.avg_latencies = calloc(iterations, sizeof(double));
        multi_stats.throughputs = calloc(iterations, sizeof(double));
        multi_stats.packet_loss_rates = calloc(iterations, sizeof(double));
        multi_stats.durations = calloc(iterations, sizeof(double));
        multi_stats.packets_sent_total = calloc(iterations, sizeof(uint64_t));
        multi_stats.packets_received_total = calloc(iterations, sizeof(uint64_t));
        
        if (!multi_stats.avg_latencies || !multi_stats.throughputs || 
            !multi_stats.packet_loss_rates || !multi_stats.durations ||
            !multi_stats.packets_sent_total || !multi_stats.packets_received_total) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            close(sockfd);
            return 1;
        }
        
        // 执行多轮测试
        for (int iter = 0; iter < iterations && running; iter++) {
            printf("\n========== 第 %d/%d 轮测试 ==========\n", iter + 1, iterations);
            
            // 重置统计信息
            memset(&stats, 0, sizeof(stats));
            seq_num = 0;
            gettimeofday(&stats.start_time, NULL);
            
            // 发送数据包
            for (int i = 0; i < test_packet_count && running; i++) {
                // 准备测试数据包
                int pkt_size = sizeof(perf_packet_t) + packet_size;
                perf_packet_t *pkt = (perf_packet_t *)buffer;
                
                pkt->seq_num = seq_num++;
                struct timeval tv;
                gettimeofday(&tv, NULL);
                pkt->timestamp_sec = tv.tv_sec;
                pkt->timestamp_usec = tv.tv_usec;
                pkt->data_len = packet_size;
                
                // 填充测试数据
                for (int j = 0; j < packet_size; j++) {
                    pkt->data[j] = (char)(j % 256);
                }
                
                // 发送数据包
                ssize_t send_len = sendto(sockfd, buffer, pkt_size, 0,
                                         (struct sockaddr *)&server_addr, sizeof(server_addr));
                
                if (send_len < 0) {
                    perror("sendto failed");
                    continue;
                }
                
                stats.packets_sent++;
                stats.bytes_sent += send_len;
                
                // 显示进度（每100个包显示一次）
                if ((i + 1) % 100 == 0 || i == test_packet_count - 1) {
                    printf("Progress: %d/%d packets (%.1f%%)\n",
                           i + 1, test_packet_count, (i + 1) * 100.0 / test_packet_count);
                }
                
                // 控制发送速率（可选，避免过快发送）
                usleep(1000);  // 1ms延迟
            }
            
            gettimeofday(&stats.end_time, NULL);
            
            // 计算本轮统计数据
            struct timeval elapsed;
            timersub(&stats.end_time, &stats.start_time, &elapsed);
            double elapsed_sec = elapsed.tv_sec + elapsed.tv_usec / 1000000.0;
            
            multi_stats.durations[iter] = elapsed_sec;
            multi_stats.packets_sent_total[iter] = stats.packets_sent;
            multi_stats.packets_received_total[iter] = stats.packets_received;
            
            // 计算吞吐量（基于发送的数据）
            if (elapsed_sec > 0) {
                multi_stats.throughputs[iter] = (stats.bytes_sent * 8.0) / elapsed_sec / 1000000.0;
            }
            
            // 计算丢包率（这里假设所有发送的包都应该被接收，实际需要TC3端反馈）
            if (stats.packets_sent > 0) {
                multi_stats.packet_loss_rates[iter] = 
                    (double)stats.packets_lost / stats.packets_sent * 100.0;
            }
            
            multi_stats.avg_latencies[iter] = stats.avg_latency_ms;
            
            // 显示本轮结果
            printf("\n--- 第 %d 轮结果 ---\n", iter + 1);
            printf("耗时: %.3f 秒\n", elapsed_sec);
            printf("发送包数: %lu\n", stats.packets_sent);
            printf("发送字节数: %.2f MB\n", stats.bytes_sent / 1024.0 / 1024.0);
            printf("吞吐量: %.2f Mbps\n", multi_stats.throughputs[iter]);
            
            // 每轮之间稍作停顿
            if (iter < iterations - 1) {
                printf("\n等待1秒后开始下一轮...\n");
                sleep(1);
            }
        }
        
        // 打印多轮测试统计结果
        if (iterations > 1) {
            print_multi_iteration_stats(&multi_stats);
        } else {
            // 单轮测试，直接打印统计信息
            print_stats(&stats);
        }
        
        // 释放多轮测试统计内存
        free_multi_iteration_stats(&multi_stats);
        
        printf("\nPerformance test completed.\n");
        
    } else {
        // 交互模式：发送用户输入的数据到TC3
        printf("UDP Client connecting to %s:%d\n", server_ip, port);
        printf("Enter message to send (or 'quit' to exit, 'max' for maximum UDP packet):\n");
        printf("Press Ctrl+C to stop\n\n");
        
        gettimeofday(&stats.start_time, NULL);
        
        while (running) {
            // 从标准输入读取数据
            printf("> ");
            fflush(stdout);
            
            if (!fgets(buffer, MAX_BUFFER_SIZE, stdin)) {
                break;
            }
            
            // 移除换行符
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
                buffer[len - 1] = '\0';
                len--;
            }
            
            if (len == 0) {
                continue;
            }
            
            // 检查退出命令
            if (strcmp(buffer, "quit") == 0 || strcmp(buffer, "exit") == 0) {
                break;
            }
            
            // 检查是否发送最大UDP包
            if (strcmp(buffer, "max") == 0) {
                // 准备最大UDP包
                len = MAX_BUFFER_SIZE;
                // 填充测试数据
                for (size_t j = 0; j < len; j++) {
                    buffer[j] = (char)(j % 256);
                }
                printf("Sending maximum UDP packet (%zu bytes)...\n", len);
            }
            
            // 发送数据
            ssize_t send_len = sendto(sockfd, buffer, len, 0,
                                     (struct sockaddr *)&server_addr, sizeof(server_addr));
            
            if (send_len < 0) {
                perror("sendto failed");
                continue;
            }
            
            stats.packets_sent++;
            stats.bytes_sent += send_len;
            printf("[SEND] Size: %zd bytes\n\n", send_len);
        }
        
        gettimeofday(&stats.end_time, NULL);
        
        printf("\nClient shutting down...\n");
        print_stats(&stats);
    }
    
    close(sockfd);
    return 0;
}

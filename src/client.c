#include "../include/common.h"
#include <sys/select.h>

static volatile int running = 1;

// 发送时间戳映射结构（用于计算RTT）
typedef struct {
    uint32_t seq_num;
    struct timeval send_time;
} send_time_entry_t;

static send_time_entry_t *send_time_map = NULL;
static uint32_t send_time_map_size = 0;
static uint32_t send_time_map_capacity = 0;

void signal_handler(int sig) {
    (void)sig;  // 避免未使用参数警告
    running = 0;
}

// 添加发送时间戳
static void add_send_time(uint32_t seq_num, struct timeval *send_time) {
    // 如果映射表满了，扩大容量
    if (send_time_map_size >= send_time_map_capacity) {
        uint32_t new_capacity = send_time_map_capacity == 0 ? 1024 : send_time_map_capacity * 2;
        send_time_entry_t *new_map = realloc(send_time_map, new_capacity * sizeof(send_time_entry_t));
        if (!new_map) {
            fprintf(stderr, "Warning: Failed to expand send time map\n");
            return;
        }
        send_time_map = new_map;
        send_time_map_capacity = new_capacity;
    }
    
    send_time_map[send_time_map_size].seq_num = seq_num;
    send_time_map[send_time_map_size].send_time = *send_time;
    send_time_map_size++;
}

// 查找并移除发送时间戳（计算RTT后删除）
static int find_and_remove_send_time(uint32_t seq_num, struct timeval *send_time) {
    for (uint32_t i = 0; i < send_time_map_size; i++) {
        if (send_time_map[i].seq_num == seq_num) {
            *send_time = send_time_map[i].send_time;
            // 移除元素（将最后一个元素移到当前位置）
            if (i < send_time_map_size - 1) {
                send_time_map[i] = send_time_map[send_time_map_size - 1];
            }
            send_time_map_size--;
            return 1;
        }
    }
    return 0;
}

// 清理发送时间映射表
static void clear_send_time_map(void) {
    send_time_map_size = 0;
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
    
    // 绑定到任意本地端口（让系统自动分配）
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;  // 0表示让系统自动分配端口
    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }
    
    // 获取绑定的本地端口
    socklen_t len = sizeof(local_addr);
    if (getsockname(sockfd, (struct sockaddr *)&local_addr, &len) == 0) {
        printf("Client bound to local port: %d\n", ntohs(local_addr.sin_port));
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
        printf("Performance test mode: Send and receive echo for RTT measurement\n");
        printf("Packet count per iteration: %d\n", test_packet_count);
        printf("Packet size: %d bytes\n", packet_size);
        printf("Number of iterations: %d\n", iterations);
        printf("\n[INFO] Waiting for echo responses from TC3...\n");
        printf("[INFO] If no responses received, TC3 may not be configured for echo mode\n");
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
            clear_send_time_map();
            gettimeofday(&stats.start_time, NULL);
            
            uint32_t packets_sent_this_round = 0;
            uint32_t expected_seq = 0;
            
            // 发送所有数据包
            for (int i = 0; i < test_packet_count && running; i++) {
                // 准备测试数据包
                int pkt_size = sizeof(perf_packet_t) + packet_size;
                perf_packet_t *pkt = (perf_packet_t *)buffer;
                
                pkt->seq_num = seq_num;
                struct timeval tv;
                gettimeofday(&tv, NULL);
                pkt->timestamp_sec = tv.tv_sec;
                pkt->timestamp_usec = tv.tv_usec;
                pkt->data_len = packet_size;
                
                // 记录发送时间
                add_send_time(seq_num, &tv);
                
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
                packets_sent_this_round++;
                seq_num++;
                
                // 尝试接收响应（非阻塞）
                fd_set read_fds;
                struct timeval timeout;
                FD_ZERO(&read_fds);
                FD_SET(sockfd, &read_fds);
                timeout.tv_sec = 0;
                timeout.tv_usec = 10000;  // 10ms超时
                
                int select_result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
                
                // 调试信息：前几个包显示select结果
                if (i < 5 && select_result == 0) {
                    printf("[DEBUG] No data available for packet #%u (select timeout)\n", seq_num - 1);
                }
                
                while (select_result > 0) {
                    struct sockaddr_in recv_addr;
                    socklen_t recv_addr_len = sizeof(recv_addr);
                    ssize_t recv_len = recvfrom(sockfd, buffer, MAX_BUFFER_SIZE, 0,
                                               (struct sockaddr *)&recv_addr, &recv_addr_len);
                    
                    if (recv_len < 0) {
                        if (errno != EINTR) {
                            perror("recvfrom failed");
                        }
                        break;
                    }
                    
                    // 获取接收方的IP地址字符串
                    char recv_ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &recv_addr.sin_addr, recv_ip_str, INET_ADDRSTRLEN);
                    char server_ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &server_addr.sin_addr, server_ip_str, INET_ADDRSTRLEN);
                    
                    // 显示所有接收到的数据包（调试用）
                    printf("[DEBUG] Received UDP packet: size=%zd bytes, from %s:%d (expected from %s)\n",
                           recv_len, recv_ip_str, ntohs(recv_addr.sin_port), server_ip_str);
                    
                    // 验证是否来自目标服务器（只检查IP地址，不检查端口）
                    // 因为TC3可能从不同端口回送数据
                    if (recv_addr.sin_addr.s_addr == server_addr.sin_addr.s_addr) {
                        if (recv_len >= (ssize_t)sizeof(perf_packet_t)) {
                            perf_packet_t *recv_pkt = (perf_packet_t *)buffer;
                            
                            // 计算RTT
                            struct timeval recv_time, send_time;
                            gettimeofday(&recv_time, NULL);
                            
                            if (find_and_remove_send_time(recv_pkt->seq_num, &send_time)) {
                                struct timeval rtt;
                                timersub(&recv_time, &send_time, &rtt);
                                double rtt_ms = rtt.tv_sec * 1000.0 + rtt.tv_usec / 1000.0;
                                
                                if (rtt_ms > 0) {
                                    if (stats.min_latency_ms == 0 || rtt_ms < stats.min_latency_ms) {
                                        stats.min_latency_ms = rtt_ms;
                                    }
                                    if (rtt_ms > stats.max_latency_ms) {
                                        stats.max_latency_ms = rtt_ms;
                                    }
                                    stats.total_latency_ms += rtt_ms;
                                    stats.packets_received++;
                                    stats.bytes_received += recv_len;
                                    
                                    // 检测丢包
                                    if (recv_pkt->seq_num == expected_seq) {
                                        expected_seq++;
                                    } else if (recv_pkt->seq_num > expected_seq) {
                                        stats.packets_lost += (recv_pkt->seq_num - expected_seq);
                                        expected_seq = recv_pkt->seq_num + 1;
                                    }
                                    
                                    // 每100个包显示一次接收信息
                                    if (stats.packets_received % 100 == 0) {
                                        printf("[RECV] Packet #%u from %s:%d, RTT=%.4f ms\n",
                                               recv_pkt->seq_num, recv_ip_str, ntohs(recv_addr.sin_port), rtt_ms);
                                    }
                                }
                            } else {
                                // 接收到未知序列号的包
                                printf("[DEBUG] Received packet with unknown seq_num=%u from %s:%d (size=%zd)\n",
                                       recv_pkt->seq_num, recv_ip_str, ntohs(recv_addr.sin_port), recv_len);
                                printf("[DEBUG] This packet may be from a previous test or invalid\n");
                            }
                        } else {
                            // 接收到非性能测试包
                            printf("[DEBUG] Received non-perf packet from %s:%d (size=%zd, expected>=%zu)\n",
                                   recv_ip_str, ntohs(recv_addr.sin_port), recv_len, sizeof(perf_packet_t));
                            printf("[DEBUG] Packet size too small, may not be a perf_packet_t structure\n");
                        }
                    } else {
                        // 接收到来自非目标IP的包（可能是其他来源）
                        printf("[DEBUG] Received packet from unexpected source %s:%d (expected %s)\n",
                               recv_ip_str, ntohs(recv_addr.sin_port), server_ip_str);
                        printf("[DEBUG] This packet is being ignored (IP address mismatch)\n");
                    }
                    
                    // 继续检查是否还有数据
                    FD_ZERO(&read_fds);
                    FD_SET(sockfd, &read_fds);
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 0;
                    select_result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
                }
                
                // 显示进度（每100个包显示一次）
                if ((i + 1) % 100 == 0 || i == test_packet_count - 1) {
                    printf("Progress: %d/%d sent, %lu received (%.1f%%)\n",
                           i + 1, test_packet_count, stats.packets_received,
                           (i + 1) * 100.0 / test_packet_count);
                }
                
                // 控制发送速率（可选，避免过快发送）
                usleep(1000);  // 1ms延迟
            }
            
            // 发送完成后，等待一段时间接收剩余的响应
            printf("\n[INFO] Sending complete. Waiting 2 seconds for remaining responses...\n");
            struct timeval wait_start;
            gettimeofday(&wait_start, NULL);
            double wait_duration = 2.0;  // 等待2秒接收剩余响应
            
            while (running) {
                struct timeval now;
                gettimeofday(&now, NULL);
                double elapsed = (now.tv_sec - wait_start.tv_sec) + 
                                (now.tv_usec - wait_start.tv_usec) / 1000000.0;
                if (elapsed >= wait_duration) {
                    break;
                }
                
                fd_set read_fds;
                struct timeval timeout;
                FD_ZERO(&read_fds);
                FD_SET(sockfd, &read_fds);
                double remaining = wait_duration - elapsed;
                timeout.tv_sec = (long)remaining;
                timeout.tv_usec = (long)((remaining - timeout.tv_sec) * 1000000);
                
                if (select(sockfd + 1, &read_fds, NULL, NULL, &timeout) > 0) {
                    struct sockaddr_in recv_addr;
                    socklen_t recv_addr_len = sizeof(recv_addr);
                    ssize_t recv_len = recvfrom(sockfd, buffer, MAX_BUFFER_SIZE, 0,
                                               (struct sockaddr *)&recv_addr, &recv_addr_len);
                    
                    if (recv_len < 0) {
                        if (errno != EINTR) {
                            perror("recvfrom failed");
                        }
                        continue;
                    }
                    
                    // 只检查IP地址，不检查端口
                    if (recv_addr.sin_addr.s_addr == server_addr.sin_addr.s_addr) {
                        if (recv_len >= (ssize_t)sizeof(perf_packet_t)) {
                            perf_packet_t *recv_pkt = (perf_packet_t *)buffer;
                            
                            struct timeval recv_time, send_time;
                            gettimeofday(&recv_time, NULL);
                            
                            if (find_and_remove_send_time(recv_pkt->seq_num, &send_time)) {
                                struct timeval rtt;
                                timersub(&recv_time, &send_time, &rtt);
                                double rtt_ms = rtt.tv_sec * 1000.0 + rtt.tv_usec / 1000.0;
                                
                                if (rtt_ms > 0) {
                                    if (stats.min_latency_ms == 0 || rtt_ms < stats.min_latency_ms) {
                                        stats.min_latency_ms = rtt_ms;
                                    }
                                    if (rtt_ms > stats.max_latency_ms) {
                                        stats.max_latency_ms = rtt_ms;
                                    }
                                    stats.total_latency_ms += rtt_ms;
                                    stats.packets_received++;
                                    stats.bytes_received += recv_len;
                                    
                                    if (recv_pkt->seq_num == expected_seq) {
                                        expected_seq++;
                                    } else if (recv_pkt->seq_num > expected_seq) {
                                        stats.packets_lost += (recv_pkt->seq_num - expected_seq);
                                        expected_seq = recv_pkt->seq_num + 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // 计算剩余未响应的包
            if (send_time_map_size > 0) {
                stats.packets_lost += send_time_map_size;
                printf("[INFO] After waiting, %u packets still pending (no response received)\n", send_time_map_size);
            }
            
            if (stats.packets_received == 0) {
                printf("[WARNING] No response packets received from TC3!\n");
                printf("[WARNING] Possible reasons:\n");
                printf("  1. TC3 is not configured to echo/send back packets\n");
                printf("  2. Network/firewall blocking responses\n");
                printf("  3. TC3 is sending to wrong IP/port\n");
                printf("[INFO] Check TC3 side configuration and network connectivity\n");
            }
            
            // 计算平均延迟
            if (stats.packets_received > 0) {
                stats.avg_latency_ms = stats.total_latency_ms / stats.packets_received;
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
            
            // 计算丢包率
            if (stats.packets_sent > 0) {
                multi_stats.packet_loss_rates[iter] = 
                    (double)stats.packets_lost / stats.packets_sent * 100.0;
            }
            
            multi_stats.avg_latencies[iter] = stats.avg_latency_ms;
            
            // 显示本轮结果
            printf("\n--- 第 %d 轮结果 ---\n", iter + 1);
            printf("耗时: %.3f 秒\n", elapsed_sec);
            printf("发送包数: %lu\n", stats.packets_sent);
            printf("接收包数: %lu\n", stats.packets_received);
            printf("丢失包数: %lu\n", stats.packets_lost);
            printf("丢包率: %.2f%%\n", multi_stats.packet_loss_rates[iter]);
            if (stats.packets_received > 0) {
                printf("最小RTT: %.4f ms\n", stats.min_latency_ms);
                printf("最大RTT: %.4f ms\n", stats.max_latency_ms);
                printf("平均RTT: %.4f ms\n", stats.avg_latency_ms);
            }
            printf("发送字节数: %.2f MB\n", stats.bytes_sent / 1024.0 / 1024.0);
            printf("接收字节数: %.2f MB\n", stats.bytes_received / 1024.0 / 1024.0);
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
        
        // 释放发送时间映射表
        if (send_time_map) {
            free(send_time_map);
            send_time_map = NULL;
            send_time_map_size = 0;
            send_time_map_capacity = 0;
        }
        
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
    
    // 释放发送时间映射表
    if (send_time_map) {
        free(send_time_map);
        send_time_map = NULL;
    }
    
    close(sockfd);
    return 0;
}

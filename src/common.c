#include "../include/common.h"
#include <math.h>

// 获取当前时间（毫秒）
double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// 获取当前时间（微秒）
uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

// 创建UDP socket
int create_udp_socket(void) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // 设置socket选项：允许地址重用
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        return -1;
    }
    
    // 设置接收缓冲区大小
    int rcvbuf = 1024 * 1024;  // 1MB
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
        perror("setsockopt SO_RCVBUF failed");
    }
    
    // 设置发送缓冲区大小
    int sndbuf = 1024 * 1024;  // 1MB
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
        perror("setsockopt SO_SNDBUF failed");
    }
    
    return sockfd;
}

// 绑定socket到指定IP和端口
int bind_socket(int sockfd, const char *ip, int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_aton(ip, &addr.sin_addr) == 0) {
        fprintf(stderr, "Invalid IP address: %s\n", ip);
        return -1;
    }
    
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return -1;
    }
    
    return 0;
}

// 打印统计信息
void print_stats(stats_t *stats) {
    struct timeval elapsed;
    timersub(&stats->end_time, &stats->start_time, &elapsed);
    double elapsed_sec = elapsed.tv_sec + elapsed.tv_usec / 1000000.0;
    
    printf("\n========== 通信统计信息 ==========\n");
    printf("测试时长: %.3f 秒\n", elapsed_sec);
    printf("发送数据包数: %lu\n", stats->packets_sent);
    printf("接收数据包数: %lu\n", stats->packets_received);
    printf("丢失数据包数: %lu\n", stats->packets_lost);
    printf("发送字节数: %lu (%.2f MB)\n", 
           stats->bytes_sent, stats->bytes_sent / 1024.0 / 1024.0);
    printf("接收字节数: %lu (%.2f MB)\n", 
           stats->bytes_received, stats->bytes_received / 1024.0 / 1024.0);
    
    if (stats->packets_received > 0) {
        printf("丢包率: %.2f%%\n", 
               (double)stats->packets_lost / stats->packets_sent * 100.0);
        printf("平均吞吐量: %.2f Mbps\n", 
               (stats->bytes_received * 8.0) / elapsed_sec / 1000000.0);
    }
    
    if (stats->packets_received > 0 && stats->avg_latency_ms > 0) {
        printf("最小延迟: %.3f ms\n", stats->min_latency_ms);
        printf("最大延迟: %.3f ms\n", stats->max_latency_ms);
        printf("平均延迟: %.3f ms\n", stats->avg_latency_ms);
    }
    printf("===================================\n\n");
}

// 打印多轮迭代测试统计信息
void print_multi_iteration_stats(multi_iteration_stats_t *multi_stats) {
    if (!multi_stats || multi_stats->iteration_count == 0) {
        return;
    }
    
    printf("\n========== 多轮迭代测试统计 ==========\n");
    printf("迭代轮数: %d\n", multi_stats->iteration_count);
    
    // 计算平均值
    double avg_latency_sum = 0.0;
    double avg_throughput_sum = 0.0;
    double avg_loss_rate_sum = 0.0;
    double avg_duration_sum = 0.0;
    uint64_t total_packets_sent = 0;
    uint64_t total_packets_received = 0;
    
    for (int i = 0; i < multi_stats->iteration_count; i++) {
        avg_latency_sum += multi_stats->avg_latencies[i];
        avg_throughput_sum += multi_stats->throughputs[i];
        avg_loss_rate_sum += multi_stats->packet_loss_rates[i];
        avg_duration_sum += multi_stats->durations[i];
        total_packets_sent += multi_stats->packets_sent_total[i];
        total_packets_received += multi_stats->packets_received_total[i];
    }
    
    double avg_latency = avg_latency_sum / multi_stats->iteration_count;
    double avg_throughput = avg_throughput_sum / multi_stats->iteration_count;
    double avg_loss_rate = avg_loss_rate_sum / multi_stats->iteration_count;
    double avg_duration = avg_duration_sum / multi_stats->iteration_count;
    
    // 计算标准差
    double latency_stddev = 0.0;
    double throughput_stddev = 0.0;
    if (multi_stats->iteration_count > 1) {
        double latency_variance = 0.0;
        double throughput_variance = 0.0;
        for (int i = 0; i < multi_stats->iteration_count; i++) {
            double diff = multi_stats->avg_latencies[i] - avg_latency;
            latency_variance += diff * diff;
            diff = multi_stats->throughputs[i] - avg_throughput;
            throughput_variance += diff * diff;
        }
        latency_stddev = sqrt(latency_variance / multi_stats->iteration_count);
        throughput_stddev = sqrt(throughput_variance / multi_stats->iteration_count);
    }
    
    printf("\n--- 平均值 ---\n");
    printf("平均延迟: %.3f ms (标准差: %.3f ms)\n", avg_latency, latency_stddev);
    printf("平均吞吐量: %.2f Mbps (标准差: %.2f Mbps)\n", avg_throughput, throughput_stddev);
    printf("平均丢包率: %.2f%%\n", avg_loss_rate);
    printf("平均耗时: %.3f 秒\n", avg_duration);
    printf("总发送包数: %lu\n", total_packets_sent);
    printf("总接收包数: %lu\n", total_packets_received);
    
    printf("\n--- 每轮详细结果 ---\n");
    for (int i = 0; i < multi_stats->iteration_count; i++) {
        printf("第 %d 轮: 延迟=%.3f ms, 吞吐量=%.2f Mbps, 丢包率=%.2f%%, 耗时=%.3f 秒\n",
               i + 1,
               multi_stats->avg_latencies[i],
               multi_stats->throughputs[i],
               multi_stats->packet_loss_rates[i],
               multi_stats->durations[i]);
    }
    
    printf("=====================================\n\n");
}

// 释放多轮测试统计内存
void free_multi_iteration_stats(multi_iteration_stats_t *multi_stats) {
    if (!multi_stats) {
        return;
    }
    if (multi_stats->avg_latencies) free(multi_stats->avg_latencies);
    if (multi_stats->throughputs) free(multi_stats->throughputs);
    if (multi_stats->packet_loss_rates) free(multi_stats->packet_loss_rates);
    if (multi_stats->durations) free(multi_stats->durations);
    if (multi_stats->packets_sent_total) free(multi_stats->packets_sent_total);
    if (multi_stats->packets_received_total) free(multi_stats->packets_received_total);
    // 注意：不释放multi_stats本身，因为它可能是在栈上分配的
    memset(multi_stats, 0, sizeof(multi_iteration_stats_t));
}

// 打印使用说明
void print_usage(const char *program_name) {
    // 根据程序名称判断是server还是client
    int is_server = (strstr(program_name, "server") != NULL);
    
    if (is_server) {
        printf("Usage: %s [options]\n", program_name);
        printf("Description: Receive UDP packets from TC3 (alternative to udp_client -R)\n");
        printf("Note: All programs run on Orin machine, TC3 runs its own UDP program\n");
        printf("\n");
        printf("Options:\n");
        printf("  -h              Show this help message\n");
        printf("  -p <port>       Specify port (default: %d)\n", DEFAULT_PORT);
        printf("  -i <ip>         Specify bind IP address (default: %s)\n", DEFAULT_SERVER_IP);
        printf("  -t              Enable performance test mode\n");
        printf("\n");
        printf("Examples:\n");
        printf("  %s -p 8888\n", program_name);
        printf("  %s -i 0.0.0.0 -p 8888 -t\n", program_name);
    } else {
        printf("Usage: %s [options]\n", program_name);
        printf("Options:\n");
        printf("  -h              Show this help message\n");
        printf("  -p <port>       Specify server port (default: %d)\n", DEFAULT_PORT);
        printf("  -i <ip>         Specify server IP address (required)\n");
        printf("  -t              Enable performance test mode (send to TC3)\n");
        printf("  -R              Enable receive mode (receive from TC3)\n");
        printf("  -n <count>      Number of test packets (default: 1000)\n");
        printf("  -s <size>       Packet size in bytes (0 or not set = max UDP size, default: 0)\n");
        printf("  -r <iterations> Number of test iterations for averaging (default: 1)\n");
        printf("\n");
        printf("Examples:\n");
        printf("  Interactive send: %s -i 192.168.1.100 -p 8888\n", program_name);
        printf("  Receive mode:     %s -R -i 0.0.0.0 -p 8888\n", program_name);
        printf("  Send test:        %s -i 192.168.1.100 -p 8888 -t -n 1000 -s 0\n", program_name);
        printf("  Multi-iteration:  %s -i 192.168.1.100 -p 8888 -t -n 1000 -s 0 -r 10\n", program_name);
    }
}


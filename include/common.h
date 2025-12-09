#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/time.h>

#define MAX_BUFFER_SIZE 65507  // UDP最大数据包大小
#define DEFAULT_PORT 8888
#define DEFAULT_SERVER_IP "0.0.0.0"
#define DEFAULT_CLIENT_IP "192.168.1.100"  // TC3开发板IP

// 数据包类型
typedef enum {
    PKT_TYPE_DATA = 0x01,
    PKT_TYPE_ECHO = 0x02,
    PKT_TYPE_PERF_TEST = 0x03,
    PKT_TYPE_ACK = 0x04
} packet_type_t;

// 性能测试数据包结构
typedef struct {
    uint32_t seq_num;          // 序列号
    uint32_t timestamp_sec;     // 时间戳（秒）
    uint32_t timestamp_usec;    // 时间戳（微秒）
    uint32_t data_len;          // 数据长度
    char data[0];               // 数据内容
} perf_packet_t;

// 统计信息结构
typedef struct {
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t packets_lost;
    double min_latency_ms;
    double max_latency_ms;
    double avg_latency_ms;
    double total_latency_ms;
    struct timeval start_time;
    struct timeval end_time;
} stats_t;

// 多轮测试统计结构（用于求平均值）
typedef struct {
    int iteration_count;           // 迭代轮数
    double *avg_latencies;         // 每轮的平均延迟
    double *throughputs;           // 每轮的吞吐量 (Mbps)
    double *packet_loss_rates;     // 每轮的丢包率 (%)
    double *durations;             // 每轮的耗时 (秒)
    uint64_t *packets_sent_total;  // 每轮的发送包数
    uint64_t *packets_received_total; // 每轮的接收包数
} multi_iteration_stats_t;

// 函数声明
void print_stats(stats_t *stats);
void print_multi_iteration_stats(multi_iteration_stats_t *multi_stats);
void free_multi_iteration_stats(multi_iteration_stats_t *multi_stats);
double get_time_ms(void);
uint64_t get_time_us(void);
int create_udp_socket(void);
int bind_socket(int sockfd, const char *ip, int port);
void print_usage(const char *program_name);

#endif // COMMON_H


#ifndef KSOCKET_H
#define KSOCKET_H

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#define T 1              // Timeout time in seconds
#define P 0.05           // Probability of packet loss
#define N 10             // Maximum number of sockets
#define BUFLEN 512       // Maximum size of a message
#define SOCK_KTP 1234
#define MAX_SEQ_SIZE 256 // 0 to 255 possible sequence numbers

// Semaphore operations
#define waitsem(s) semop(s, &pop, 1)
#define signalsem(s) semop(s, &vop, 1)

// States of socket
#define FREE 0
#define CREATE_PENDING 1
#define CREATED 2
#define BIND_PENDING 3
#define READY 4
#define CLOSE_PENDING 5
#define NO_SPACE 6
#define HAS_SPACE 7

// Error codes
#define ENOSPACE 1001
#define ENOTBOUND 1002
#define ENOMESSAGE 1003

// Data packet structure
typedef struct{
    int seq;
    int ack;
    int size;
    char data[BUFLEN];
}packet_t;

typedef struct{
    int buf_flag[MAX_SEQ_SIZE];
    int base_num;               // for window
    int next_num;
    int to_be_ack;              // current window size
    int window_size;            // maximum window size
}window_t;

typedef struct{
    packet_t buf[N];
    int buf_cnt;                // new packets that got added after sleep for T/2
    int read_ptr;
    int next_ptr;
    int write_ptr;
}buffer_t;

// Socket structure
typedef struct{
    int state;
    int sockfd;
    int pid;
    struct sockaddr_in src_addr;
    struct sockaddr_in dest_addr;
    time_t last_sent;
    buffer_t recv_buffer;
    window_t recv_window;
    buffer_t send_buffer;
    window_t send_window;
    int msg_count;
    int trans_count;
    int flag;
}ksocket_t;

void init_semaphore();
void deinit_semaphore();
int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, const struct sockaddr *src_addr, const struct sockaddr *dest_addr);
int k_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
int k_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int k_close(int sockfd);
void k_perror(const char *msg);

#endif
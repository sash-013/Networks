#include "ksocket.h"

int main(){
    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if(sockfd < 0){
        k_perror("socket");
        return -1;
    }
    printf("Socket created successfully\n");
    int src_port, dest_port;
    printf("Enter SRC port: ");
    scanf("%d", &src_port);
    printf("Enter DEST port: ");
    scanf("%d", &dest_port);
    struct sockaddr_in src_addr, dest_addr;
    src_addr.sin_family = AF_INET;
    src_addr.sin_port = htons(src_port);
    src_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(k_bind(sockfd, (struct sockaddr*)&src_addr, (struct sockaddr*)&dest_addr) < 0){
        k_perror("bind");
        return -1;
    }
    printf("Socket bound successfully\n");
    sleep(5);
    char filename[BUFLEN];
    sprintf(filename, "user2_%d.txt", src_port);
    FILE *fp = fopen(filename, "w");
    if(fp == NULL){
        perror("fopen");
        return -1;
    }
    char buf[BUFLEN];
    socklen_t len = sizeof(dest_addr); 
    int ret, cnt = 1;
    while(1){
        while((ret=k_recvfrom(sockfd, buf, BUFLEN, 0, (struct sockaddr*)&dest_addr, &len))==-1);
        printf("message received (len = %d) %d\n", ret, cnt++);
        if(strcmp(buf, "DONE")==0) break;
        fwrite(buf, sizeof(char), ret, fp);
        fflush(fp);
    }
    fclose(fp);
    printf("File received successfully\n");
    sleep(100);
    k_close(sockfd);
    return 0;
}
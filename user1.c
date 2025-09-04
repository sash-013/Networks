#include "ksocket.h"

int main(){
    int src_port, dest_port;
    printf("Enter SRC port: ");
    scanf("%d", &src_port);
    printf("Enter DEST port: ");
    scanf("%d", &dest_port);
    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if(sockfd < 0){
        k_perror("socket");
        return -1;
    }
    printf("Socket created successfully\n");

    struct sockaddr_in src_addr, dest_addr;
    src_addr.sin_family = AF_INET;
    src_addr.sin_port = htons(src_port);
    src_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(k_bind(sockfd, (struct sockaddr*)&src_addr, (struct sockaddr *)&dest_addr) < 0){
        k_perror("bind");
        return -1;
    }
    printf("Socket bound successfully\n");
    sleep(5);
    
    FILE* fp = fopen("user1.txt", "r");
    if(fp == NULL){
        perror("fopen");
        return -1;
    }

    char buf[BUFLEN];
    int cnt=1;
    int ret;
    while(1){
        int n=fread(buf, 1, BUFLEN, fp);
        if(n==0) break;
        while((ret = k_sendto(sockfd, buf, n, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr)))==-1);
        printf("message sent (len = %d) %d\n", ret, cnt++);
    }
    fclose(fp);
    
    sprintf(buf, "DONE");
    while((ret = k_sendto(sockfd, buf, 5, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr)))==-1);
    printf("message sent (len = %d) %d\n", ret, cnt++);
    printf("File sent successfully\n");
    k_close(sockfd);
    return 0;
}
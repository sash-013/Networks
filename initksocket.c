#include "ksocket.h"

int semkey, semkey1, semkey2; // keys
int sem, unlock, lock, shmid; // semaphores
struct sembuf pop, vop; // semaphore operations
ksocket_t *SM; // shared memory
pthread_t R, S, G; // threads
int pid; // process id

// Function to create the semaphores and shared memory
void init_semaphore(){
    semkey=ftok("/", 1);
    if(semkey==-1){
        perror("ftok");
        exit(1);
    }
    semkey1=ftok("/usr", 2);
    if(semkey1==-1){
        perror("ftok1");
        exit(1);
    }
    semkey2=ftok("/bin", 3);
    if(semkey2==-1){
        perror("ftok2");
        exit(1);
    }
    sem=semget(semkey, 1, 0777|IPC_CREAT);
    if(sem==-1){
        perror("semget");
        exit(1);
    }
    unlock=semget(semkey1, 1, 0777|IPC_CREAT);
    if(unlock==-1){
        perror("semget1");
        exit(1);
    }
    lock=semget(semkey2, 1, 0777|IPC_CREAT);
    if(lock==-1){
        perror("semget2");
        exit(1);
    }
    pop.sem_num=0;
    pop.sem_op=-1;
    pop.sem_flg=0;
    // vop.sem_num=0;
    vop.sem_op=1;
    vop.sem_flg=0;

    semctl(sem, 0, SETVAL, 1);
    semctl(unlock, 0, SETVAL, 0);
    semctl(lock, 0, SETVAL, 0);
    
    shmid=shmget(semkey, N*sizeof(ksocket_t), 0777|IPC_CREAT);
    if(shmid==-1){
        perror("shmget");
        exit(1);
    }
    printf("shmid: %d\n", shmid);
    SM=(ksocket_t *)shmat(shmid, 0, 0);
    if(SM==(void *)-1){
        perror("shmat");
        exit(1);
    }
    return;
}

void clean_exit(int code){
    shmdt(SM);
    if(pid>0){
        semctl(sem, 0, IPC_RMID, 0);
        semctl(unlock, 0, IPC_RMID, 0);
        semctl(lock, 0, IPC_RMID, 0);
        shmctl(shmid, IPC_RMID, 0);
        kill(pid, SIGKILL);
    }
    printf("Exiting\n");
    exit(code);
}

void init_sockets(){
    waitsem(sem);
    for(int i=0;i<N;i++) SM[i].state=FREE;
    signalsem(sem);
    return;
}

int dropmessage(float p){
    return (rand()%100<p*100);
}

void *reader(void *arg){
    fd_set rfds;
    struct timeval tv;
    while(1){
        FD_ZERO(&rfds);
        tv.tv_sec=T;
        tv.tv_usec=0;
        int maxfd=-1;
        waitsem(sem);
        for(int i=0;i<N;i++){
            if(SM[i].state==READY){
                FD_SET(SM[i].sockfd, &rfds);
                if(SM[i].sockfd>maxfd) maxfd=SM[i].sockfd;
            }
        }
        signalsem(sem);
        if(select(maxfd+1,&rfds,0,0,&tv)==-1) perror("select");
        waitsem(sem);
        for(int i=0;i<N;i++){
            if(SM[i].state==READY && FD_ISSET(SM[i].sockfd,&rfds)){
                printf("Socket Number %d: ", i);
                packet_t temp;
                socklen_t len=sizeof(SM[i].dest_addr);
                int n=recvfrom(SM[i].sockfd, &temp, sizeof(packet_t), 0, (struct sockaddr *)&SM[i].dest_addr, &len);
                if(n==-1){
                    perror("recvfrom");
                    continue;
                }
                printf("Received packet: SEQ=%d, ACK=%d, (size = %d)\n", temp.seq, temp.ack, temp.size);
                if(dropmessage(P)){
                    printf("Dropped packet: SEQ=%d, ACK=%d\n", temp.seq, temp.ack);
                    continue;
                }
                if(temp.ack==1){ // ACK Packet
                    int in_window=1;
                    int L=SM[i].send_window.base_num, R=(SM[i].send_window.next_num-1+MAX_SEQ_SIZE)%MAX_SEQ_SIZE;
                    int curr_index=temp.seq;
                    if((L<=R) && (curr_index<L || curr_index>R)) in_window=0;
                    if((L>R) && (curr_index<L && curr_index>R)) in_window=0;
                    if(in_window){
                        int window_index=SM[i].send_window.base_num;
                        int stop=(curr_index+1)%MAX_SEQ_SIZE;
                        while(window_index!=stop){
                            window_index=(window_index+1)%MAX_SEQ_SIZE;
                            SM[i].send_window.base_num=(SM[i].send_window.base_num+1)%MAX_SEQ_SIZE;
                            SM[i].send_buffer.read_ptr=(SM[i].send_buffer.read_ptr+1)%N;
                            SM[i].send_window.to_be_ack--;
                            SM[i].send_buffer.buf_cnt--;
                        }
                    }
                    else printf("Socket Number %d: Out of order packet SEQ %d ACK %d, L: %d, R: %d, curr_index: %d\n", i, temp.seq, temp.ack, L, R, temp.seq);
                    SM[i].send_window.window_size=temp.size;
                    if(temp.size<SM[i].send_window.to_be_ack){
                        SM[i].send_window.to_be_ack=temp.size;
                        SM[i].send_window.next_num=(SM[i].send_window.base_num+temp.size)%MAX_SEQ_SIZE;
                    }
                }
                else{ // Data Packet
                    if(SM[i].flag==NO_SPACE){
                        printf("Socket Number %d: Setting flag to HAS_SPACE\n", i);
                        SM[i].flag=HAS_SPACE;
                    }
                    int in_window=1;
                    int L=SM[i].recv_window.base_num, R=(SM[i].recv_window.window_size+L)%MAX_SEQ_SIZE;
                    int curr_index=temp.seq;
                    if(L<=R && (curr_index<L || curr_index>R)) in_window=0;
                    if(L>R && (curr_index<L && curr_index>R)) in_window=0;
                    if(in_window){
                        if(SM[i].recv_window.buf_flag[temp.seq]==0){
                            SM[i].recv_window.buf_flag[temp.seq]=1;
                            int buff_index=(SM[i].recv_buffer.write_ptr+((temp.seq-SM[i].recv_window.base_num+MAX_SEQ_SIZE)%MAX_SEQ_SIZE))%N;
                            SM[i].recv_buffer.buf[buff_index]=temp;
                            int base=SM[i].recv_window.base_num;
                            while(SM[i].recv_window.buf_flag[base]==1){
                                SM[i].recv_window.buf_flag[base]=0;
                                SM[i].recv_window.base_num=(SM[i].recv_window.base_num+1)%MAX_SEQ_SIZE;
                                SM[i].recv_buffer.write_ptr=(SM[i].recv_buffer.write_ptr+1)%N;
                                SM[i].recv_buffer.buf_cnt++;
                                SM[i].recv_window.window_size--;
                                base=(base+1)%MAX_SEQ_SIZE;
                            }
                        }
                        else printf("Packet already recieved SEQ %d ACK %d\n", temp.seq, temp.ack);
                    }
                    else printf("Socket Number %d: Out of order packet SEQ %d ACK %d, L: %d, R: %d, curr_index: %d\n", i, temp.seq, temp.ack, L, R, temp.seq);
                    if(SM[i].recv_window.window_size==0){
                        printf("Socket Number %d: Setting flag to NO_SPACE\n", i);
                        SM[i].flag=NO_SPACE;
                    }
                    packet_t send_ack;
                    send_ack.seq=(SM[i].recv_window.base_num-1+MAX_SEQ_SIZE)%MAX_SEQ_SIZE;
                    send_ack.ack=1;
                    send_ack.size=SM[i].recv_window.window_size;
                    if(sendto(SM[i].sockfd, &send_ack, sizeof(packet_t), 0, (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr))==-1){
                        perror("sendto, sender");
                        continue;
                    }
                    printf("Socket Number %d: Sent ACK, SEQ %d ACK %d, (size = %d)\n", i, send_ack.seq, send_ack.ack, send_ack.size);
                }
            }
        }
        signalsem(sem);
    }
    return NULL;
}

void *sender(void *arg){
    while(1){
        usleep(T*500000);
        waitsem(sem);
        for(int i=0;i<N;i++){
            if(SM[i].flag==NO_SPACE && SM[i].recv_window.window_size>0){
                packet_t send_ack;
                send_ack.seq=(SM[i].recv_window.base_num-1+MAX_SEQ_SIZE)%MAX_SEQ_SIZE;
                send_ack.ack=1;
                send_ack.size=SM[i].recv_window.window_size;
                if(sendto(SM[i].sockfd, &send_ack, sizeof(packet_t), 0, (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr))==-1){
                    perror("sendto, sender");
                    continue;
                }
                printf("Socket Number %d: Send probe packet SEQ %d ACK %d (size = %d)\n", i, send_ack.seq, send_ack.ack, send_ack.size);
            }
            time_t curr_time=time(0);
            if(SM[i].state==READY && curr_time-SM[i].last_sent>=T){
                int to_be_sent=SM[i].send_window.base_num;
                int to_be_sent_data_ptr=SM[i].send_buffer.read_ptr;
                while(to_be_sent!=SM[i].send_window.next_num){
                    printf("Socket Number %d: ", i);
                    if(sendto(SM[i].sockfd, &SM[i].send_buffer.buf[to_be_sent_data_ptr], sizeof(packet_t), 0, (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr))==-1){
                        perror("sendto, sender");
                        continue;
                    }
                    SM[i].trans_count++;
                    SM[i].last_sent=curr_time;
                    printf("Timeout: Sent message SEQ %d ACK %d (char = %c), (to_be_sent_data_ptr %d)\n", to_be_sent, SM[i].send_window.buf_flag[to_be_sent], SM[i].send_buffer.buf[to_be_sent_data_ptr].data[0], to_be_sent_data_ptr);
                    to_be_sent=(to_be_sent+1)%MAX_SEQ_SIZE;
                    to_be_sent_data_ptr=(to_be_sent_data_ptr+1)%N;
                }
            }
            if(SM[i].state==READY && SM[i].send_buffer.buf_cnt>SM[i].send_window.to_be_ack){
                while(SM[i].send_window.to_be_ack<SM[i].send_window.window_size && SM[i].send_buffer.buf_cnt>SM[i].send_window.to_be_ack){
                    printf("Socket Number %d: ", i);
                    // SM[i].send_buffer.buf_cnt--;
                    SM[i].send_buffer.buf[SM[i].send_buffer.next_ptr].seq=SM[i].send_window.next_num;
                    SM[i].send_buffer.buf[SM[i].send_buffer.next_ptr].ack=0;
                    SM[i].send_window.to_be_ack++;
                    SM[i].send_window.buf_flag[SM[i].send_window.next_num]=0; // waiting to be acknowledged
                    if(sendto(SM[i].sockfd, &SM[i].send_buffer.buf[SM[i].send_buffer.next_ptr], sizeof(packet_t), 0, (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr))==-1){
                        perror("sendto, sender");
                        continue;
                    }
                    SM[i].trans_count++;
                    printf("%d %d\n", SM[i].msg_count, SM[i].trans_count);
                    SM[i].last_sent=curr_time;
                    printf("First time: Sent message SEQ %d ACK %d (char = %c)\n", SM[i].send_window.next_num, SM[i].send_window.buf_flag[SM[i].send_window.next_num], SM[i].send_buffer.buf[SM[i].send_buffer.next_ptr].data[0]);
                    SM[i].send_window.next_num=(SM[i].send_window.next_num+1)%MAX_SEQ_SIZE;
                    SM[i].send_buffer.next_ptr=(SM[i].send_buffer.next_ptr+1)%N;
                }
            }
        }
        signalsem(sem);
    }
    return NULL;
}

void *garbage(void *arg){
    while(1){
        sleep(T);
        waitsem(sem);
        for(int i=0;i<N;i++) {
            if(SM[i].state!=FREE && kill(SM[i].pid, 0)==-1){
                printf("Socket %d garbage collected\n", i);
                SM[i].state=CLOSE_PENDING;
                close(SM[i].sockfd);
                SM[i].state=FREE;
            }
        }
        signalsem(sem);
    }
    return NULL;
}

void help(){
    semkey=ftok("/", 1);
    semkey1=ftok("/usr", 2);
    semkey2=ftok("/bin", 3);
    sem=semget(semkey, 1, 0777);
    unlock=semget(semkey1, 1, 0777);
    lock=semget(semkey2, 1, 0777);

    shmid=shmget(semkey, N*sizeof(ksocket_t), 0777);

    clean_exit(0);
    return;
}

int main(){
    srand(time(0));
    signal(SIGINT, clean_exit);
    // help();
    init_semaphore();
    printf("Sempahore made\n");
    fflush(stdout);
    init_sockets();
    printf("Sockets initialized\n");
    fflush(stdout);

    pthread_create(&R,0,reader,0);
    pthread_create(&S,0,sender,0);
    pthread_create(&G,0,garbage,0);

    if((pid=fork())==0){ // garbage cleaner process
        while(1){
            sleep(T);
            waitsem(sem);
            for(int i=0;i<N;i++) {
                if(SM[i].state!=FREE && kill(SM[i].pid, 0)==-1){
                    printf("Socket %d garbage collected\n", i);
                    SM[i].state=CLOSE_PENDING;
                    signalsem(unlock);
                    waitsem(lock);
                }
            }
            signalsem(sem);
        }
        clean_exit(0);
    }
    
    while(1){
        waitsem(unlock);
        for(int i=0;i<N;i++){
            if(SM[i].state==CREATE_PENDING){
                SM[i].sockfd=socket(AF_INET,SOCK_DGRAM,0);
                if(SM[i].sockfd==-1)break;
                SM[i].state=CREATED;

                SM[i].recv_buffer.buf_cnt=0;
                SM[i].recv_buffer.read_ptr=0;
                SM[i].recv_buffer.next_ptr=0;
                SM[i].recv_buffer.write_ptr=0;

                SM[i].send_buffer.buf_cnt=0;
                SM[i].send_buffer.read_ptr=0;
                SM[i].send_buffer.next_ptr=0;
                SM[i].send_buffer.write_ptr=0;

                SM[i].recv_window.base_num=0;
                SM[i].recv_window.next_num=0;
                SM[i].recv_window.to_be_ack=0;
                SM[i].recv_window.window_size=N;

                SM[i].send_window.base_num=0;
                SM[i].send_window.next_num=0;
                SM[i].send_window.to_be_ack=0;
                SM[i].send_window.window_size=N;

                SM[i].msg_count=0;
                SM[i].trans_count=0;
                SM[i].flag=HAS_SPACE;

                for(int j=0; j<MAX_SEQ_SIZE; j++) SM[i].recv_window.buf_flag[j]=0;
                printf("Socket %d created\n", i);
                break;
            }
            else if(SM[i].state==BIND_PENDING){
                if(bind(SM[i].sockfd, (struct sockaddr *)&SM[i].src_addr, sizeof(SM[i].src_addr))==-1)break;
                printf("Socket %d bound to %s:%d\n", i, inet_ntoa(SM[i].src_addr.sin_addr), ntohs(SM[i].src_addr.sin_port));
                SM[i].state=READY;
                break;
            }
            else if(SM[i].state==CLOSE_PENDING){
                close(SM[i].sockfd);
                printf("\t\tSocket %d closed\n", i);
                printf("\t====Statistics for socket %d====\n", i);
                printf("\t\tMessages sent: %d\n", SM[i].msg_count);
                printf("\t\tTransmissions: %d\n", SM[i].trans_count);
                printf("\t================================\n");
                SM[i].state=FREE;
                break;
            }
        }
        signalsem(lock);
    }

    pthread_join(R, 0);
    pthread_join(S, 0);
    pthread_join(G, 0);
    
    clean_exit(0);
    return 0;
}
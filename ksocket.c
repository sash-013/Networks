#include "ksocket.h"

int semkey, semkey1, semkey2;   // keys
int sem, unlock, lock, shmid;   // semaphores
struct sembuf pop, vop;         // semaphore operations
ksocket_t *SM;                  // shared memory

int GLOBAL_ERRNO=0;             // Global error number

// Function to initialize the semaphore, shared memory and other resources (mainly getting them)
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
    sem=semget(semkey, 1, 0777);
    if(sem==-1){
        perror("semget");
        exit(1);
    }
    unlock=semget(semkey1, 1, 0777);
    if(unlock==-1){
        perror("semget1");
        exit(1);
    }
    lock=semget(semkey2, 1, 0777);
    if(lock==-1){
        perror("semget2");
        exit(1);
    }
    pop.sem_num=0;
    pop.sem_op=-1;
    pop.sem_flg=0;
    vop.sem_num=0;
    vop.sem_op=1;
    vop.sem_flg=0;

    shmid=shmget(semkey, N*sizeof(ksocket_t), 0777);
    if(shmid==-1){
        perror("shmget");
        exit(1);
    }
    SM=(ksocket_t *)shmat(shmid, 0, 0);
    if(SM==(void *)-1){
        perror("shmat");
        exit(1);
    }
    return;
}

// Function to deinitialize the shared memory
void deinit_semaphore(){
    shmdt((void *)SM);
    return;
}

int k_socket(int domain, int type, int protocol){
    if(type!=SOCK_KTP){
        GLOBAL_ERRNO=EINVAL;
        return -1;
    }

    init_semaphore();
    waitsem(sem);

    for(int i=0; i<N; i++){
        if(SM[i].state==FREE){
            SM[i].state=CREATE_PENDING;
            signalsem(unlock);
            waitsem(lock);
            if(SM[i].state!=CREATED){
                GLOBAL_ERRNO=EINVAL;
                signalsem(sem);
                deinit_semaphore();
                return -1;
            }
            SM[i].pid=getpid();
            signalsem(sem);
            deinit_semaphore();
            return i;
        }
    }

    signalsem(sem);
    deinit_semaphore();
    return -1;
}

// Function to bind the socket to the source and destination addresses
int k_bind(int sockfd, const struct sockaddr *src_addr, const struct sockaddr *dest_addr){
    init_semaphore();
    waitsem(sem);
    if(sockfd<0 || sockfd>=N || SM[sockfd].state!=CREATED){
        GLOBAL_ERRNO=EINVAL;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }

    SM[sockfd].src_addr=*(struct sockaddr_in *)src_addr;
    SM[sockfd].dest_addr=*(struct sockaddr_in *)dest_addr;

    SM[sockfd].state=BIND_PENDING;
    signalsem(unlock);
    waitsem(lock);

    if(SM[sockfd].state!=READY){
        GLOBAL_ERRNO=EINVAL;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }

    signalsem(sem);
    deinit_semaphore();
    return 0;
}

// Function to send a message to the destination
int k_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen){
    init_semaphore();
    waitsem(sem);
    if(sockfd<0 || sockfd>=N || SM[sockfd].state!=READY){
        GLOBAL_ERRNO=EINVAL;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }

    int flag = 1;
    if(SM[sockfd].dest_addr.sin_family!=dest_addr->sa_family) flag=0;
    if(SM[sockfd].dest_addr.sin_addr.s_addr!=((struct sockaddr_in *)dest_addr)->sin_addr.s_addr) flag=0;
    if(SM[sockfd].dest_addr.sin_port!=((struct sockaddr_in *)dest_addr)->sin_port) flag=0;
    if(flag==0){
        GLOBAL_ERRNO=ENOTBOUND;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }

    if(SM[sockfd].send_buffer.buf_cnt==N){
        GLOBAL_ERRNO=ENOSPACE;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }

    SM[sockfd].msg_count++;
    len=len>BUFLEN?BUFLEN:len;
    for(int i=0; i<len; i++) SM[sockfd].send_buffer.buf[SM[sockfd].send_buffer.write_ptr].data[i]=((char *)buf)[i];
    // printf("Write_ptr: %d, Char: %c\n", SM[sockfd].send_buffer.write_ptr, SM[sockfd].send_buffer.buf[SM[sockfd].send_buffer.write_ptr].data[0]);
    SM[sockfd].send_buffer.buf[SM[sockfd].send_buffer.write_ptr].size=len;
    SM[sockfd].send_buffer.write_ptr=(SM[sockfd].send_buffer.write_ptr+1)%N;
    SM[sockfd].send_buffer.buf_cnt++;
    signalsem(sem);
    deinit_semaphore();
    return len;
}

int k_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen){
    init_semaphore();
    waitsem(sem);
    if(sockfd<0 || sockfd>=N || SM[sockfd].state!=READY){
        GLOBAL_ERRNO=EINVAL;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }

    if(SM[sockfd].recv_buffer.buf_cnt==0){
        GLOBAL_ERRNO=ENOMESSAGE;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }

    len=len>SM[sockfd].recv_buffer.buf[SM[sockfd].recv_buffer.read_ptr].size?SM[sockfd].recv_buffer.buf[SM[sockfd].recv_buffer.read_ptr].size:len;
    for(int i=0;i<len;i++) ((char *)buf)[i]=SM[sockfd].recv_buffer.buf[SM[sockfd].recv_buffer.read_ptr].data[i];
    SM[sockfd].recv_buffer.read_ptr=(SM[sockfd].recv_buffer.read_ptr+1)%N;
    SM[sockfd].recv_buffer.buf_cnt--;
    SM[sockfd].recv_window.window_size++;
    signalsem(sem);
    deinit_semaphore();
    return len;
}

int k_close(int sockfd){
    init_semaphore();
    waitsem(sem);
    if(sockfd<0 || sockfd>=N || SM[sockfd].state!=READY){
        GLOBAL_ERRNO=EINVAL;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }
    signalsem(sem);

    waitsem(sem);
    time_t close_time=time(0)+50*T;
    while(SM[sockfd].send_buffer.buf_cnt>0 && close_time>time(0)){
        signalsem(sem);
        usleep(10000);
        waitsem(sem);
    }

    SM[sockfd].state=CLOSE_PENDING;
    signalsem(unlock);
    waitsem(lock);
    if(SM[sockfd].state!=FREE){
        GLOBAL_ERRNO=EINVAL;
        signalsem(sem);
        deinit_semaphore();
        return -1;
    }

    signalsem(sem);
    deinit_semaphore();
    return 0;
}

void k_perror(const char *msg){
    if(GLOBAL_ERRNO==0) return;
    switch(GLOBAL_ERRNO){
        case ENOSPACE:
            fprintf(stderr, "%s: No space available\n", msg);
            break;
        case ENOTBOUND:
            fprintf(stderr, "%s: Socket not bound to the destination\n", msg);
            break;
        case ENOMESSAGE:
            fprintf(stderr, "%s: No message available\n", msg);
            break;
        case EINVAL:
            fprintf(stderr, "%s: Invalid argument\n", msg);
            break;
        default:
            errno = GLOBAL_ERRNO;
            perror(msg);
    }
    GLOBAL_ERRNO=0;
    return;
}
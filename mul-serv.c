#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <strings.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#define SERV_ADDR "192.168.1.5"
#define SERV_PORT 2000
#define MAX_PORT 100

#define OPEN_SIZE 1000
#define BUF_SIZE 100

#define TIME_SUB(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

typedef int (*call_back_t)(int efd, int fd, void* arg);

int Read(int efd, int fd, void* arg);
int Write(int efd, int fd, void* arg);

struct timeval begin_tv, end_tv;
typedef struct connect {
    int fd;
    char rbuf[BUF_SIZE];
    int rlen;
    char wbuf[BUF_SIZE];
    int wlen;
    call_back_t call_bk;
    void* arg;
    struct connect *next;
    struct connect *last;
}connect_t;

typedef struct head_connect {
    long count;
    struct connect *first;
    struct connect *tail;
    int (*conn_add_f)(struct head_connect* head, int fd, call_back_t call_bk, void* arg);
    void (*conn_del_f)(struct head_connect* head, connect_t *conn);
    struct connect* (*conn_lookup_f)(struct head_connect* head, int fd);
}head_connect_t;

int conn_add(struct head_connect* head, int fd, call_back_t call_bk, void* arg) {
    struct connect* conn = NULL;
    conn = (struct connect*)malloc(sizeof(struct connect));
    if (!conn) {
        perror("malloc");
        return -errno;
    }
    
    conn->fd = fd;
    conn->next = NULL;
    conn->last = NULL;
    conn->call_bk = call_bk;
    conn->arg = arg;

    if (!head->first) {
        head->first = head->tail = conn;
    } else {
        head->tail->next = conn;
        conn->last = head->tail;
        head->tail = conn;
    }
    head->count++;

    return 0;
}

struct connect* conn_lookup(struct head_connect* head, int fd) {
    connect_t* curr = head->first;
    while(curr) {
        if (curr->fd == fd)
            break;
        curr = curr->next;
    }
    return curr;
}

void conn_del(struct head_connect* head, connect_t *conn) {
    if (!conn) return;
    if (conn->last) conn->last->next = conn->next;
    if (conn->next) conn->next->last = conn->last;
    if (head->first == conn) head->first = conn->next;
    if (head->tail == conn) head->tail = conn->last;
    head->count--;
    free(conn);
}

int Socket(int idx) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	// servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    inet_pton(AF_INET, SERV_ADDR, &servaddr.sin_addr); // 推荐
	servaddr.sin_port = htons(SERV_PORT + idx);

    if (-1 == bind(sockfd, (struct sockaddr*) &servaddr, sizeof(servaddr))) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if(-1 == listen(sockfd, 1)) {
        perror("listen");
        close(sockfd);
        return -1;
    }
    printf("listening prot: %d\n", SERV_PORT + idx);
    return sockfd;
}

int Accept(int efd, int fd, void* arg) {
    int j, connfd;
    struct epoll_event ev;
    struct sockaddr_in cliaddr;
    char addr_cli[INET_ADDRSTRLEN];
    head_connect_t *head = (head_connect_t*)arg;
    socklen_t clilen = sizeof(cliaddr);

    if ((connfd = accept(fd, (struct sockaddr*)&cliaddr, &clilen)) == -1) {
        perror("accept");
        return -1;
    }

    inet_ntop(AF_INET, &cliaddr.sin_addr, addr_cli, sizeof(addr_cli));
    if(head->count % 1000 == 0) {
        gettimeofday(&end_tv, NULL);
        printf("connect count: %ld, take time: %ld\n", head->count, TIME_SUB(end_tv, begin_tv));
    }
    // printf("accept from %s:%d, total conn: %ld\n", addr_cli, ntohs(cliaddr.sin_port), head->count);

    ev.data.fd = connfd;
    ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
        perror("add connfd");
        ev.data.fd = -1;
        close(connfd);
        return -1;
    }

    return connfd;
}

int Read(int efd, int fd, void* arg) {
    int j;
    connect_t *conn = (connect_t *)arg;
    memset(conn->rbuf, 0, BUF_SIZE);
    conn->rlen = read(fd, conn->rbuf, BUF_SIZE);
    if (conn->rlen <= 0) {
        // perror("read");
        return -errno;
    }

    // struct epoll_event ev;
    // ev.data.fd = fd;
    // ev.events = EPOLLOUT;
    // epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev);

    memset(conn->wbuf, 0, BUF_SIZE);
    memcpy(conn->wbuf, conn->rbuf, conn->rlen);
    conn->wlen = conn->rlen;
    conn->call_bk = Write;
    printf("recv:%s", conn->rbuf);

    return conn->rlen;
}

int Write(int efd, int fd, void* arg) {
    size_t wsize = 0, ret = 0;
    connect_t *conn = (connect_t *)arg;
    while (wsize < conn->wlen) {
        ret = write(fd, conn->wbuf + wsize, conn->wlen - wsize);
        if (ret < 0 && errno != EINTR && EINTR != EAGAIN) {
            perror("write");
            return -errno;
        }
        wsize += ret;
    }
    conn->call_bk = Read;
    // struct epoll_event ev;
    // ev.data.fd = fd;
    // ev.events = EPOLLIN;
    // epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev);

    return wsize;
}

int lookup_sockfd(int* sockfd, int fd) {
    int i;
    for (i = 0; i < MAX_PORT; i++) {
        if (sockfd[i] == fd) return 1;
    }
    return 0;
}

int main()
{
    int nready = 0, i, *sockfd = NULL, *pfd = NULL;
    struct head_connect head;
    struct epoll_event evs[OPEN_SIZE], tev;

    head.first = head.tail = NULL;
    head.conn_add_f = conn_add;
    head.conn_del_f = conn_del;
    head.conn_lookup_f = conn_lookup;
    head.count = 0;

    int efd = epoll_create(1);
    if (efd == -1) {
        perror("epoll_create");
        return errno;
    }

    sockfd = (int*)malloc(sizeof(int) * MAX_PORT);
    pfd = sockfd;

    for (i = 0; i < MAX_PORT; i++) {
        pfd[i] = Socket(i);
        if (pfd[i] < 0) {
            return errno;
        }
        
        tev.data.fd = pfd[i];
        tev.events = EPOLLIN;
        if (epoll_ctl(efd, EPOLL_CTL_ADD, pfd[i], &tev) == -1) {
            perror("epoll_ctl");
            close(pfd[i]);
            return errno;
        }

        if (head.conn_add_f(&head, pfd[i], Accept, &head) < 0) {
            close(pfd[i]);
            return errno;
        }
    }

    gettimeofday(&begin_tv, NULL);
    while (1) {
        if ((nready = epoll_wait(efd, evs, OPEN_SIZE, -1)) == -1) {
            perror("epoll_wait");
            close(efd);
            return errno;
        }
        
        for (i = 0; i < nready; i++) {
            tev = evs[i];
            int fd = tev.data.fd;
            if (tev.events & EPOLLIN) { // 可读事件
                if(lookup_sockfd(sockfd, fd)) {  // 连接就绪
                    connect_t* conn = head.conn_lookup_f(&head, fd);
                    int connfd = Accept(efd, fd, conn->arg);
                    if (connfd > 0) head.conn_add_f(&head, connfd, Read, NULL);
                } else {            // 读就绪
                    connect_t* conn = head.conn_lookup_f(&head, fd);
                    if (conn->call_bk(efd, fd, conn) <= 0) {
                        epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
                        tev.data.fd = -1;
                        close(fd);
                        head.conn_del_f(&head, conn);
                        // printf("%d disconnect\n", fd);
                        continue;
                    }
                }
            } else if (tev.events & EPOLLOUT) { // 可写事件
                connect_t* conn = head.conn_lookup_f(&head, fd);
                if (conn->wlen == 0) continue;
                if (conn->call_bk(efd, fd, conn) <= 0) {
                    epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
                    tev.data.fd = -1;
                    close(fd);
                    head.conn_del_f(&head, conn);
                }
            }
        }
    }

    return 0;
}

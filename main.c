#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define SERV_ADDR "192.168.1.10"
#define SERV_PORT 2300

#define OPEN_SIZE 1024
#define BUF_SIZE 100

typedef int (*call_back_t)(int efd, int fd, void* arg);

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
    struct connect *first;
    struct connect *tail;
    int (*conn_add_f)(struct head_connect* head, int fd, call_back_t call_bk, void* arg);
    void (*conn_del_f)(struct head_connect* head, int fd);
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

void conn_del(struct head_connect* head, int fd) {
    struct connect* conn = conn_lookup(head, fd);
    if (!conn) return;
    if (conn->last) conn->last->next = conn->next;
    if (conn->next) conn->next->last = conn->last;
    if (head->first == conn) head->first = conn->next;
    if (head->tail == conn) head->tail = conn->last;
    free(conn);
}

int Socket() {
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
	servaddr.sin_port = htons(SERV_PORT);

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
    return sockfd;
}

int Accept(int efd, int fd, void* arg) {
    int j, connfd;
    struct epoll_event ev;
    struct sockaddr_in cliaddr;
    char addr_cli[INET_ADDRSTRLEN];
    socklen_t clilen = sizeof(cliaddr);

    if ((connfd = accept(fd, (struct sockaddr*)&cliaddr, &clilen)) == -1) {
        perror("accept");
        return -1;
    }

    inet_ntop(AF_INET, &cliaddr.sin_addr, addr_cli, sizeof(addr_cli));
    printf("accept from %s:%d\n", addr_cli, ntohs(cliaddr.sin_port));

    ev.data.fd = connfd;
    ev.events = EPOLLIN ;
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
        perror("read");
        return -errno;
    }

    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLOUT;
    epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev);
    

    memset(conn->wbuf, 0, BUF_SIZE);
    memcpy(conn->wbuf, conn->rbuf, conn->rlen);
    conn->wlen = conn->rlen;
    printf("recv:%s", conn->rbuf);

    return conn->rlen;
}

int Write(int efd, int fd, void* arg) {
    size_t wsize = 0, ret = 0;
    connect_t *conn = (connect_t *)arg;
    while (wsize < conn->wlen) {
        ret = write(fd, conn->wbuf + wsize, conn->wlen - wsize);
        if (ret <= 0) {
            perror("write");
            return -errno;
        }
        wsize += ret;
    }

    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev);

    return wsize;
}

int main()
{
    int nready = 0, i, sockfd;
    struct head_connect head;
    struct epoll_event evs[OPEN_SIZE], tev;

    head.first = head.tail = NULL;
    head.conn_add_f = conn_add;
    head.conn_del_f = conn_del;
    head.conn_lookup_f = conn_lookup;

    sockfd = Socket();
    if (sockfd < 0) {
        return errno;
    }

    int efd = epoll_create(1);
    if (efd == -1) {
        perror("epoll_create");
        close(sockfd);
        return errno;
    }

    tev.data.fd = sockfd;
    tev.events = EPOLLIN;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &tev) == -1) {
        perror("epoll_ctl");
        close(sockfd);
        return errno;
    }

    if (head.conn_add_f(&head, sockfd, Accept, NULL) < 0) {
        close(sockfd);
        return errno;
    }

    while (1) {
        if ((nready = epoll_wait(efd, evs, OPEN_SIZE, -1)) == -1) {
            perror("epoll_wait");
            close(efd);
            close(sockfd);
            return errno;
        }
        
        for (i = 0; i < nready; i++) {
            tev = evs[i];
            int fd = tev.data.fd;
            if (tev.events & EPOLLIN) { // 可读事件
                if(fd == sockfd) {  // 连接就绪
                    connect_t* conn = head.conn_lookup_f(&head, fd);
                    int connfd = Accept(efd, fd, conn->arg);
                    if (connfd > 0) head.conn_add_f(&head, connfd, Read, NULL);
                } else {            // 读就绪
                    connect_t* conn = head.conn_lookup_f(&head, fd);
                    if (conn->call_bk(efd, fd, conn) <= 0) {
                        epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
                        tev.data.fd = -1;
                        close(fd);
                        printf("%d disconnect\n", fd);
                        continue;
                    }
                }
            } else if (tev.events & EPOLLOUT) { // 可写事件
                connect_t* conn = head.conn_lookup_f(&head, fd);
                if (conn->wlen == 0) continue;
                if (Write(efd, fd, conn) <= 0) {
                    epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
                    tev.data.fd = -1;
                    close(fd);
                }
            }
        }
    }

    return 0;
}
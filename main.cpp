#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define SERV_ADDR "192.168.1.10"
#define SERV_PORT 3000

#define OPEN_SIZE 1024
#define BUF_SIZE 100

struct connect_t {
    char rbuf[BUF_SIZE];
    int rlen;
    char wbuf[BUF_SIZE];
    int wlen;
};

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

int Accept(int efd, int fd, struct epoll_event* ev) {
    int j;
    int connfd;
    struct sockaddr_in cliaddr;
    char addr_cli[INET_ADDRSTRLEN];
    socklen_t clilen = sizeof(cliaddr);
    if ((connfd = accept(fd, (struct sockaddr*)&cliaddr, &clilen)) == -1) {
        perror("accept");
        return -1;
    }
    inet_ntop(AF_INET, &cliaddr.sin_addr, addr_cli, sizeof(addr_cli));
    printf("accept from %s:%d\n", addr_cli, ntohs(cliaddr.sin_port));
    ev->data.fd = connfd;
    ev->events = EPOLLIN;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, ev) == -1) {
        perror("add connfd");
        ev->data.fd = -1;
        close(connfd);
        return -1;
    }
    return 0;
}

size_t Read(int efd, int fd, char *buf, size_t maxLen, struct epoll_event* ev) {
    int j;
    ssize_t rsize = 0;
    rsize = read(fd, buf, maxLen);
    if (rsize == 0) {
        if (-1 == epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL)) {
            perror("epoll_ctl DEL");
            return 0;
        }
        ev->data.fd = -1;
        printf("%d disconnect\n", fd);
        close(fd);
    } else if (rsize == -1) {
        perror("read");
        return -1;
    }
    return rsize;
}

size_t Write(int fd, char *buf, size_t maxLen) {
    size_t wsize = 0, ret = 0;
    while (wsize < maxLen) {
        ret = write(fd, buf + wsize, maxLen - wsize);
        if (ret <= 0) {
            perror("write");
            return 0;
        }
        wsize += ret;
    }
    return wsize;
}

int main()
{
    int sockfd = Socket();
    if (sockfd < 0) {
        return 1;
    }

    int efd = epoll_create(1);
    if (efd == -1) {
        perror("epoll_create");
        close(sockfd);
        return 1;
    }

    struct epoll_event ev, evt[OPEN_SIZE], tev;
    ev.data.fd = sockfd;
    ev.events = EPOLLIN | EPOLLOUT;
    if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &ev)) {
        perror("epoll_ctl");
        close(sockfd);
        return 1;
    }

    int nready = 0;
    int i, j;
    connect_t conn;
    while (1) {
        if ((nready = epoll_wait(efd, evt, OPEN_SIZE, -1)) == -1) {
            perror("epoll_wait");
            close(efd);
            close(sockfd);
            return 1;
        }
        
        for (i = 0; i < nready; i++) {
            tev = evt[i];
            int fd = tev.data.fd;
            if (tev.events & EPOLLIN) { // 可读事件
                if(fd == sockfd) {   // 连接就绪
                    for (j = 0; j < OPEN_SIZE; j++) {
                        if (evt[j].data.fd == -1) break;
                    }
                    Accept(efd, fd, &evt[j]);
                } else {            // 读就绪
                    memset(conn.rbuf, 0, BUF_SIZE);
                    conn.rlen = Read(efd, fd, conn.rbuf, BUF_SIZE, &tev);
                    if (conn.rlen <= 0) continue;

                    memset(conn.wbuf, 0, BUF_SIZE);
                    memcpy(conn.wbuf, conn.rbuf, conn.rlen);
                    conn.wlen = conn.rlen;

                    printf("recv:%s", conn.rbuf);
                }
            } else if (tev.events & EPOLLOUT){ // 可写事件
                if (conn.wlen == 0) continue;
                Write(fd, conn.wbuf, conn.wlen);
            }
        }
    }

    return 0;
}
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
#define BUF_SIZE 40

size_t Read(int fd, char *buf, size_t len) {
    ssize_t rsize = 0, ret = 0;
    // while(rsize < len) {
        ret = read(fd, buf + rsize, len - rsize);
        if (ret == 0) {
            return 0;
        } else if (ret == -1) {
            perror("error epoll_ctl");
            return -1;
        }
        rsize += ret;
    // }
    return rsize;
}

size_t Write(int fd, char *buf, size_t len) {
    size_t wsize = 0, ret = 0;
    while (wsize < len) {
        ret = write(fd, buf + wsize, len - wsize);
        if (ret <= 0) {
            return 0;
        }
        wsize += ret;
    }
    return wsize;
}

int main()
{

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in cliaddr, servaddr;
    bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	// servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    inet_pton(AF_INET, SERV_ADDR, &servaddr.sin_addr); // 推荐
	servaddr.sin_port = htons(SERV_PORT);

    if (-1 == bind(sockfd, (struct sockaddr*) &servaddr, sizeof(servaddr))) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    if(-1 == listen(sockfd, 1)) {
        perror("listen");
        close(sockfd);
        return 1;
    }

    int efd = epoll_create(1);
    if (efd == -1) {
        perror("epoll_create1");
        close(sockfd);
        return 1;
    }

    struct epoll_event ev, evt[OPEN_SIZE], tev;
    ev.data.fd = sockfd;
    ev.events = EPOLLIN;
    if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &ev)) {
        perror("epoll_ctl");
        close(sockfd);
        return 1;
    }

    int nready = 0;
    int i, j;
    int connfd;
    socklen_t clilen;
    ssize_t rsize, wsize;
    char addr_cli[INET_ADDRSTRLEN], rbuf[BUF_SIZE];
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
            if (tev.events & EPOLLIN) { // 可读
                if(fd == sockfd) { // 新连接就绪
                    clilen = sizeof(cliaddr);
                    if ((connfd = accept(fd, (struct sockaddr*)&cliaddr, 
                                        &clilen)) == -1) {
                        perror("accept");
                        continue;
                    }
                    inet_ntop(AF_INET, &cliaddr.sin_addr, addr_cli, sizeof(addr_cli));
                    printf("accept from %s:%d\n", addr_cli, ntohs(cliaddr.sin_port));
                    for (j = 0; j < OPEN_SIZE; j++) {
                        if (evt[j].data.fd == -1) {
                            evt[j].data.fd = connfd;
                            evt[j].events = EPOLLIN;
                            break;
                        }
                    }
                    if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &evt[j]) == -1) {
                        perror("add connfd");
                        continue;
                    }
                } else { // 可读数据就绪
                    memset(rbuf, 0, BUF_SIZE);
                    rsize = Read(fd, rbuf, BUF_SIZE);
                    if (rsize <= 0) {
                        if (-1 == epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL)) {
                            perror("epoll_ctl");
                            return 0;
                        }
                        for (j = 0; j < OPEN_SIZE; j++) {
                            if (evt->data.fd == fd) {
                                evt->data.fd = -1;
                                break;
                            }
                        }
                        printf("%d disconnect\n", fd);
                        close(fd);
                        continue;
                    }
                    printf("recv:%s", rbuf);
                    wsize = Write(fd, rbuf, rsize);
                    if (wsize <= 0) {
                        perror("write");
                    }
                }
            } else if (tev.events & EPOLLOUT){

            }
        }
    }

    return 0;
}
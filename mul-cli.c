#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>

#define MAX_EVENTS 100
#define MAX_CONN 1000
#define SERV_PORT 2000
#define SERV_ADDR "192.168.1.5"

#define BUF_SIZE 100

#define TIME_SUB(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

int Write(int fd, char* buf, int maxSize) {
    int ret = 0, sendSize = 0;
    while (sendSize < maxSize) {
        ret = write(fd, buf + ret, maxSize - ret);
        if (ret < 0 && errno != EINTR && errno != EAGAIN) {
            return ret;
        }
        sendSize += ret;
    }
    return sendSize;
}

int main() {
    int* fds = (int*)malloc(sizeof(int) * MAX_CONN);
    int i, idx = 0, nready = 0;
    struct sockaddr_in addr;

    bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
    inet_pton(AF_INET, SERV_ADDR, &addr.sin_addr); // 推荐

    int efd = epoll_create(1);
    if (efd == -1) {
        perror("epoll_create");
        exit(0);
    }

    struct timeval begin_tv, end_tv;
	gettimeofday(&begin_tv, NULL);
    for (i = 1; i <= MAX_CONN; i++) {
        if (i % 100 == 0) {
            idx = 0;
        }
        addr.sin_port = htons(SERV_PORT + idx);
        
        fds[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (fds[i] == -1) {
            perror("socket");
            return errno;
        }

        if (connect(fds[i], (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("connect");
            close(fds[i]);
            continue;
        }
        printf("%d: fd = %d, connected to %d\n", i, fds[i], SERV_PORT + idx);
        if (i % 1000 == 0) {
            gettimeofday(&end_tv, NULL);
            printf("count = %d, take time = %d ms\n", i, TIME_SUB(end_tv, begin_tv));
        }
        idx++;

        char buf[BUF_SIZE] = {0};
        snprintf(buf, BUF_SIZE, "fd = %d send data\n", fds[i]);
        if (Write(fds[i], buf, strlen(buf)) <= 0) {
            perror("Write");
            close(fds[i]);
            continue;
        }

        struct epoll_event ev;
        ev.data.fd = fds[i];
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        epoll_ctl(efd, EPOLL_CTL_ADD, fds[i], &ev);
    }

    printf("====== connect complete =======\n");
    struct epoll_event events[MAX_EVENTS];

    while(1) {
        nready = epoll_wait(efd, events, MAX_EVENTS, -1);
        for (i = 0; i < nready; i++) {
            int fd = events->data.fd;
            if (events[i].events & EPOLLIN) {
                char buf[BUF_SIZE] = {0};
                snprintf(buf, BUF_SIZE, "fd = %d send data\n", fd);
                if (Write(fd, buf, strlen(buf)) <= 0) {
                    perror("Write");
                    epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                }
            } else if (events[i].events & EPOLLOUT) {
                char buf[BUF_SIZE] = {0};
                if (read(fd, buf, sizeof(buf)) <=0) {
                    perror("read");
                    epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                }
                printf("recv:%s\n", buf);
            }
        }
        usleep(1000);
    }

    return 0;
}
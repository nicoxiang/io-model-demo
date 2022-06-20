/**
 * 相较于 select，poll 的优点
 * 1. 内核中保存一份文件描述符集合，无需用户每次都重新传入，只需告诉内核修改的部分
 * 2. 不再通过轮询的的方式找到就绪的 fd，而是通过异步 IO 事件唤醒 epoll_wait
 * 3. 内核仅会将有事件发生的 fd 返回给用户，用户无需遍历整个 fd 集合
 * */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/types.h>

#define MAXEVENTS 1024

int initserver(int port);

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("usage: ./epollserverdemo port\n");
        return -1;
    }

    // 用于监听的 socket
    int listensock = initserver(atoi(argv[1]));
    if (listensock < 0)
    {
        printf("initserver() failed.\n");
        return -1;
    }
    printf("listensock=%d\n", listensock);

    // 创建一个新的 epoll 实例，返回对应的 fd，参数无用，大于0即可
    int epollfd = epoll_create(1);

    /**
     *     typedef union epoll_data {
               void    *ptr;
               int      fd;
               uint32_t u32;
               uint64_t u64;
           } epoll_data_t;

           struct epoll_event {
               uint32_t     events;    // Epoll events 
               epoll_data_t data;      // User data variable
           };
     * 
     * */

    struct epoll_event ev;
    ev.data.fd = listensock;
    // 相关的 fd 有数据可读的情况，包括新客户端的连接、客户端socket有数据可读和客户端socket断开三种情况
    ev.events = EPOLLIN;

    /**
     * int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
     * 
     * 用于向 epfd 实例注册，修改或移除事件
     * */
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listensock, &ev);

    while (1)
    {
        // 用于存放有事件发生的数组
        struct epoll_event events[MAXEVENTS];

        /**
         * int epoll_wait(int epfd, struct epoll_event *events,
                      int maxevents, int timeout);

         * epoll 将会阻塞，直到：
         * 1. fd 收到事件
         * 2. 调用被信号 interrupt
         * 3. 超时
         * 
         * 返回值：返回有事件发生的 fd 数量，0 表示 timeout 期间都没有事件发生，-1 error
         * */
        int readyfds = epoll_wait(epollfd, events, MAXEVENTS, -1);

        // -1 error
        if (readyfds < 0)
        {
            perror("epoll() failed");
            break;
        }
        if (readyfds == 0)
        {
            printf("timeout\n");
            continue;
        }

        // 检查有事情发生的socket，包括监听和客户端连接的socket。
        for (int i = 0; i <= readyfds; i++)
        {
            // 新的客户端连接
            if ((events[i].data.fd == listensock) && (events[i].events & EPOLLIN))
            {
                struct sockaddr_in client;
                socklen_t len = sizeof(client);

                /**
                 * It extracts the first
       connection request on the queue of pending connections for the
       listening socket, sockfd, creates a new connected socket, and
       returns a new file descriptor referring to that socket.  The
       newly created socket is not in the listening state.  The original
       socket sockfd is unaffected by this call.
                 **/

                // 从 pending 的连接队列中取出第一个给 listensock，创建一个新的已连接的 socket，并返回 fd
                int clientsock = accept(listensock, (struct sockaddr *)&client, &len);

                if (clientsock < 0)
                {
                    printf("accept() failed.\n");
                    continue;
                }

                printf("client(socket=%d) connected ok.\n", clientsock);

                memset(&ev, 0, sizeof(struct epoll_event));
                ev.data.fd = clientsock;
                ev.events = EPOLLIN;
                epoll_ctl(epollfd, EPOLL_CTL_ADD, clientsock, &ev);

                continue;    
            }
            else if(events[i].events & EPOLLIN)
            {
                // 客户端有数据过来或客户端的socket连接被断开。
                char buffer[1024];
                memset(buffer, 0, sizeof(buffer));

                // 读取客户端的数据。
                ssize_t isize = read(events[i].data.fd, buffer, sizeof(buffer));
                // 发生了错误或socket被对方关闭。
                if (isize <= 0)
                {
                    printf("client(eventfd=%d) disconnected.\n", events[i].data.fd);
                    
                    memset(&ev, 0, sizeof(struct epoll_event));
                    ev.data.fd = events[i].data.fd;
                    ev.events = EPOLLIN;
                    // 从 epollfd 实例中移除对该 fd 的事件监视
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, &ev);
                    close(events[i].data.fd);
                    continue;
                }

                printf("recv(eventfd=%d,size=%ld):%s\n", events[i].data.fd, isize, buffer);
                // 把收到的报文发回给客户端。
                write(events[i].data.fd, buffer, strlen(buffer));
            }
        }
    }

    // 别忘了最后关闭 epollfd
    close(epollfd); 

    return 0;
}

// 初始化服务端的监听端口。
int initserver(int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        printf("socket() failed.\n");
        return -1;
    }

    int opt = 1;
    unsigned int len = sizeof(opt);

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, len);
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &opt, len);

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    // INADDR_ANY is used when you don't need to bind a socket to a specific IP.
    // When you use this value as the address when calling bind(), the socket accepts connections to all the IPs of the machine.
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    // When a socket is created with socket(2), it exists in a namespace (address family) but has no address assigned to it.
    // bind() assigns the address specified by addr to the socket referred to by the file descriptor sockfd.
    if (bind(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("bind() failed.\n");
        close(sock);
        return -1;
    }

    //  listen() marks the socket referred to by sockfd as a passive socket, that is,
    // as a socket that will be used to accept incoming connection requests using accept(2).
    if (listen(sock, 5) != 0)
    {
        printf("listen() failed.\n");
        close(sock);
        return -1;
    }

    return sock;
}
/**
 * select 每次循环都会创建一份 fd_set 的拷贝，然后交由 kernel 标记，效率很低
 * select 默认只能监视 1024 个 fds，poll 没有这个限制
 * */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/fcntl.h>

#define MAXNFDS 1024

int initserver(int port);

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("usage: ./pollserverdemo port\n");
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

    /**
     * struct pollfd {
     *     int   fd;         // file descriptor 
     *     short events;     // requested events 应用关心的事件
     *     short revents;    // returned events 由 kernel 填充，真实发生的事件 filled by the kernel with the events that actually occurred.
     * };
     * 
     * */

    struct pollfd pfds[MAXNFDS];
    memset(pfds, 0 , sizeof(pfds));

    // Set up the initial listening socket 
    pfds[listensock].fd = listensock;
    // listensock 关心的是 POLLIN，有数据可读的情况，包括新客户端的连接、客户端socket有数据可读和客户端socket断开三种情况
    pfds[listensock].events = POLLIN;

    int maxfd = listensock;

    while (1)
    {
        /**
         * int poll(struct pollfd *fds, nfds_t nfds, int timeout);
         *
         * **/
        int ready = poll(pfds, maxfd+1, -1);
        if (ready == -1)
        {
            perror("poll() failed");
            break;
        }

        // 检查有事情发生的socket，包括监听和客户端连接的socket。
        for (int eventfd = 0; eventfd <= maxfd; eventfd++)
        {
            if (pfds[eventfd].fd < 0) continue;
            // 确保收到的事件为 POLLIN
            if ((pfds[eventfd].revents & POLLIN) == 0) continue;
            // 先把revents清空。
            pfds[eventfd].revents=0;

            // 如果发生事件的是listensock，表示有新的客户端连上来。
            if (eventfd == listensock)
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

                if (clientsock > MAXNFDS)
                {
                    printf("client socket > MAXNFDS\n");
                    close(clientsock);
                    continue;
                }

                // 把新的客户端连接 fd 放入数组 
                pfds[clientsock].fd = clientsock;
                pfds[clientsock].events = POLLIN;

                if (maxfd < clientsock)
                    maxfd = clientsock;

                continue;    
            }
            else
            {
                // 客户端有数据过来或客户端的socket连接被断开。
                char buffer[1024];
                memset(buffer, 0, sizeof(buffer));

                // 读取客户端的数据。
                ssize_t isize = read(eventfd, buffer, sizeof(buffer));
                // 发生了错误或socket被对方关闭。
                if (isize <= 0)
                {
                    printf("client(eventfd=%d) disconnected.\n", eventfd);
                    // 关闭客户端的socket
                    close(eventfd);
                    
                    //关闭的 fd 置为 -1
                    pfds[eventfd].fd = -1;

                    // 重新计算maxfd的值，注意，只有当eventfd==maxfd时才需要计算。
                    if (eventfd == maxfd)
                    {
                        for (int ii = maxfd; ii > 0; ii--)
                        {
                            if (pfds[ii].fd != -1)
                            {
                                maxfd = ii;
                                break;
                            }
                        }
                        printf("maxfd=%d\n", maxfd);
                    }
                    continue;
                }

                printf("recv(eventfd=%d,size=%ld):%s\n", eventfd, isize, buffer);
                // 把收到的报文发回给客户端。
                write(eventfd, buffer, strlen(buffer));
            }
        }
    }

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
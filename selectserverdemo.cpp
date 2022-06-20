/**
 * select 最多能监视的 fd 数量太少，为 1024
 * 每次调用 select，都要把 fd_set 从用户态拷贝到内核态
 * 每次都要遍历所有的 fd，随着监视的 fd 数量的增长，效率也会线性下降
 * */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/fcntl.h>

int initserver(int port);

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("usage: ./selectserverdemo port\n");
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

    // 读事件的集合，包括监听socket和客户端连接上来的socket。
    fd_set set;
    // set中socket的最大值。
    int maxfd;
    // set 置空
    FD_ZERO(&set);
    // 把用于监听的 socket 加入 set 中
    FD_SET(listensock, &set);
    maxfd = listensock;

    while (1)
    {
        // 调用select函数时，会改变socket集合的内容，所以要把socket集合保存下来，传一个临时的给select。
        fd_set tmpfdset = set;

        /**
         * int select(int nfds, fd_set *restrict readfds,
                  fd_set *restrict writefds, fd_set *restrict exceptfds,
                  struct timeval *restrict timeout);
         *
         * nfds: 三个 fd_set 中最大值 + 1
         * readfds: 这个 fd_set 中的 fd 会被监视，并被检查他们是否可读。select 方法返回后，readfds 中除了 ready 的 fd，其他的会被清除。
         *
         *
         * 返回值 infds On success, select() and pselect() return the number of file descriptors contained in the three returned descriptor sets
         * (that is, the total number of bits that are set in readfds, writefds, exceptfds).
         *
         * 原理是传入 fd_set 后当作 bitmap 处理，在函数处理后会修改传入的 fd_set
         * 如果某一位上有事件发生，则置1
         * **/

        int infds = select(maxfd + 1, &tmpfdset, NULL, NULL, NULL);

        // -1 error
        if (infds < 0)
        {
            printf("select() failed.\n");
            perror("select()");
            break;
        }
        // 超时，The return value may be zero if the timeout expired before any file descriptors became ready.
        if (infds == 0)
        {
            println("timeout");
            continue;
        }

        // 检查有事情发生的socket，包括监听和客户端连接的socket。
        for (int eventfd = 0; eventfd <= maxfd; eventfd++)
        {
            if (FD_ISSET(eventfd, &tmpfdset) <= 0)
                continue;

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

                // 把新的客户端socket加入集合。
                FD_SET(clientsock, &set);

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
                    // 从集合中移去客户端的socket。
                    FD_CLR(eventfd, &set);

                    // 重新计算maxfd的值，注意，只有当eventfd==maxfd时才需要计算。
                    if (eventfd == maxfd)
                    {
                        for (int ii = maxfd; ii > 0; ii--)
                        {
                            if (FD_ISSET(ii, &set))
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
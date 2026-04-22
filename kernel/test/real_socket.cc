// POSIX socket implementation of socket_api.h for standalone host-side testing.
// Uses the OS's real TCP stack so HttpClient can hit actual servers.

#include "kernel/net/socket_api.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int socket_create(SocketType type) {
    int kind = (type == SocketType::STREAM) ? SOCK_STREAM : SOCK_DGRAM;
    return socket(AF_INET, kind, 0);
}

int socket_connect(int sockfd, const char* host, uint16_t port) {
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);

    // 10-second send/receive timeout
    struct timeval tv{};
    tv.tv_sec = 10;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int rc = connect(sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (rc < 0) perror("[socket] connect failed");
    return rc;
}

int socket_send(int sockfd, const void* data, size_t length) {
    return (int)send(sockfd, data, length, 0);
}

int socket_receive(int sockfd, void* buffer, size_t length) {
    return (int)recv(sockfd, buffer, length, 0);
}

void socket_close(int sockfd) {
    close(sockfd);
}

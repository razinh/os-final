#pragma once

#include <stdint.h>
#include <stddef.h>

// Socket abstraction, allows testing with mock implementation or real kernel sockets

enum class SocketType {
    STREAM, // TCP
    DGRAM   // UDP
};

// Create a socket
int socket_create(SocketType type);

// Connect to remote host
int socket_connect(int sockfd, const char* host, uint16_t port);

// send data
int socket_send(int sockfd, const void* data, size_t length);

// receive data
int socket_receive(int sockfd, void* buffer, size_t length);

// close socket
void socket_close(int sockfd);
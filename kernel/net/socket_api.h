#pragma once

#include <stdint.h>
#include "lib/kstd.h"

// Socket abstraction layer so the callers use this interface without depending on the kernel TCP stack directly, 
// which lets a mock implementation to be swapped in for unit testing.
// Transport protocol selection passed to socket_create.
enum class SocketType {
    STREAM, // TCP: reliable, ordered, connection-oriented byte stream.
    DGRAM   // UDP: unreliable, connectionless datagrams.
};

// Allocates a new socket of the requested type and returns a non-negative file descriptor, or -1 on failure, 
// but since we have a single-connection kernel implementation for the TCP, the descriptor is always 0.
int socket_create(SocketType type);

// Establishes a connection to the given host (dotted-decimal IPv4) on the port and blocks until the connection reaches 
// ESTABLISHED or times out, so it will return 0 on success and -1 on failure or timeout.
int socket_connect(int sockfd, const char* host, uint16_t port);

// Sends length bytes from data over sockfd and it returns the number of bytes accepted for transmission, or -1 on 
// an error.
int socket_send(int sockfd, const void* data, size_t length);

// Reads up to length bytes from sockfd into buffer as it is received and blocks until at least one byte is available 
// or the connection closes and it returns the number of bytes read, a 0 on an EOF, or -1 on error.
int socket_recv(int sockfd, void* buffer, size_t length);

// Closing the socket initiates a shutdown of sockfd and releases any associated kernel resources with the file 
// descriptor.
void socket_close(int sockfd);
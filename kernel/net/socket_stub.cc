#include "socket_api.h"
#include "lib/kstd.h"
#include "print.h"

// Adapted from mock_socket.cc for kernel use
static bool mock_connected = false;

int socket_create(SocketType type) {
    KPRINT("[STUB] socket_create(type=?)\n", Dec((int)type));
    return 42; // Mock fd
}

int socket_connect(int sockfd, const char* host, uint16_t port) {
    KPRINT("[STUB] socket_connect(fd=?, host=?, port=?)\n", 
           Dec(sockfd), host, Dec(port));
    mock_connected = true;
    return 0; // Success
}

int socket_send(int sockfd, const void* data, size_t length) {
    KPRINT("[STUB] socket_send(fd=?, length=?)\n", Dec(sockfd), Dec(length));
    // Could print first few bytes if you want
    return static_cast<int>(length); // Pretend it sent
}

int socket_recv(int sockfd, void* buffer, size_t length) {
    KPRINT("[STUB] socket_recv(fd=?, length=?)\n", Dec(sockfd), Dec(length));
    if (!mock_connected) return -1;
    
    // Just return 0 (EOF) since we don't have mock data
    return 0;
}

void socket_close(int sockfd) {
    KPRINT("[STUB] socket_close(fd=?)\n", Dec(sockfd));
    mock_connected = false;
}
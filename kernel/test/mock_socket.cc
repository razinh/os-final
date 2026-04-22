#include "kernel/net/socket_api.h"
#include "kernel/lib/kstd.h"

// Mock HTTP responses for testing HttpClient without real network
static const char MOCK_RESPONSE[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 53\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><body><h1>Hello from Mock Server!</h1></body></html>";

static size_t mock_response_pos = 0;
static bool mock_connected = false;

int socket_create(SocketType type) {
    printf("[MOCK] socket_create(type=%d)\n", (int)type);
    return 42; // mock socket fd
}

int socket_connect(int sockfd, const char* host, uint16_t port) {
    printf("[MOCK] socket_connect(fd=%d, host=%s, port=%u)\n", sockfd, host, port);
    mock_connected = true;
    mock_response_pos = 0;
    return 0;
}

int socket_send(int sockfd, const void* data, size_t length) {
    printf("[MOCK] socket_send(fd=%d, length=%zu)\n", sockfd, length);
    printf("---\n%.*s---\n", (int)length, (const char*)data);
    return length;
}

int socket_receive(int sockfd, void* buffer, size_t length) {
    if (!mock_connected) return -1; // not connected
    
    size_t remaining = sizeof(MOCK_RESPONSE) - mock_response_pos - 1; // exclude null terminator
    if (remaining == 0) {
        printf("[MOCK] socket_receive: EOF\n");
        return 0; // EOF
    }
    size_t to_copy = (length < remaining) ? length : remaining;
    memcpy(buffer, MOCK_RESPONSE + mock_response_pos, to_copy);
    mock_response_pos += to_copy;
    printf("[MOCK] socket_receive(fd=%d, length=%zu) -> %zu bytes\n", sockfd, length, to_copy);
    return to_copy;
}

void socket_close(int sockfd) {
    printf("[MOCK] socket_close(fd=%d)\n", sockfd);
    mock_connected = false;
}
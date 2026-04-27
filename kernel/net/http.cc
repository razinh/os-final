#include "http.h"
#include "socket_api.h"
#include "machine.h"

namespace net {

HttpRequest::HttpRequest(Method method, const char* host, const char* path)
    : method_(method), host_(host), path_(path), num_headers_(0) {}

void HttpRequest::add_header(const char* key, const char* value) {
    if (num_headers_ >= 8) { // Too many headers, so ignore them rather than overflowing the fixed size array.
        return;
    }
    custom_headers_[num_headers_].key = key;
    custom_headers_[num_headers_].value = value;
    num_headers_++;
}

size_t HttpRequest::build(char* buffer, size_t buffer_size) const {
    size_t offset = 0;

    // Convert the Method enum into its string for the HTTP methods before writing the request line.
    const char* method_str = "";
    switch (method_) {
        case Method::GET:    
            method_str = "GET";    
            break;
        case Method::POST:   
            method_str = "POST";   
            break;
        case Method::PUT:    
            method_str = "PUT";    
            break;
        case Method::DELETE: 
            method_str = "DELETE"; 
            break;
        case Method::PATCH:  
            method_str = "PATCH";  
            break;
        case Method::HEAD:   
            method_str = "HEAD";   
            break;
    }
    offset += snprintf(buffer + offset, buffer_size - offset, "%s %s HTTP/1.0\r\n", method_str, path_);

    // The Host header is required and the User-Agent and Connection are always fixed.
    offset += snprintf(buffer + offset, buffer_size - offset, "Host: %s\r\n", host_);
    offset += snprintf(buffer + offset, buffer_size - offset, "User-Agent: MyOS-HTTP/1.0\r\n");
    offset += snprintf(buffer + offset, buffer_size - offset, "Connection: close\r\n");

    // Custom headers can be appended after the fixed headers since the caller wanted these.
    for (size_t i = 0; i < num_headers_; i++) {
        offset += snprintf(buffer + offset, buffer_size - offset, "%s: %s\r\n",
            custom_headers_[i].key, custom_headers_[i].value);
    }
    // Blank CLRF line that terminates the header section as a signal.
    offset += snprintf(buffer + offset, buffer_size - offset, "\r\n");
    return offset;
}

HttpResponse::HttpResponse() : status_code_(0), num_headers_(0), body_(nullptr), body_length_(0) {
    status_message_[0] = '\0';
}

bool HttpResponse::parse(const char* data, size_t length) {
    if (!data || length == 0) {
        return false;
    }

    const char* current = data;
    const char* end = data + length;

    // Parse the status line. 
    const char* line_end = strstr(current, "\r\n");
    if (!line_end) {
        return false;
    }
    char status_line[256];
    size_t line_length = line_end - current;
    if (line_length >= sizeof(status_line)) {
        return false;
    }
    memcpy(status_line, current, line_length);
    status_line[line_length] = '\0';
    if (!parse_status_line(status_line)) {
        return false;
    }
    current = line_end + 2;

    // Parse the headers by walking CRLF lines until the blank line that separates the header from the body
    // and skipping any line that is too long to fit into the local buffer.
    while (current < end) {
        line_end = strstr(current, "\r\n");
        if (!line_end) {
            break;
        }
        if (line_end == current) { // The empty line indicates the end of the headers.
            current += 2;
            break;
        }

        // Parse a header.
        line_length = line_end - current;
        if (line_length >= 256) {
            current = line_end + 2;
            continue; // Skip really long header lines.
        }
        char header_line[256];
        memcpy(header_line, current, line_length);
        header_line[line_length] = '\0';
        parse_header_line(header_line);
        current = line_end + 2;
    }

    // Parse the response body as everything after the blank line is the body so the pointer for the body
    // will be into the caller's buffer.
    if (current < end) {
        body_ = current;
        body_length_ = end - current;
    }
    return true;
}

bool HttpResponse::parse_status_line(const char* line) {

    // Formatted as "HTTP/1.X STATUS_CODE STATUS_MESSAGE", so this gets matched as the status line.
    char http_version[16];
    int code;
    char message[64];

    int matched = sscanf(line, "%15s %d %63[^\r\n]", http_version, &code, message);
    if (matched < 2) {
        return false;
    }

    status_code_ = code;
    if (matched == 3) {
        strncpy(status_message_, message, sizeof(status_message_) - 1);
        status_message_[sizeof(status_message_) - 1] = '\0';
    } else {
        status_message_[0] = '\0';
    }
    return true;
}

bool HttpResponse::parse_header_line(const char* line) {
    if (num_headers_ >= 16) {
        return false;
    }

    // Before the frst colon has everything as the key and then after is the value after stripping whitespace.
    const char* colon = strchr(line, ':');
    if (!colon) {
        return false;
    }

    // Extract the key.
    size_t key_length = colon - line;
    if (key_length >= sizeof(headers_[0].key)) return false;
    memcpy(headers_[num_headers_].key, line, key_length);
    headers_[num_headers_].key[key_length] = '\0';
    const char* value_start = colon + 1; // Skip the colon and the whitespace to get the key.
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }

    // Extract the value by fixing it in the fixed field width rather than just rejecting the header outright.
    size_t value_length = strlen(value_start);
    if (value_length >= sizeof(headers_[0].value)) {
        value_length = sizeof(headers_[0].value) - 1;
    }
    memcpy(headers_[num_headers_].value, value_start, value_length);
    headers_[num_headers_].value[value_length] = '\0';
    num_headers_++;
    return true;
}

const char* HttpResponse::get_header(const char* key) const {
    for (size_t i = 0; i < num_headers_; i++) {
        if (strcasecmp(headers_[i].key, key) == 0) { // The headers are case insensitive so just get them.
            return headers_[i].value;
        }
    }
    return nullptr;
}

void HttpResponse::print() const {
    printf("=== HTTP Response ===\n");
    printf("Status: %d %s\n", status_code_, status_message_);
    printf("\nHeaders:\n");
    for (size_t i = 0; i < num_headers_; i++) {
        printf("  %s: %s\n", headers_[i].key, headers_[i].value);
    }
    printf("\nBody (%zu bytes):\n", body_length_);
    if (body_ && body_length_ > 0) {
        printf("%.*s\n", (int)body_length_, body_);
    }
    printf("=====================\n");
}

// The HTTPClient calls do_request() to build the request and read into response buffer and TCP handling.
int HttpClient::get(const char* host, uint16_t port, const char* path, char* response_buffer, size_t buffer_size) {
    return do_request(HttpRequest::Method::GET, host, port, path, nullptr, 0, response_buffer, buffer_size);
}

int HttpClient::post(const char* host, uint16_t port, const char* path, const char* body, size_t body_length, char* response_buffer, size_t buffer_size) {
    return do_request(HttpRequest::Method::POST, host, port, path, body, body_length, response_buffer, buffer_size);
}

int HttpClient::put(const char* host, uint16_t port, const char* path, const char* body, size_t body_length, char* response_buffer, size_t buffer_size) {
    return do_request(HttpRequest::Method::PUT, host, port, path, body, body_length, response_buffer, buffer_size);
}

int HttpClient::delete_request(const char* host, uint16_t port, const char* path, char* response_buffer, size_t buffer_size) {
    return do_request(HttpRequest::Method::DELETE, host, port, path, nullptr, 0, response_buffer, buffer_size);
}

int HttpClient::patch(const char* host, uint16_t port, const char* path, const char* body, size_t body_length, char* response_buffer, size_t buffer_size) {
    return do_request(HttpRequest::Method::PATCH, host, port, path, body, body_length, response_buffer, buffer_size);
}

int HttpClient::do_request(HttpRequest::Method method, const char* host, uint16_t port, const char* path, 
    const char* request_body, size_t request_body_length, char* response_buffer, size_t buffer_size) {

    // A content length array is needed as it has to outlive the request.build() since the header stores a pointer
    // into it.
    char content_length[32] = {};
    HttpRequest request(method, host, path);
    if (request_body && request_body_length > 0) {
        snprintf(content_length, sizeof(content_length), "%zu", request_body_length);
        request.add_header("Content-Length", content_length);
        request.add_header("Content-Type", "application/x-www-form-urlencoded");
    }

    char request_buffer[2048];
    size_t request_length = request.build(request_buffer, sizeof(request_buffer));

    // Append the body to the same buffer directly so the headers and body arrive in the same TCP segment.
    // The nic_helper proxy waits for the full response after the first TCP PSH, so splitting into two sends would \
    // stall and leave the server waiting for the body.
    if (request_body && request_body_length > 0 &&
        request_length + request_body_length < sizeof(request_buffer)) {
        memcpy(request_buffer + request_length, request_body, request_body_length);
        request_length += request_body_length;
    }

    // Create the socket stream.
    int sock = socket_create(SocketType::STREAM);
    if (sock < 0) {
        return -1;
    }

    // Connect to the server.
    if (socket_connect(sock, host, port) < 0) {
        socket_close(sock);
        return -1;
    }

    // Send a complete request (headers and body) in one call.
    if (socket_send(sock, request_buffer, request_length) < 0) {
        socket_close(sock);
        return -1;
    }

    // Receive a response by reading until the server closes the connection or the buffer is full,
    // and then null terminate so the callers can treat the response as a C string.
    size_t total_received = 0;
    while (total_received < buffer_size) {
        int n = socket_recv(sock, response_buffer + total_received, buffer_size - total_received - 1);
        if (n < 0) {
            socket_close(sock);
            return -1;
        } else if (n == 0) {
            break; // Connection closed.
        }
        total_received += n;
    }
    response_buffer[total_received] = '\0'; // Null-terminate the response.
    socket_close(sock);
    return total_received;
}

} // namespace net
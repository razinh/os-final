#pragma once

#include <stdint.h>
#include "lib/kstd.h"

namespace net {

class HttpRequest {
public:

    // Supported HTTP methods for out requests.
    enum class Method {
        GET,
        POST,
        PUT,
        DELETE,
        PATCH,
        HEAD
    };

    HttpRequest(Method method, const char* host, const char* path);

    // Add a custom header to the request.
    void add_header(const char* key, const char* value);

    // Build the final request string.
    size_t build(char* buffer, size_t buffer_size) const;

private:
    Method method_;
    const char* host_;
    const char* path_;

    struct Header {
        const char* key;
        const char* value;
    };
    Header custom_headers_[8];
    size_t num_headers_;
};

class HttpResponse {
public:
    HttpResponse();

    // Parse the HHTP response from a buffer in a single pass, so the status line, headers, and body after walking the
    // raw response bytes.
    bool parse(const char* data, size_t length);

    // Accessor variables for the response parts.
    int status_code() const { return status_code_; };
    const char* status_message() const { return status_message_; }
    const char* body() const { return body_; }
    size_t body_length() const { return body_length_; }
    const char* get_header(const char* key) const; // Specific header can be accessed.
    void print() const;

private:
    int status_code_;
    char status_message_[64];

    // Store the headers with keys and values from the raw response buffer.
    struct Header {
        char key[64];
        char value[256];
    };
    Header headers_[16];
    size_t num_headers_;

    const char* body_;
    size_t body_length_;

    // Parses "HTTP/1.X CODE REASON" from a null-terminated line and stores the code and reason into its member fields.
    bool parse_status_line(const char* line);

    // Parses a single key and value header line and appends it to headers_.
    bool parse_header_line(const char* line);
};

class HttpClient {
public:

    // Sends an HTTP response based on the method to the host:port/path and writes the raw response into the response 
    // buffer, and the number of bytes recieved is returned or -1 if there is an error. Any method that passes a body
    // into do_request() get a Content-Length and Content-Type headers added to them automatically.
    static int get(const char* host, uint16_t port, const char* path,
                   char* response_buffer, size_t buffer_size);

    static int post(const char* host, uint16_t port, const char* path,
                    const char* body, size_t body_length,
                    char* response_buffer, size_t buffer_size);

    static int put(const char* host, uint16_t port, const char* path,
                   const char* body, size_t body_length,
                   char* response_buffer, size_t buffer_size);

    static int delete_request(const char* host, uint16_t port, const char* path,
                              char* response_buffer, size_t buffer_size);

    static int patch(const char* host, uint16_t port, const char* path,
                     const char* body, size_t body_length,
                     char* response_buffer, size_t buffer_size);

private:

    // Shared for all of the HTTP response methods and it builds a request, opens a socket, sends headers
    // and body by write and reads the response into the response buffer and then the body is added to the header
    // buffer before sending so the TCP segment can have both the headers and body. This is needed because the
    // NIC helper proxy might stall while waiting for the body after the first TCP PSH (push) flag that will
    // force immediate data transmission and delivery to the application and bypass TCP buffering to reduce
    // latency. This will return -1 if error or the byte count.
    static int do_request(HttpRequest::Method method, const char* host,
                          uint16_t port, const char* path,
                          const char* request_body, size_t request_body_length,
                          char* response_buffer, size_t buffer_size);
};

} // namespace net
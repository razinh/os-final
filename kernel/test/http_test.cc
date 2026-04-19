#include "kernel/net/http.h"
#include <stdio.h>
#include <string.h>

using namespace net;

void test_request_builder() {
    printf("\n=== Test HttpRequest Builder ===\n");

    HttpRequest req(HttpRequest::Method::GET, "example.com", "/test.html");
    req.add_header("Accept", "text/html");
    req.add_header("Accept-Language", "en-US");

    char buffer[1024];
    size_t len = req.build(buffer, sizeof(buffer));

    printf("Built request (%zu bytes):\n%s\n", len, buffer);
}

void test_response_parser() {
    printf("\n=== Test: HTTP Response Parser ===\n");
    
    const char* test_response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 27\r\n"
        "Server: TestServer/1.0\r\n"
        "\r\n"
        "{\"status\":\"success\",\"id\":42}";
    
    HttpResponse resp;
    if (resp.parse(test_response, strlen(test_response))) {
        printf("✓ Parse successful\n");
        resp.print();
        
        printf("\nTesting header lookup:\n");
        printf("Content-Type: %s\n", resp.get_header("Content-Type"));
        printf("Server: %s\n", resp.get_header("Server"));
        printf("Missing-Header: %s\n", 
               resp.get_header("Missing-Header") ? 
               resp.get_header("Missing-Header") : "(null)");
    } else {
        printf("✗ Parse failed\n");
    }
}

void test_http_client() {
    printf("\n=== Test: HTTP Client (with mock socket) ===\n");
    
    char response[4096];
    int len = HttpClient::get("10.0.2.2", 80, "/index.html", 
                             response, sizeof(response));
    
    if (len > 0) {
        printf("✓ Received %d bytes\n", len);
        
        HttpResponse parsed;
        if (parsed.parse(response, len)) {
            parsed.print();
        }
    } else {
        printf("✗ Request failed\n");
    }
}

int main() {
    printf("HTTP Client Test Suite\n");
    printf("======================\n");
    
    test_request_builder();
    test_response_parser();
    test_http_client();
    
    printf("\n✓ All tests complete\n");
    return 0;
}
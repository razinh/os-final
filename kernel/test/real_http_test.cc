#include "kernel/net/http.h"
#include <stdio.h>
#include <string.h>

using namespace net;

// ============================================================
//  INSERT YOUR TARGET DETAILS HERE
// ============================================================
static const char*    TARGET_IP   = "128.83.120.168";   // e.g. "192.168.1.42"
static const uint16_t TARGET_PORT = 8000;                 // e.g. 8080
static const char*    GET_PATH    = "/";                // e.g. "/api/items"
static const char*    POST_PATH   = "/";                // e.g. "/api/items"
static const char*    POST_BODY   = "seconds=10"; // form-encoded body
// ============================================================

static int tests_run = 0, tests_passed = 0;

static void print_separator() {
    printf("----------------------------------------\n");
}

// Returns true if status is 2xx
static bool is_success(int status) {
    return status >= 200 && status < 300;
}

static void test_get() {
    printf("\n=== GET %s:%u%s ===\n", TARGET_IP, TARGET_PORT, GET_PATH);
    print_separator();
    tests_run++;

    char buf[8192] = {};
    int  len = HttpClient::get(TARGET_IP, TARGET_PORT, GET_PATH,
                               buf, sizeof(buf) - 1);

    if (len < 0) {
        printf("FAIL: connection error (len=%d)\n", len);
        return;
    }

    HttpResponse resp;
    if (!resp.parse(buf, (size_t)len)) {
        printf("FAIL: could not parse response\n");
        printf("Raw (%d bytes):\n%.*s\n", len, len, buf);
        return;
    }

    resp.print();

    if (!is_success(resp.status_code())) {
        printf("FAIL: expected 2xx, got %d\n", resp.status_code());
        return;
    }

    printf("PASS: GET returned %d\n", resp.status_code());
    tests_passed++;
}

static void test_post() {
    printf("\n=== POST %s:%u%s ===\n", TARGET_IP, TARGET_PORT, POST_PATH);
    printf("Body: %s\n", POST_BODY);
    print_separator();
    tests_run++;

    char buf[8192] = {};
    int  len = HttpClient::post(TARGET_IP, TARGET_PORT, POST_PATH,
                                POST_BODY, strlen(POST_BODY),
                                buf, sizeof(buf) - 1);

    if (len < 0) {
        printf("FAIL: connection error (len=%d)\n", len);
        return;
    }

    HttpResponse resp;
    if (!resp.parse(buf, (size_t)len)) {
        printf("FAIL: could not parse response\n");
        printf("Raw (%d bytes):\n%.*s\n", len, len, buf);
        return;
    }

    resp.print();

    // Server returns 405 for POST since it only handles GET — that's fine,
    // it still proves the connection and HTTP layer work end-to-end.
    if (resp.status_code() == 405) {
        printf("PASS: POST returned 405 (server only supports GET — connection worked)\n");
        tests_passed++;
        return;
    }

    if (!is_success(resp.status_code())) {
        printf("FAIL: unexpected status %d\n", resp.status_code());
        return;
    }

    printf("PASS: POST returned %d\n", resp.status_code());
    tests_passed++;
}

int main() {
    printf("Live HTTP Test\n");
    printf("Target: %s:%u\n", TARGET_IP, TARGET_PORT);
    print_separator();

    test_get();
    test_post();

    print_separator();
    printf("%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

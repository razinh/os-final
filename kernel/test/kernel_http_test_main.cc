// Kernel-integrated HTTP test entry point.
// Replaces kernel_main.cc: initialises the network stack, issues one GET and
// one POST to TARGET_IP:TARGET_PORT, prints *** PASS / *** FAIL, then shuts
// the kernel down cleanly.
//
// Build + run:
//   make -f Makefile.kernel_http          # build the bootable image
//   make -f Makefile.kernel_http run      # boot in QEMU (serial → stdout)

// Use full "kernel/..." paths so we get the real headers, not the no-op stubs
// that live in kernel/test/ (print.h, machine.h, etc.).
#include "kernel/net/http.h"
#include "kernel/net/net_init.h"
#include "kernel/print.h"
#include "kernel/machine.h"   // strlen

// ============================================================
//  FILL IN YOUR TARGET DETAILS
// ============================================================
static const char*    TARGET_IP   = "128.83.120.168";   // e.g. "192.168.1.42"
static const uint16_t TARGET_PORT = 8000;                 // e.g. 8080
static const char*    GET_PATH    = "/";                // e.g. "/api/items"
static const char*    POST_PATH   = "/";                // e.g. "/api/items"
static const char*    POST_BODY   = "seconds=1";         // form-encoded or JSON body
// ============================================================

static int tests_run    = 0;
static int tests_passed = 0;

static void test_get() {
    tests_run++;
    SAY("GET ?:?\n", TARGET_IP, Dec(TARGET_PORT));

    char buf[8192] = {};
    int  len = net::HttpClient::get(TARGET_IP, TARGET_PORT, GET_PATH,
                                    buf, sizeof(buf) - 1);
    if (len < 0) {
        SAY("FAIL: GET connection error (len=?)\n", Dec(len));
        return;
    }

    net::HttpResponse resp;
    if (!resp.parse(buf, (size_t)len)) {
        KPRINT("*** FAIL: GET could not parse response\n");
        return;
    }

    int s = resp.status_code();
    if (s >= 200 && s < 300) {
        SAY("PASS: GET returned ?\n", Dec(s));
        tests_passed++;
    } else {
        SAY("FAIL: GET returned ?\n", Dec(s));
    }
}

static void test_post() {
    tests_run++;
    SAY("POST ?:?\n", TARGET_IP, Dec(TARGET_PORT));

    char buf[8192] = {};
    int  len = net::HttpClient::post(TARGET_IP, TARGET_PORT, POST_PATH,
                                     POST_BODY, strlen(POST_BODY),
                                     buf, sizeof(buf) - 1);
    if (len < 0) {
        SAY("FAIL: POST connection error (len=?)\n", Dec(len));
        return;
    }

    net::HttpResponse resp;
    if (!resp.parse(buf, (size_t)len)) {
        KPRINT("*** FAIL: POST could not parse response\n");
        return;
    }

    int s = resp.status_code();
    if (s >= 200 && s < 300) {
        SAY("PASS: POST returned ?\n", Dec(s));
        tests_passed++;
    } else {
        SAY("FAIL: POST returned ?\n", Dec(s));
    }
}

void kernel_main() {
    net::net_init();

    test_get();
    test_post();

    SAY("? / ? tests passed\n", Dec(tests_passed), Dec(tests_run));
    shutdown(true);
}

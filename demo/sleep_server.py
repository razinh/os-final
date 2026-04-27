# justs sleeps for X amount of time

import http.server
import socketserver
import urllib.parse
import time
import socket

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "0.0.0.0"

class SleepHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length).decode('utf-8')
        parsed_params = urllib.parse.parse_qs(post_data)
        seconds_str = parsed_params.get('seconds', [None])[0]

        if seconds_str and seconds_str.isdigit():
            seconds = int(seconds_str)
            print(f"Sleeping for {seconds} seconds...")
            
            time.sleep(seconds)
            
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(f"Done sleeping for {seconds} seconds.".encode('utf-8'))
        else:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"Error: Please provide an integer 'seconds' parameter.")

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()
        self.wfile.write(b"Send a POST with 'seconds=X' to make me sleep.")

PORT = 8000
HOST = get_local_ip()

socketserver.TCPServer.allow_reuse_address = True

with socketserver.TCPServer((HOST, PORT), SleepHandler) as httpd:
    print(f"Sleep Server started at http://{HOST}:{PORT}")
    print(f"Example: curl -X POST -d 'seconds=5' http://{HOST}:{PORT}")
    httpd.serve_forever()

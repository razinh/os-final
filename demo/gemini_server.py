import os
import http.server
import socketserver
import urllib.parse
import google.generativeai as genai

API_KEY = "INSERT API KEY HERE"
genai.configure(api_key=API_KEY)
model = genai.GenerativeModel('gemini-2.5-flash-lite')

def get_local_ip():
    try:
        s = http.server.socket.socket(http.server.socket.AF_INET, http.server.socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "0.0.0.0"

class GeminiHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length).decode('utf-8')
        
        parsed_params = urllib.parse.parse_qs(post_data)
        
        user_msg = parsed_params.get('msg', [None])[0]

        if user_msg:
            try:
                response = model.generate_content(user_msg)
                print(response)
                self.send_response(200)
                self.send_header('Content-type', 'text/plain')
                self.end_headers()
                self.wfile.write(response.text.encode('utf-8'))
            except Exception as e:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"Gemini Error: {str(e)}".encode('utf-8'))
        else:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"Error: Missing 'msg' parameter")

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()
        self.wfile.write(b"Server is running. Send a POST request with 'msg'.")

PORT = 8000
HOST = get_local_ip()

with socketserver.TCPServer((HOST, PORT), GeminiHandler) as httpd:
    print(f"Server started at http://{HOST}:{PORT}")
    print(f"Example: curl -X POST -d 'msg=Hello' http://{HOST}:{PORT}")
    httpd.serve_forever()

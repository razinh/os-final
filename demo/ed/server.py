"""
Small HTTP bridge:
1) listens for incoming messages on localhost
2) posts each message to Ed as a new thread

Environment variables:
- ED_API_TOKEN (required unless hardcoded in your local copy)
- ED_COURSE_ID (optional, default: 93170)
- ED_HOST (optional, default: 127.0.0.1)
- ED_PORT (optional, default: 8080)
- ED_THREAD_TYPE (optional, default: post)
- ED_CATEGORY (optional, default: General)
"""

import asyncio
import html
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs

import edpy


def _to_ed_content_xml(text: str) -> str:
    stripped = (text or "").strip()
    if stripped.startswith("<document"):
        return stripped
    escaped = html.escape(stripped)
    return f'<document version="2.0"><paragraph>{escaped}</paragraph></document>'


async def create_thread(client: edpy.EdClient, course_id: int, title: str, content: str) -> dict:
    payload = {
        "type": os.getenv("ED_THREAD_TYPE", "post"),
        "title": title,
        "content": _to_ed_content_xml(content),
        "category": os.getenv("ED_CATEGORY", "General"),
        "subcategory": "",
        "subsubcategory": "",
        "is_pinned": False,
        "is_private": False,
        "is_anonymous": False,
        "is_megathread": False,
        "anonymous_comments": False,
    }

    endpoint = f"https://us.edstem.org/api/courses/{course_id}/threads"
    async with client._transport._session.request(
        method="POST",
        url=endpoint,
        headers={"Authorization": client._transport.ed_token},
        json={"thread": payload},
    ) as resp:
        data = await resp.json(content_type=None)
        if resp.status != 200:
            raise RuntimeError(f"Ed API error {resp.status}: {data}")
        return data


class Handler(BaseHTTPRequestHandler):
    def _json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path == "/":
            body = b"EdStem API"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self._json(404, {"error": "Not found"})

    def do_POST(self) -> None:
        if self.path != "/message":
            self._json(404, {"error": "Not found"})
            return

        raw_length = self.headers.get("Content-Length", "0")
        content_length = int(raw_length) if raw_length.isdigit() else 0
        body_bytes = self.rfile.read(content_length) if content_length > 0 else b""
        body_text = body_bytes.decode("utf-8", errors="replace")
        content_type = (self.headers.get("Content-Type") or "").lower()

        payload = {}
        if "application/json" in content_type:
            try:
                payload = json.loads(body_text) if body_text else {}
            except json.JSONDecodeError:
                self._json(400, {"error": "Invalid JSON body"})
                return
        else:
            parsed = parse_qs(body_text, keep_blank_values=True)
            payload = {key: values[-1] if values else "" for key, values in parsed.items()}

        message = payload.get("message") or payload.get("msg")
        title = payload.get("title", "Message from local server")
        if not message:
            self._json(400, {"error": "Missing 'message' (or 'msg') in request body"})
            return

        try:
            future = asyncio.run_coroutine_threadsafe(
                create_thread(
                    client=self.server.ed_client,
                    course_id=self.server.course_id,
                    title=title,
                    content=message,
                ),
                self.server.ed_loop,
            )
            result = future.result(timeout=30)
        except RuntimeError as error:
            self._json(400, {"error": str(error)})
            return
        except Exception as error:
            self._json(500, {"error": f"Unexpected server error: {error}"})
            return

        thread = result.get("thread", {})
        self._json(200, {"ok": True, "thread_id": thread.get("id"), "thread_title": thread.get("title")})

    def log_message(self, format: str, *args) -> None:
        return


async def _create_client() -> edpy.EdClient:
    token = os.getenv("ED_API_TOKEN", "AlGW-d.2ELW8lzgHpHQp5l2e3btPOgpR9a0nDPgsLttzxpj") #this api key is dead
    client = edpy.EdClient(ed_token=token)
    await client._login()
    return client


def _start_background_loop(loop: asyncio.AbstractEventLoop) -> None:
    asyncio.set_event_loop(loop)
    loop.run_forever()


def main() -> None:
    host = os.getenv("ED_HOST", "127.0.0.1")
    port = int(os.getenv("ED_PORT", "8000"))
    course_id = int(os.getenv("ED_COURSE_ID", "93170"))

    ed_loop = asyncio.new_event_loop()
    loop_thread = threading.Thread(target=_start_background_loop, args=(ed_loop,), daemon=True)
    loop_thread.start()
    client = asyncio.run_coroutine_threadsafe(_create_client(), ed_loop).result(timeout=30)

    server = HTTPServer((host, port), Handler)
    server.ed_client = client
    server.course_id = course_id
    server.ed_loop = ed_loop

    print(f"Listening on http://{host}:{port}")
    try:
        server.serve_forever()
    finally:
        ed_loop.call_soon_threadsafe(ed_loop.stop)
        loop_thread.join(timeout=2)


if __name__ == "__main__":
    main()


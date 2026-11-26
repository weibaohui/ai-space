#!/usr/bin/env python3
"""Self-contained automated runner for test_git_merge using a local git-http-backend server."""

import http.server
import os
import shutil
import socketserver
import subprocess
import sys
import tempfile
import urllib.parse


def find_git_http_backend() -> str:
    exec_path = subprocess.check_output(["git", "--exec-path"], text=True).strip()
    backend = os.path.join(exec_path, "git-http-backend")
    if not os.path.isfile(backend):
        raise RuntimeError(f"git-http-backend not found at {backend}")
    return backend


class GitHTTPHandler(http.server.BaseHTTPRequestHandler):
    backend_bin: str = ""
    project_root: str = ""

    def log_message(self, format: str, *args) -> None:  # noqa: A002
        # Keep test output clean unless debugging.
        if os.environ.get("GIT_TEST_HTTP_VERBOSE"):
            super().log_message(format, *args)

    def _handle_git(self) -> None:
        parsed = urllib.parse.urlsplit(self.path)
        content_len = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(content_len) if content_len > 0 else b""

        env = os.environ.copy()
        env["GIT_PROJECT_ROOT"] = self.project_root
        env["GIT_HTTP_EXPORT_ALL"] = "1"
        env["REQUEST_METHOD"] = self.command
        env["PATH_INFO"] = parsed.path
        env["QUERY_STRING"] = parsed.query
        env["REMOTE_USER"] = "test"
        if "Content-Type" in self.headers:
            env["CONTENT_TYPE"] = self.headers["Content-Type"]
        if content_len > 0:
            env["CONTENT_LENGTH"] = str(content_len)

        proc = subprocess.run(
            [self.backend_bin],
            input=body,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
            check=False,
        )
        if proc.returncode != 0:
            self.send_error(500, proc.stderr.decode("utf-8", errors="replace"))
            return

        raw = proc.stdout
        sep = b"\r\n\r\n"
        idx = raw.find(sep)
        if idx == -1:
            sep = b"\n\n"
            idx = raw.find(sep)
        if idx == -1:
            self.send_error(500, "Malformed CGI response from git-http-backend")
            return

        header_block = raw[:idx].decode("iso-8859-1")
        payload = raw[idx + len(sep) :]

        status_code = 200
        headers = []
        for line in header_block.splitlines():
            if ":" not in line:
                continue
            k, v = line.split(":", 1)
            k_norm = k.strip()
            v_norm = v.strip()
            if k_norm.lower() == "status":
                status_code = int(v_norm.split()[0])
            else:
                headers.append((k_norm, v_norm))

        self.send_response(status_code)
        for k, v in headers:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        self._handle_git()

    def do_POST(self) -> None:
        self._handle_git()


class ReusableThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def main() -> int:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    test_bin = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo_root, "test_git_merge")
    if not os.path.isfile(test_bin):
        print(f"ERROR: test binary not found: {test_bin}", file=sys.stderr)
        return 1

    backend_bin = find_git_http_backend()
    tmp_dir = tempfile.mkdtemp(prefix="skillboard-git-merge-")
    try:
        bare_repo = os.path.join(tmp_dir, "remote.git")
        subprocess.run(["git", "init", "--bare", "-b", "main", bare_repo], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["git", "-C", bare_repo, "config", "http.receivepack", "true"], check=True)

        GitHTTPHandler.backend_bin = backend_bin
        GitHTTPHandler.project_root = tmp_dir
        server = ReusableThreadingHTTPServer(("127.0.0.1", 0), GitHTTPHandler)
        port = server.server_address[1]

        import threading

        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f"http://127.0.0.1:{port}/remote.git"
            fixture_dir = os.path.join(tmp_dir, "fixture")
            proc = subprocess.run([test_bin, url, fixture_dir], check=False)
            return proc.returncode
        finally:
            server.shutdown()
            server.server_close()
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

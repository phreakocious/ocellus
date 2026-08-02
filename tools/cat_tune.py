#!/usr/bin/env python3
"""cat_tune -- procedural-cat tuning rig (spec 2026-07-28). stdlib only.

Usage:  python3 tools/cat_tune.py [port]      (default 8484)
Then open http://localhost:8484/  (localhost, same workflow config.html needs).
Rebuilds the host renderer with `c++` whenever cat_proc.h or cat_tune_main.cpp change.
"""
import hashlib, http.server, os, subprocess, sys, tempfile, urllib.parse

ROOT = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(ROOT, 'cat_tune_main.cpp')
HDR  = os.path.join(ROOT, '..', 'cat_proc.h')
BIN  = os.path.join(tempfile.gettempdir(), 'cat_tune_bin')
REFS = os.path.join(ROOT, '..', 'references', 'chibi_kawaii')

def build():
    """Recompile if the sources changed. Returns compiler stderr on failure, else None.

    Keyed on content, NOT mtime: cat_proc.h lives on a volume that reports whole-second
    mtimes while BIN sits on APFS with sub-second ones, so `mtime(BIN) >= mtime(HDR)` was
    true for any edit landing in the same second as the last build -- the rig silently
    served the previous cat, one edit behind. Hashing two small files costs nothing next
    to the compile it guards.
    """
    key = hashlib.sha256(open(MAIN, 'rb').read() + open(HDR, 'rb').read()).hexdigest()
    if key == build.key and os.path.exists(BIN):
        return None
    r = subprocess.run(['c++', '-O2', '-std=gnu++17', '-o', BIN, MAIN],
                       capture_output=True, text=True)
    if r.returncode:
        return r.stderr
    build.key = key
    return None
build.key = None

class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == '/':
            with open(os.path.join(ROOT, 'cat_tune.html'), 'rb') as f:
                body = f.read()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(body)
        elif u.path == '/render':
            err = build()
            if err:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(err.encode())
                return
            args = [f'{k}={v[0]}' for k, v in urllib.parse.parse_qs(u.query).items()]
            r = subprocess.run([BIN] + args, capture_output=True)
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.end_headers()
            self.wfile.write(r.stdout)
        elif u.path == '/refs':
            # Art references, shown as a strip beside the canvas so proportion calls are made
            # against them instead of from memory. REFS is gitignored, so an empty list is the
            # normal case for a fresh clone and the page must render fine without it.
            try:
                names = sorted(f for f in os.listdir(REFS)
                               if f.lower().endswith(('.png', '.jpg', '.jpeg', '.webp', '.gif')))
            except OSError:
                names = []
            body = '\n'.join(names).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain; charset=utf-8')
            self.end_headers()
            self.wfile.write(body)
        elif u.path == '/ref':
            # basename() the request: this serves a directory of arbitrary files to a browser,
            # so a traversal here would happily hand out anything readable by the process.
            name = os.path.basename(urllib.parse.parse_qs(u.query).get('f', [''])[0])
            path = os.path.join(REFS, name)
            if not name or not os.path.isfile(path):
                self.send_response(404); self.end_headers(); return
            with open(path, 'rb') as f:
                body = f.read()
            ext = os.path.splitext(name)[1].lower().lstrip('.')
            self.send_response(200)
            self.send_header('Content-Type',
                             'image/' + ('jpeg' if ext in ('jpg', 'jpeg') else ext))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *a):   # keep the terminal readable
        pass

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8484
    print(f'cat_tune: http://localhost:{port}/')
    http.server.ThreadingHTTPServer(('127.0.0.1', port), H).serve_forever()

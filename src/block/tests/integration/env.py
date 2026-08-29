#!/usr/bin/env python3
"""Test environment for the Block integration suite.

Starts a real DDNet-Server with a Block config, real headless clients, an
econ connection for rcon, and a stub HTTP server standing in for Discord / the
VPN providers / Agones. Everything is driven the way a player or an admin would
drive it, and assertions are made against the server log, the client log and the
database.

Follows the process/FIFO/log-waiting approach of scripts/integration_test.py.
"""

import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))

SERVER_BIN = os.environ.get("BLOCK_SERVER_BIN", os.path.join(REPO, "build-verify", "DDNet-Server"))
CLIENT_BIN = os.environ.get("BLOCK_CLIENT_BIN", os.path.join(REPO, "build-client", "DDNet"))

DB_NAME = os.environ.get("BLOCK_TEST_DB", "block_itest")
DB_USER = os.environ.get("BLOCK_TEST_DB_USER", "teeworlds")
DB_PASS = os.environ.get("BLOCK_TEST_DB_PASS", "root")
DB_HOST = os.environ.get("BLOCK_TEST_DB_HOST", "127.0.0.1")
DB_PORT = os.environ.get("BLOCK_TEST_DB_PORT", "3306")

TEST_MAP = "blmapV3ROYAL"


class TestFailure(Exception):
    pass


def free_port(kind=socket.SOCK_DGRAM):
    with socket.socket(socket.AF_INET, kind) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# --------------------------------------------------------------------------
# database
# --------------------------------------------------------------------------


def mysql(sql, database=DB_NAME, check=True):
    cmd = ["mysql", "-u", DB_USER, f"-p{DB_PASS}", "-h", DB_HOST, "-P", DB_PORT, "-N", "-B"]
    if database:
        cmd.append(database)
    r = subprocess.run(cmd, input=sql, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise TestFailure(f"mysql failed: {r.stderr.strip()}\n{sql}")
    return [line.split("\t") for line in r.stdout.strip().split("\n") if line]


def drop_database():
    """Remove the database itself, not just its tables.

    Used to reproduce a genuine first startup, where the server is handed
    credentials and nothing else.
    """
    mysql(f"DROP DATABASE IF EXISTS `{DB_NAME}`;", database=None)


def database_exists():
    rows = mysql("SHOW DATABASES;", database=None)
    return any(r[0] == DB_NAME for r in rows)


def reset_database():
    """Empty the database completely.

    Nothing is loaded back in: the server creates its own schema on startup, so
    every test in the suite also exercises that path rather than trusting a dump
    that could drift away from what the code expects.
    """
    tables = [row[0] for row in mysql("SHOW TABLES;")]
    if tables:
        mysql("SET FOREIGN_KEY_CHECKS=0; DROP TABLE " + ", ".join(f"`{t}`" for t in tables) + "; SET FOREIGN_KEY_CHECKS=1;")


def load_reference_schema(*, strip_columns=()):
    """Load sql/schema.sql, optionally without some columns.

    Only used to build a database that looks like an older install, so the
    startup migrations have something to upgrade.
    """
    path = os.path.join(REPO, "src", "block", "sql", "schema.sql")
    with open(path) as f:
        sql = f.read()
    if strip_columns:
        kept = [l for l in sql.splitlines() if not any(f"`{c}`" in l for c in strip_columns)]
        # dropping the last field of a CREATE TABLE leaves its comma dangling
        for i, line in enumerate(kept[:-1]):
            if line.rstrip().endswith(",") and kept[i + 1].lstrip().startswith(")"):
                kept[i] = line.rstrip()[:-1]
        sql = "\n".join(kept)
    proc = subprocess.run(
        ["mysql", "-u", DB_USER, f"-p{DB_PASS}", "-h", DB_HOST, "-P", DB_PORT, DB_NAME],
        input=sql, capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise TestFailure(f"could not load the reference schema: {proc.stderr.strip()}")


# --------------------------------------------------------------------------
# processes
# --------------------------------------------------------------------------


class Process:
    """A child process whose stdout is tailed into a list of lines."""

    def __init__(self, name, argv, cwd, env=None):
        self.name = name
        self.lines = []
        self._lock = threading.Lock()
        self.proc = subprocess.Popen(
            argv, cwd=cwd, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL, text=True, bufsize=1,
        )
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def _pump(self):
        for line in self.proc.stdout:
            with self._lock:
                self.lines.append(line.rstrip("\n"))

    def snapshot(self):
        with self._lock:
            return list(self.lines)

    def mark(self):
        """Index to search from, so waits only look at what happens next."""
        with self._lock:
            return len(self.lines)

    def wait_for(self, pattern, timeout=10.0, start=0):
        rx = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            for line in self.snapshot()[start:]:
                if rx.search(line):
                    return line
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise TestFailure(
            f"{self.name}: timed out waiting for /{pattern}/\n"
            + "\n".join("    " + l for l in self.snapshot()[start:][-25:])
        )

    def find(self, pattern, start=0):
        rx = re.compile(pattern)
        return [l for l in self.snapshot()[start:] if rx.search(l)]

    def assert_absent(self, pattern, start=0):
        hits = self.find(pattern, start)
        if hits:
            raise TestFailure(f"{self.name}: expected no /{pattern}/, got:\n" + "\n".join("    " + h for h in hits))

    def alive(self):
        return self.proc.poll() is None

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass


ZONE_MAP = "block_zones"


def zone_map_dir(env):
    """The maps directory the server reads generated maps from.

    It sits inside the run's own temp directory, which storage.cfg puts on the
    search path as $CURRENTDIR. Writing into the repo's data/ instead would
    leave a file behind that CMake then reports as missing from its DATA list.
    """
    maps = os.path.join(env.tmp, "maps")
    os.makedirs(maps, exist_ok=True)
    return maps


def ensure_zone_map(env):
    """Generate the map that carries the Block zone quad layers.

    None of the stock maps has a "game_zones" group, so without this a 1on1
    cannot start and events have no spawn positions.
    """
    import map

    return map.write(os.path.join(zone_map_dir(env), f"{ZONE_MAP}.map"))


class Server(Process):
    def __init__(self, env, extra_config=(), use_sql=True, map_name=None):
        self.env = env
        self.port = free_port()
        self.econ_port = free_port(socket.SOCK_STREAM)
        self.econ_password = "itest"
        cfg = [
            f"sv_port {self.port}",
            'sv_name "Block integration test"',
            f'sv_map "{map_name or TEST_MAP}"',
            "sv_register 0",
            'sv_rcon_password "itest"',
            "sv_max_clients 16",
            "sv_max_clients_per_ip 16",
            f"ec_port {self.econ_port}",
            # 'localhost' resolves to ::1 first, which makes the port we probed
            # for on 127.0.0.1 meaningless; pin it.
            'ec_bindaddr "127.0.0.1"',
            f'ec_password "{self.econ_password}"',
            "ec_bantime 0",
            "ec_output_level 2",
            f'sv_whois_db_path "{os.path.join(env.tmp, "whois.sqlite")}"',
            "sv_vpn_enabled 0",
            "sv_account_system 1",
            "sv_1on1_system 1",
            "sv_shop_server 0",
            "sv_debug_sql 1",
            "sv_inactivekick 0",
            "sv_spamprotection 0",
            "sv_test_cmds 1",
        ]
        if use_sql:
            cfg += [
                "sv_use_sql 1",
                f'add_sqlserver r {DB_NAME} record {DB_USER} "{DB_PASS}" "{DB_HOST}" "{DB_PORT}"',
                f'add_sqlserver w {DB_NAME} record {DB_USER} "{DB_PASS}" "{DB_HOST}" "{DB_PORT}"',
            ]
        cfg += list(extra_config)

        self.cfg_path = os.path.join(env.tmp, "server.cfg")
        with open(self.cfg_path, "w") as f:
            f.write("\n".join(cfg) + "\n")

        with open(os.path.join(env.tmp, "storage.cfg"), "w") as f:
            f.write(f"add_path $CURRENTDIR\nadd_path {os.path.join(REPO, 'data')}\n")
        with open(os.path.join(env.tmp, "autoexec_server.cfg"), "w") as f:
            f.write("# intentionally empty: the integration tests are hermetic\n")

        super().__init__("server", [SERVER_BIN, "-f", self.cfg_path], cwd=env.tmp)
        self.wait_for(r"server name is", timeout=25)
        self._econ = None

    # -- rcon over econ --------------------------------------------------
    def _econ_connect(self):
        deadline = time.time() + 15
        last = None
        while time.time() < deadline:
            try:
                s = socket.create_connection(("127.0.0.1", self.econ_port), timeout=5)
                s.settimeout(1.0)
                self._drain(s)
                s.sendall((self.econ_password + "\n").encode())
                time.sleep(0.3)
                self._drain(s)
                self._econ = s
                return
            except OSError as e:
                last = e
                time.sleep(0.3)
        raise TestFailure(f"could not connect to econ: {last}")

    @staticmethod
    def _drain(sock, timeout=0.4):
        sock.settimeout(timeout)
        out = b""
        try:
            while True:
                chunk = sock.recv(16384)
                if not chunk:
                    break
                out += chunk
        except socket.timeout:
            pass
        return out.decode(errors="replace")

    def rcon(self, command, settle=0.7):
        """Run a command as an authenticated admin. Returns new server log lines."""
        if self._econ is None:
            self._econ_connect()
        start = self.mark()
        self._econ.sendall((command + "\n").encode())
        time.sleep(settle)
        self._drain(self._econ)
        return self.snapshot()[start:]

    def kill(self):
        if self._econ is not None:
            try:
                self._econ.close()
            except OSError:
                pass
        super().kill()


class Client(Process):
    def __init__(self, env, name, server, connect=True):
        self.env = env
        self.fifo = os.path.join(env.tmp, f"{name}.fifo")
        self._fifo_fd = None
        argv = [
            CLIENT_BIN, "-f", "/dev/null",
            f"cl_input_fifo {self.fifo}",
            "gfx_fullscreen 0",
            "cl_save_settings 0",
            "snd_enable 0",
            "cl_download_skins 0",
            "cl_menu_map \"\"",
            f"player_name {name}",
        ]
        super().__init__(name, argv, cwd=os.path.dirname(CLIENT_BIN))
        self.wait_for(r"client: version", timeout=30)
        self.player_name = name
        if connect:
            self.connect(server)

    def command(self, cmd):
        deadline = time.time() + 10
        while not os.path.exists(self.fifo) and time.time() < deadline:
            time.sleep(0.05)
        with open(self.fifo, "w") as f:
            f.write(cmd + "\n")

    def connect(self, server, timeout=25):
        start = self.mark()
        self.command(f"connect 127.0.0.1:{server.port}")
        self.wait_for(r"entered and joined", timeout=timeout, start=start)

    # -- player actions --------------------------------------------------
    def say(self, text, settle=0.35):
        self.command(f'say "{text}"')
        time.sleep(settle)

    def chat_command(self, cmd, settle=1.2):
        """Run a Block chat command and return the chat lines it produced."""
        start = self.mark()
        self.say(cmd, settle=0)
        time.sleep(settle)
        return [self._chat_text(l) for l in self.find(r"chat/server", start)]

    def callvote_option(self, option, reason="itest", settle=1.0):
        start = self.mark()
        self.command(f'callvote option "{option}" "{reason}"')
        time.sleep(settle)
        return self.snapshot()[start:]

    def vote(self, yes=True, settle=0.8):
        self.command(f"vote {'yes' if yes else 'no'}")
        time.sleep(settle)

    @staticmethod
    def _chat_text(line):
        m = re.search(r"chat/server: (.*)$", line)
        return (m.group(1) if m else line).strip()

    def chat(self, start=0):
        return [self._chat_text(l) for l in self.find(r"chat/server", start)]

    def kill(self):
        super().kill()
        try:
            os.unlink(self.fifo)
        except OSError:
            pass


# --------------------------------------------------------------------------
# stub HTTP endpoint for Discord / VPN / Agones
# --------------------------------------------------------------------------


class StubHttp:
    def __init__(self):
        self.requests = []
        self._lock = threading.Lock()
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def _record(self, method):
                length = int(self.headers.get("Content-Length", 0) or 0)
                body = self.rfile.read(length).decode(errors="replace") if length else ""
                with outer._lock:
                    outer.requests.append({"method": method, "path": self.path, "body": body})
                payload = b'{"ok":true,"proxy":"no","vpn":false,"block":0}'
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

            def do_POST(self):
                self._record("POST")

            def do_PUT(self):
                self._record("PUT")

            def do_GET(self):
                self._record("GET")

            def log_message(self, *args):
                pass

        self.server = HTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    @property
    def url(self):
        return f"http://127.0.0.1:{self.port}"

    def wait_for_request(self, predicate, timeout=10.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                for r in self.requests:
                    if predicate(r):
                        return r
            time.sleep(0.05)
        with self._lock:
            seen = list(self.requests)
        raise TestFailure(f"no matching HTTP request; saw {seen}")

    def stop(self):
        self.server.shutdown()


# --------------------------------------------------------------------------
# environment
# --------------------------------------------------------------------------


class Env:
    def __init__(self, keep=False):
        self.tmp = tempfile.mkdtemp(prefix="block-itest-")
        self.keep = keep
        self.procs = []
        self.stub = None

    def server(self, **kwargs):
        s = Server(self, **kwargs)
        self.procs.append(s)
        return s

    def client(self, name, server, **kwargs):
        c = Client(self, name, server, **kwargs)
        self.procs.append(c)
        return c

    def http_stub(self):
        if self.stub is None:
            self.stub = StubHttp()
        return self.stub

    def close(self):
        for p in reversed(self.procs):
            p.kill()
        if self.stub:
            self.stub.stop()
        if not self.keep:
            shutil.rmtree(self.tmp, ignore_errors=True)
        else:
            print(f"    (kept {self.tmp})")

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False


def preflight():
    missing = [p for p in (SERVER_BIN, CLIENT_BIN) if not os.path.exists(p)]
    if missing:
        print("missing binaries: " + ", ".join(missing), file=sys.stderr)
        print("build them with:", file=sys.stderr)
        print("  cmake -B build-verify -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCLIENT=OFF -DTOOLS=OFF "
              "-DSERVER=ON -DMYSQL=ON -DANTIBOT=OFF -DDOWNLOAD_GTEST=ON && ninja -C build-verify", file=sys.stderr)
        print("  cmake -B build-client -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCLIENT=ON -DHEADLESS_CLIENT=ON "
              "-DTOOLS=OFF -DSERVER=ON -DMYSQL=ON -DANTIBOT=OFF -DVIDEORECORDER=OFF && ninja -C build-client", file=sys.stderr)
        return False
    try:
        # database_created_on_first_startup deliberately drops it, so put it back
        # rather than making every later run start with a manual step
        if not database_exists():
            mysql(f"CREATE DATABASE `{DB_NAME}`;", database=None)
        mysql("SELECT 1;")
    except TestFailure as e:
        print(f"cannot reach the test database '{DB_NAME}': {e}", file=sys.stderr)
        print("create it once with:", file=sys.stderr)
        print(f"  sudo mysql -e \"CREATE DATABASE {DB_NAME}; GRANT ALL ON {DB_NAME}.* TO '{DB_USER}'@'localhost';\"", file=sys.stderr)
        return False
    return True

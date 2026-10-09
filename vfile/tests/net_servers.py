#!/usr/bin/env python3
"""
Network fault-injection servers for the vfile tests.

Runs a minimal FTP server and a minimal HTTP redirector on loopback.  Both
are only good enough to drive vfile; the point is the faults they can inject.

FTP faults are selected by the requested path (the server learns it from
SIZE/MDTM, which poldek sends before PASV):
  /pasv-drop/<name>     - abort the control connection when PASV arrives
  /pasv-nodigits/<name> - answer PASV with a 227 carrying no address at all
  /data-reset/<name>    - reset the data connection during RETR and leave
                          the reply to the aborted transfer unread

HTTP serves nothing but redirects:
  /to-ftp/<name>        - 302 to ftp://127.0.0.1:<ftp port>/<name>
  /to-dead-https/<name> - 302 to https://127.0.0.1:1/<name>
  /rel-to-ftp/<name>    - 302 to the relative path /to-ftp/<name>
"""

import argparse
import os
import socket
import struct
import sys
import threading
import time


def log(verbose, msg):
    if verbose:
        print(msg, file=sys.stderr, flush=True)


def listen(port):
    sock = socket.socket()
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('127.0.0.1', port))
    sock.listen(16)
    return sock


def reset(sock):
    """Close so that the peer sees RST instead of an orderly EOF"""
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                        struct.pack('ii', 1, 0))
        sock.close()
    except OSError:
        pass


class FTPSession:
    def __init__(self, sock, data_dir, verbose):
        self.sock = sock
        self.data_dir = data_dir
        self.verbose = verbose
        self.stream = sock.makefile('rwb', buffering=0)
        self.datasock = None
        self.path = ''

    def log(self, msg):
        log(self.verbose, "ftpd: %s" % msg)

    def send(self, line):
        self.log("> %s" % line.strip())
        self.stream.write(line.encode())

    def abort(self):
        """makefile() holds a reference, so the socket only really closes
           once the stream is closed too"""
        self.stream.close()
        reset(self.sock)

    def localpath(self):
        return os.path.join(self.data_dir, os.path.basename(self.path))

    def open_datasock(self):
        self.datasock = listen(0)
        return self.datasock.getsockname()[1]

    def serve(self):
        try:
            self.run()
        except OSError:         # client went away
            pass

    def run(self):
        self.send("220 vfile test ftpd\r\n")
        while True:
            line = self.stream.readline()
            if not line:
                break
            line = line.decode(errors='replace').strip()
            self.log("< %s" % line)
            parts = line.split(None, 1)
            if not parts:
                continue
            cmd = parts[0].upper()
            arg = parts[1] if len(parts) > 1 else ''

            if cmd in ('SIZE', 'MDTM', 'RETR'):
                self.path = arg

            if cmd in ('PASV', 'EPSV') and 'pasv-drop' in self.path:
                self.log("!! dropping control connection at %s" % cmd)
                self.abort()
                return

            if cmd in ('PASV', 'EPSV') and 'pasv-nodigits' in self.path:
                self.send("227 Entering Passive Mode\r\n")
                continue

            if cmd == 'USER':
                self.send("331 password required\r\n")
            elif cmd == 'PASS':
                self.send("230 logged in\r\n")
            elif cmd in ('TYPE', 'NOOP'):
                self.send("200 ok\r\n")
            elif cmd == 'SIZE':
                if os.path.isfile(self.localpath()):
                    self.send("213 %d\r\n" % os.path.getsize(self.localpath()))
                else:
                    self.send("550 no such file\r\n")
            elif cmd == 'MDTM':
                if os.path.isfile(self.localpath()):
                    ts = time.gmtime(os.path.getmtime(self.localpath()))
                    self.send("213 %s\r\n" % time.strftime("%Y%m%d%H%M%S", ts))
                else:
                    self.send("550 no such file\r\n")
            elif cmd == 'PASV':
                port = self.open_datasock()
                self.send("227 Entering Passive Mode (127,0,0,1,%d,%d)\r\n"
                          % (port >> 8, port & 0xff))
            elif cmd == 'EPSV':
                self.send("229 Entering Extended Passive Mode (|||%d|)\r\n"
                          % self.open_datasock())
            elif cmd == 'REST':
                self.send("350 ok\r\n")
            elif cmd == 'RETR':
                self.retr()
            elif cmd == 'QUIT':
                self.send("221 bye\r\n")
                break
            else:
                self.send("500 unknown command\r\n")

        self.sock.close()

    def retr(self):
        if not os.path.isfile(self.localpath()) or self.datasock is None:
            self.send("550 no such file\r\n")
            return

        size = os.path.getsize(self.localpath())
        self.send("150 Opening BINARY mode data connection (%d bytes)\r\n" % size)
        conn, _ = self.datasock.accept()

        if 'data-reset' in self.path:
            self.log("!! resetting data connection")
            reset(conn)
            self.datasock.close()
            self.datasock = None
            self.send("426 Transfer aborted\r\n")
            return

        with open(self.localpath(), 'rb') as f:
            conn.sendall(f.read())
        conn.close()
        self.datasock.close()
        self.datasock = None
        self.send("226 Transfer complete\r\n")


def http_session(sock, ftp_port, verbose):
    try:
        req = b""
        while b"\r\n\r\n" not in req:
            data = sock.recv(4096)
            if not data:
                return
            req += data
        line = req.split(b"\r\n")[0].decode(errors='replace')
        log(verbose, "httpd: < %s" % line)
        path = line.split()[1]
        name = os.path.basename(path)

        if path.startswith("/to-ftp/"):
            to = "ftp://127.0.0.1:%d/%s" % (ftp_port, name)
        elif path.startswith("/rel-to-ftp/"):
            to = "/to-ftp/%s" % name
        elif path.startswith("/to-dead-https/"):
            to = "https://127.0.0.1:1/%s" % name
        else:
            sock.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                         b"Connection: close\r\n\r\n")
            return

        log(verbose, "httpd: > 302 %s" % to)
        sock.sendall(("HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\n"
                      "Connection: close\r\n\r\n" % to).encode())
    except OSError:
        pass
    finally:
        try:
            sock.close()
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(description='vfile network test servers')
    parser.add_argument('--ftp-port', type=int, default=0)
    parser.add_argument('--http-port', type=int, default=0)
    parser.add_argument('--data-dir', default='./data')
    parser.add_argument('--write-ports',
                        help='write "<ftp port> <http port>" to this file')
    parser.add_argument('--verbose', '-v', action='store_true')
    args = parser.parse_args()

    verbose = args.verbose or bool(os.environ.get('VERBOSE'))

    ftp_srv = listen(args.ftp_port)
    http_srv = listen(args.http_port)
    ftp_port = ftp_srv.getsockname()[1]
    http_port = http_srv.getsockname()[1]

    if args.write_ports:
        with open(args.write_ports, 'w') as f:
            f.write("%d %d" % (ftp_port, http_port))

    print("test servers on ftp://127.0.0.1:%d and http://127.0.0.1:%d"
          % (ftp_port, http_port), flush=True)

    def accept_ftp():
        while True:
            conn, _ = ftp_srv.accept()
            session = FTPSession(conn, args.data_dir, verbose)
            threading.Thread(target=session.serve, daemon=True).start()

    threading.Thread(target=accept_ftp, daemon=True).start()

    while True:
        conn, _ = http_srv.accept()
        threading.Thread(target=http_session,
                         args=(conn, ftp_port, verbose), daemon=True).start()


if __name__ == '__main__':
    main()

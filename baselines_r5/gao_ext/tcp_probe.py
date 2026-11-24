#!/usr/bin/env python3
"""Check whether a local TCP connect/accept round trip works in this shell.
Usage: tcp_probe.py PORT [HOST ...]"""
import socket, sys
port = int(sys.argv[1]) if len(sys.argv) > 1 else 27333
hosts = sys.argv[2:] or ["127.0.0.1"]
for h in hosts:
    fam = socket.AF_INET6 if ":" in h else socket.AF_INET
    try:
        srv = socket.socket(fam); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((h, port)); srv.listen(4)
        cli = socket.socket(fam); cli.settimeout(3)
        cli.connect((h, port))
        conn, _ = srv.accept(); cli.sendall(b"ok"); print(h, "tcp ok:", conn.recv(2))
    except Exception as e:
        print(h, "tcp FAIL:", repr(e))
    port += 1

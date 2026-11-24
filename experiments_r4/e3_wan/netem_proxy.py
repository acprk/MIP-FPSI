"""User-space network emulator: TCP proxy with per-direction bandwidth and one-way delay.

Each direction is a FIFO link: a chunk that arrives at time a starts transmission at
max(a, link_free), occupies the link for 8*len/bandwidth seconds, and is delivered `delay` later.
No root privileges or tc/netem needed. Prints per-connection byte counts on close.
usage: python3 netem_proxy.py <listen_port> <upstream_host:port> <mbps> <one_way_delay_ms>
"""
import asyncio
import sys
import time

listen_port = int(sys.argv[1])
up_host, up_port = sys.argv[2].rsplit(":", 1)
bw = float(sys.argv[3]) * 1e6 / 8.0  # bytes per second
delay = float(sys.argv[4]) / 1000.0


async def pipe(reader, writer, tag):
    q = asyncio.Queue()
    link_free = 0.0
    total = 0

    async def deliver():
        while True:
            item = await q.get()
            if item is None:
                break
            t, data = item
            now = time.monotonic()
            if t > now:
                await asyncio.sleep(t - now)
            writer.write(data)
            await writer.drain()
        try:
            writer.write_eof()
        except Exception:
            pass

    task = asyncio.create_task(deliver())
    while True:
        data = await reader.read(1 << 16)
        if not data:
            break
        total += len(data)
        now = time.monotonic()
        start = max(now, link_free)
        link_free = start + len(data) / bw
        await q.put((link_free + delay, data))
    await q.put(None)
    await task
    print(f"{tag} bytes={total}", flush=True)


async def handle(cr, cw):
    for _ in range(600):  # the server may not be listening yet
        try:
            ur, uw = await asyncio.open_connection(up_host, int(up_port))
            break
        except OSError:
            await asyncio.sleep(0.05)
    else:
        cw.close()
        return
    await asyncio.gather(pipe(cr, uw, "c->s"), pipe(ur, cw, "s->c"))
    cw.close()
    uw.close()


async def main():
    srv = await asyncio.start_server(handle, "127.0.0.1", listen_port)
    async with srv:
        await srv.serve_forever()


asyncio.run(main())

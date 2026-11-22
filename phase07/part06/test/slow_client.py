import socket, time
s = socket.create_connection(("127.0.0.1", 8080))
s.setblocking(False)
blob = b"x" * 65536
sent = 0
while sent < 32 * 1024 * 1024:          # 32MB
    try:
        sent += s.send(blob)
    except BlockingIOError:
        time.sleep(0.005)
print("sent", sent, "now NOT reading")
time.sleep(300)

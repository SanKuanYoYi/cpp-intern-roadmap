import socket, time
s = socket.create_connection(("127.0.0.1", 8080))
s.sendall(b"a" * (1024 * 1024))          # 一次性发 1MB
time.sleep(1)
s.settimeout(2)
total = 0
try:
    while True:
        d = s.recv(65536)
        if not d: break
        total += len(d)
except socket.timeout:
    pass
print("echoed:", total)

import socket, struct, threading

def one(i):
    s = socket.create_connection(("127.0.0.1", 8080))
    body = f"hello-{i}".encode()
    s.sendall(struct.pack("!I", len(body)) + body)   # 4 字节长度 + body
    print(i, s.recv(1024))
    s.close()

ts = [threading.Thread(target=one, args=(i,)) for i in range(4)]
for t in ts: t.start()
for t in ts: t.join()
import socket, struct, time

HOST, PORT = "127.0.0.1", 8080

def frame(payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + payload

def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed")
        buf += chunk
    return buf

def recv_frame(s):
    (n,) = struct.unpack(">I", recv_exact(s, 4))
    return recv_exact(s, n)

def expect_no_reply(s, ms):
    s.settimeout(ms / 1000.0)
    try:
        data = s.recv(1)
        assert not data, "服务器提前回包了（还应当等）"
    except socket.timeout:
        pass

def case_roundtrip():
    s = socket.create_connection((HOST, PORT))
    for msg in [b"hello", b"x" * 100, b""]:
        s.sendall(frame(msg))
        assert recv_frame(s) == msg
    s.close()

def case_half_packet():
    s = socket.create_connection((HOST, PORT))
    body = b"A" * 1000
    f = frame(body)
    s.sendall(f[:2]);   expect_no_reply(s, 300)   # 半个头
    s.sendall(f[2:6]);  expect_no_reply(s, 300)   # 头齐了，body 只有 2 字节
    s.sendall(f[6:])
    s.settimeout(5)
    assert recv_frame(s) == body
    s.close()

def case_sticky():
    s = socket.create_connection((HOST, PORT))
    msgs = [b"one", b"two", b"three"]
    s.sendall(b"".join(frame(m) for m in msgs))   # 一次发三帧
    for m in msgs:
        assert recv_frame(s) == m
    s.close()

def case_too_large():
    s = socket.create_connection((HOST, PORT))
    s.sendall(struct.pack(">I", 0xFFFFFFFF))
    s.settimeout(2)
    try:
        assert s.recv(1) == b"", "超长帧应被断开"
    except (socket.timeout, ConnectionResetError):
        pass
    s.close()

if __name__ == "__main__":
    case_roundtrip(); print("roundtrip ok")
    case_half_packet(); print("half packet ok")
    case_sticky();   print("sticky ok")
    case_too_large(); print("too large ok")

import socket, sys, time
n = int(sys.argv[1])
socks = []
for i in range(n):
    socks.append(socket.create_connection(("127.0.0.1", 8080)))
print(f"connected: {n}")
time.sleep(60)          # 保持连接，别断

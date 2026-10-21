/*
AI 先行解析提示：

服务器端就像总机，先准备好“电话线”并“监听”来电；客户端则主动“拨号”。

客户端流程：
socket() → connect() → send() → recv() → close()
socket()：创建通信端点。
connect()：向服务器的IP和端口发起连接请求（拨号）。
send() / recv()：连接成功后，发送和接收数据。
close()：关闭连接。
*/
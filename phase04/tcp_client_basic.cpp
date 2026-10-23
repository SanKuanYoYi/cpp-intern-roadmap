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

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


int main() {
    // ---------- 1. socket() ----------
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }



    return 0;
}

/*
AI 文档粘贴，供于查看

步骤	服务器	客户端
socket()	✅	✅ 一样
bind()	✅ 必须（固定端口 8080）	❌ 不需要（内核自动分配临时端口）
listen()	✅ 必须	❌ 不需要
accept()	✅ 必须	❌ 不需要
connect()	❌ 不需要	✅ 必须（主动连接服务器）
recv/send	✅	✅ 一样（方向相反）


个连接在两个视角下，源/目的端口是反的：
服务器视角：local=8080, peer=54321
客户端视角：local=54321, peer=8080


sockfd 命名差异——纯习惯
// 服务器
int listenfd = socket(...);   // 命名 listenfd，强调"最终会 listen"
// 客户端
int sockfd = socket(...);     // 命名 sockfd，通用名
客户端的那个 fd 就是"我这条连接的 fd"，叫 sockfd 或 clientfd 都行




*/
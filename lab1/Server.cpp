#include<iostream>
#include<string.h>
#include<string>
#include<map>
#include<queue>
#include <typeinfo>
#include<WinSock2.h>
#include<WS2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
#pragma warning(disable : 4996)

#define DEFAULT_BUFLEN 500

using namespace std;

queue<string> message_queue; // 如果遇到消息并发的情形，需要添加消息队列单开一个处理并发消息转发的线程
map<SOCKET,string> user_list; // 目前在线用户名单

void appendUser(SOCKET &socket, char a[]) {
    string s = "";
    for (int i = 0; a[i]; i++)s += a[i];
    user_list[socket] = s;
}

DWORD WINAPI handlerRequest(LPVOID lparam)
{
    // 为每一个连接到此端口的用户创建一个线程
    SOCKET ClientSocket = (SOCKET)lparam;

    char curr_username[20];
    recv(ClientSocket, curr_username, sizeof(curr_username), 0); // 接收用户名
    appendUser(ClientSocket, curr_username);

    SYSTEMTIME sysTime = { 0 };
    GetLocalTime(&sysTime);
    cout << endl;
    cout << "[" << sysTime.wYear << "-" << sysTime.wMonth << "-" << sysTime.wDay << " ";
    cout << sysTime.wHour << ":" << sysTime.wMinute << ":" << sysTime.wSecond << "] ";
    cout << curr_username << " 已加入三体世界     ";
    cout << "目前在线人数：" << user_list.size() << "人" << endl;


    // 向用户发送当前在线用户名单
    if (user_list.size() > 0) {
        string nameList = "";
        for (auto it : user_list) {
            nameList += it.second;
            nameList += " ";
        }
        char sendList[1000];
        sendList[0] = '0';
        for (int i = 0; i < nameList.length(); i++)sendList[i + 1] = nameList[i];
        sendList[nameList.length()] = '\0';
        send(ClientSocket, sendList, sizeof(sendList), 0);
    }
    else {
        char sendList[1000];
        sendList[0] = '0';
        string msg = "当前无在线成员";
        for (int i = 0; i < msg.length(); i++)sendList[i + 1] = msg[i];
        send(ClientSocket, sendList, sizeof(sendList), 0);
    }


    // 循环接受客户端数据
    int recvResult;
    int sendResult;
    do {
        char recvBuf[DEFAULT_BUFLEN] = "";
        char sendBuf[DEFAULT_BUFLEN] = "";
        recvResult = recv(ClientSocket, recvBuf, DEFAULT_BUFLEN, 0);
        if (recvResult > 0) {
            SYSTEMTIME logTime = { 0 };
            GetLocalTime(&logTime);
            if (recvBuf[0] == '0') {
                // 获取当前用户列表
                char message[DEFAULT_BUFLEN];
                for (int i = 1; i < DEFAULT_BUFLEN; i++) {
                    message[i] = recvBuf[i];
                }
                cout << endl;
                cout << "当前在线用户" << "] " << message << endl;
            }
            else if (recvBuf[0] == '1') {
                // 群发
                char message[DEFAULT_BUFLEN];
                for (int i = 21; i < DEFAULT_BUFLEN; i++) {
                    message[i - 21] = recvBuf[i];
                }

                cout << endl;
                cout << "[" << logTime.wYear << "-" << logTime.wMonth << "-" << logTime.wDay << " ";
                cout << logTime.wHour << ":" << logTime.wMinute << ":" << logTime.wSecond;
                cout << " 来自 " << curr_username << " 的群发消息] " << message << endl;

                // 向其他用户分发消息
                for (auto it : user_list) {
                    if (it.first != ClientSocket) {
                        sendResult = send(it.first, recvBuf, DEFAULT_BUFLEN, 0);
                        if (sendResult == SOCKET_ERROR)cout << "send failed with error: " << WSAGetLastError() << endl;
                    }
                }
            }
            else if (recvBuf[0] == '2') {
                // 私发
                char des_user[20], message[DEFAULT_BUFLEN];
                for (int i = 1; i <= 20; i++)des_user[i - 1] = recvBuf[i];
                for (int i = 21; i < DEFAULT_BUFLEN; i++)message[i - 21] = recvBuf[i];
                cout << endl;
                cout << "[" << curr_username << " 私发给 " << des_user << " 的消息：" << "] " << message << endl;
                // 向指定用户发送消息
                bool success = 0; // 发送成功？
                for (auto it : user_list) {
                    string sdes_user = "";
                    for (int i = 0; des_user[i]; i++)sdes_user += des_user[i];
                    if (it.second == sdes_user) {
                        for (int i = 1; i <= 20; i++)recvBuf[i] = curr_username[i - 1];
                        sendResult = send(it.first, recvBuf, DEFAULT_BUFLEN, 0); // 发送
                        if (sendResult == SOCKET_ERROR)cout << "send failed with error: " << WSAGetLastError() << endl;
                        else success = 1;
                        break;
                    }
                }
                if (!success) {
                    string smsg = "发送失败，此用户不存在或未上线";
                    char msg[100];
                    for (int i = 0; i < smsg.length(); i++)msg[i] = smsg[i];
                    msg[smsg.length()] = '\0';
                    for (int i = 1; i <= 20; i++)sendBuf[i] = des_user[i - 1];
                    for (int i = 0; i < 100; i++)sendBuf[i + 21] = msg[i];
                    sendBuf[0] = '9';
                    sendResult = send(ClientSocket, sendBuf, DEFAULT_BUFLEN, 0); // 发送出错信息到原用户
                    if (sendResult == SOCKET_ERROR)cout << "send failed with error: " << WSAGetLastError() << endl;
                }
            }
        }
    } while (recvResult != SOCKET_ERROR);

    GetLocalTime(&sysTime);
    cout << endl;
    cout << "[" << sysTime.wYear << "-" << sysTime.wMonth << "-" << sysTime.wDay << " ";
    cout << sysTime.wHour << ":" << sysTime.wMinute << ":" << sysTime.wSecond << "] ";
    cout << curr_username << " 离开了三体世界" << endl;

    user_list.erase(ClientSocket);
    closesocket(ClientSocket);
    return 0;
}

int main()
{
    WSADATA wsaData; // 用来存储被WSAStartup函数调用后返回的Windows Sockets数据，包含Winsock.dll执行的数据。
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData); // 指定socket规范的版本
    if (iResult != NO_ERROR) {
        cout << "WSAStartup failed with error: " << iResult << endl;
        return 1;
    }


    // 创建一个监听的SOCKET
    // 如果有connect的请求就新创建一个线程
    SOCKET listenSocket;
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // 通信协议：IPv4 Internet协议; 套接字通信类型：TCP链接;  协议特定类型：某些协议只有一种类型，则为0
    if (listenSocket == INVALID_SOCKET) {
        cout << "socket failed with error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }


    // 获取本机ip
    char ip[20] = { 0 };
    struct hostent* phostinfo = gethostbyname("");
    char* p = inet_ntoa(*((struct in_addr*)(*phostinfo->h_addr_list)));
    strncpy(ip, p, sizeof(ip));
    cout << "服务器端ip为：" << ip << endl;


    // 用bind函数绑定IP地址和端口号
    sockaddr_in sockAddr;
    memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.sin_family = AF_INET;
    inet_pton(AF_INET, "10.130.106.124", &sockAddr.sin_addr.s_addr);
    // inet_pton(AF_INET, ip, &sockAddr.sin_addr.s_addr); // 将点分十进制的ip地址转化为用于网络传输的数值格式
    sockAddr.sin_port = htons(920); // 端口号
    iResult = bind(listenSocket, (SOCKADDR*)&sockAddr, sizeof(sockAddr)); // bind函数把一个本地协议地址赋予一个套接字。对于网际协议，协议地址是32位的IPv4地址或是128位的IPv6地址与16位的TCP或UDP端口号的组合
    if (iResult == SOCKET_ERROR) {
        wprintf(L"bind failed with error: %ld\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }


    // 使socket进入监听状态，监听远程连接是否到来
    if (listen(listenSocket, 5) == SOCKET_ERROR) {
        cout << "listen failed with error: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }


    // 对于每个新的请求使用多线程处理
    cout << "等待客户端连接..." << endl << endl;
    while (1) {
        sockaddr_in clientAddr;
        int len = sizeof(clientAddr);
        SOCKET AcceptSocket = accept(listenSocket, (SOCKADDR*)&clientAddr, &len); // 接受一个特定socket请求等待队列中的连接请求
        if (AcceptSocket == INVALID_SOCKET) {
            cout << "accept failed with error: " << WSAGetLastError() << endl;
            closesocket(listenSocket);
            WSACleanup();
            return 1;
        }
        else {
            HANDLE hThread = CreateThread(NULL, 0, handlerRequest, (LPVOID)AcceptSocket, 0, NULL); // 创建线程，并且传入与client通讯的套接字
            CloseHandle(hThread); // 关闭对线程的引用
        }
    }
    

    // 关闭服务端SOCKET
    iResult = closesocket(listenSocket);
    if (iResult == SOCKET_ERROR) {
        cout << "close failed with error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    WSACleanup();
    return 0;
}

#include<iostream>
#include<string>
#include<WinSock2.h>
#include<WS2tcpip.h>
#include<Windows.h>
#include<thread>
#pragma comment(lib,"ws2_32.lib")

#define DEFAULT_BUFLEN 500

using namespace std;

string quit_string = "quit"; // 退出信号
char user_name[20]; // 用户名

DWORD WINAPI recv(LPVOID lparam_socket) {
    int recvResult;
    SOCKET* recvSocket = (SOCKET*)lparam_socket; // 一定要使用指针，因为要指向connect socket的位置

    while (1) {
        char recvBuf[DEFAULT_BUFLEN] = "";
        recvResult = recv(*recvSocket, recvBuf, DEFAULT_BUFLEN, 0);
        if (recvResult > 0) {
            SYSTEMTIME systime = { 0 };
            GetLocalTime(&systime);
            cout << endl << endl;
            cout << "#################################################################" << endl;
            cout << endl;
            cout << "[" << systime.wYear << "-" << systime.wMonth << "-" << systime.wDay << " ";
            cout << systime.wHour << ":" << systime.wMinute << ":" << systime.wSecond; 
            if (recvBuf[0] == '0') {
                // 获取当前用户列表
                char message[DEFAULT_BUFLEN];
                for (int i = 1; i < DEFAULT_BUFLEN; i++) {
                    message[i - 1] = recvBuf[i];
                }
                cout << " 当前在线用户" << "] " << message << endl;
                cout << "#################################################################" << endl;
            }
            else if (recvBuf[0] == '1') {
                // 群发
                char user_name[20], message[DEFAULT_BUFLEN];
                for (int i = 1; i <= 20; i++)user_name[i - 1] = recvBuf[i];
                for (int i = 21; i < DEFAULT_BUFLEN; i++)message[i - 21] = recvBuf[i];
                cout << " 收到来自 " << user_name << " 群发的消息：" << "] " << message << endl;
                cout << "#################################################################" << endl;
            }
            else if (recvBuf[0] == '2') {
                // 私发
                char des_user[20], message[DEFAULT_BUFLEN];
                for (int i = 1; i <= 20; i++)des_user[i - 1] = recvBuf[i];
                for (int i = 21; i < DEFAULT_BUFLEN; i++)message[i - 21] = recvBuf[i];
                cout << " 收到来自 " << des_user << " 私发的消息" << "] " << message << endl;
                cout << "#################################################################" << endl;
            }
            else if (recvBuf[0] == '9') {
                // 收到私发错误信息
                char des_user[20], message[DEFAULT_BUFLEN];
                for (int i = 1; i <= 20; i++)des_user[i - 1] = recvBuf[i];
                for (int i = 21; i < DEFAULT_BUFLEN; i++)message[i - 21] = recvBuf[i];
                cout << " 发送给用户 " << des_user << " 的私发信息] " << message << endl;
                cout << "#################################################################" << endl;
            }
        }
        else {
            closesocket(*recvSocket);
            return 1;
        }
    }
}

DWORD WINAPI send(LPVOID lparam_socket) {

    // 接受消息直到quit退出聊天
    int sendResult;
    SOCKET* sendSocket = (SOCKET*)lparam_socket;

    while (1)
    {
        // 发送消息
        char sendBuf[DEFAULT_BUFLEN] = "";
        char temp[DEFAULT_BUFLEN] = "";
        cout << endl << endl;
        cout << "-----------------------------------------------------------------" << endl;
        cout << "请输入你的消息：";
        cin >> temp;
        // cin.getline(temp, DEFAULT_BUFLEN); // 保证可以输入空格，getline函数设置好了以换行符为结束
        if (temp == quit_string) {
            closesocket(*sendSocket);
            cout << endl << "您已退出" << endl;
            return 1;
        }
        while (1) {
            cout << "如群发，输入1；如私发，输入2：";
            string flag;
            cin >> flag;
            if (flag == "1") {
                sendBuf[0] = '1';
                for (int i = 1; i <= 20; i++)sendBuf[i] = user_name[i - 1];
                break;
            }
            else if (flag == "2") {
                sendBuf[0] = '2';
                char des_user[20];
                cout << "请输入要私发的用户名：";
                cin >> des_user;
                for (int i = 1; i <= 20; i++)sendBuf[i] = des_user[i - 1];
                break;
            }
            else {
                cout << "目前不支持此功能，请重新输入" << endl;
            }
        }
        for (int i = 21; i < DEFAULT_BUFLEN; i++) {
            sendBuf[i] = temp[i-21];
        }
        sendResult = send(*sendSocket, sendBuf, DEFAULT_BUFLEN, 0);
        if (sendResult == SOCKET_ERROR) {
            cout << "send failed with error: " << WSAGetLastError() << endl;
            closesocket(*sendSocket);
            WSACleanup();
            return 1;
        }
        else {
            SYSTEMTIME systime = { 0 };
            GetLocalTime(&systime);
            cout << endl << systime.wYear << "-" << systime.wMonth << "-" << systime.wDay << " ";
            cout << systime.wHour << ":" << systime.wMinute << ":" << systime.wSecond;
            cout << " 消息已成功发送" << endl;
            cout << "-----------------------------------------------------------------" << endl;
        }
        Sleep(1000); // 停顿1秒再接收输入
    }
}


int main() {
    int iResult; // 使用iResult的值来表征各个步骤是否操作成功
    WSADATA wsaData;
    SOCKET connectSocket = INVALID_SOCKET;

    int recvBuflen = DEFAULT_BUFLEN;
    int sendBuflen = DEFAULT_BUFLEN;


    // 初始化 Winsock,输出信息详细描述
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        cout << "WSAStartup failed with error: " << iResult << endl;
        return 1;
    }


    // 客户端创建SOCKET内存来连接到服务端
    connectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connectSocket == INVALID_SOCKET) {
        cout << "socket failed with error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }


    // 输入服务器ip
    cout << "请输入要连接的服务器ip地址：";
    char ip[20];
    cin >> ip;
    // 输入用户名
    cout << "请输入你的用户名：";
    cin >> user_name;


    // 创建sockaddr_in结构，再转换成SOCKADDR的结构
    // 要连接的服务端的IP地址、端口号
    sockaddr_in clientService;
    clientService.sin_family = AF_INET;
    inet_pton(AF_INET, "10.130.106.124", &clientService.sin_addr.s_addr);
    // inet_pton(AF_INET, ip, &clientService.sin_addr.s_addr);
    clientService.sin_port = htons(920);


    // Connect连接到服务端
    iResult = connect(connectSocket, (SOCKADDR*)&clientService, sizeof(clientService));
    if (iResult == SOCKET_ERROR) {
        cout << "connect failed with error: " << WSAGetLastError() << endl;
        closesocket(connectSocket);
        WSACleanup();
        return 1;
    }
    // 向服务器端发送用户名
    send(connectSocket, user_name, sizeof(user_name), 0);
    char nameList[1000], msg[1000];
    recv(connectSocket, nameList, sizeof(nameList), 0);
    for (int i = 1; i < 1000; i++)msg[i - 1] = nameList[i];
    

    // 打印进入聊天的标志
    cout << endl;
    cout << "*****************************************************************" << endl;
    cout << "*                                                               *" << endl;
    cout << "  欢迎 " << user_name << " 进入三体世界                          " << endl;
    cout << "*                                                               *" << endl;
    cout << "*                                                               *" << endl;
    cout << "*  当前在线用户                                                 *" << endl;    
    cout << "*                                                               *" << endl;
    cout << "  " << msg << "                                                  " << endl;
    cout << "*                                                               *" << endl;
    cout << "*                                                               *" << endl;
    cout << "*                                        输入quit以退出三体世界 *" << endl;
    cout << "*                                                               *" << endl;
    cout << "*****************************************************************" << endl;
    cout << endl << endl;

    // 创建两个线程，一个接受线程，一个发送线程
    HANDLE hThread[2];
    hThread[0] = CreateThread(NULL, 0, recv, (LPVOID)&connectSocket, 0, NULL);
    hThread[1] = CreateThread(NULL, 0, send, (LPVOID)&connectSocket, 0, NULL);

    WaitForMultipleObjects(2, hThread, TRUE, INFINITE);
    CloseHandle(hThread[0]);
    CloseHandle(hThread[1]);

    // 关闭socket
    iResult = closesocket(connectSocket);
    WSACleanup();
    return 0;
}

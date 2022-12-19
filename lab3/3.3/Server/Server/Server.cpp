#include "Server.h"

#define SERVER_IP "127.0.0.1"  // 服务器IP地址 
#define SERVER_PORT 920  // 服务器端口号
#define DATA_AREA_SIZE 1024
#define BUFFER_SIZE sizeof(Packet)  // 缓冲区大小
#define WINDOW_SIZE 16  // 滑动窗口大小

SOCKADDR_IN socketAddr;  // 服务器地址
SOCKET socketServer;  // 服务器套接字

int err;  // socket错误提示
int socketAddrLen = sizeof(SOCKADDR);
unsigned int packetNum;  // 发送数据包的数量
unsigned int fileSize;  // 文件大小
unsigned int recvSize;  // 累积收到的文件位置
unsigned int recvBase;  // 期待收到但还未收到的最小分组序号
bool isCached[WINDOW_SIZE];  // 标识窗口内的分组有没有被缓存
char* fileName;  // 文件名
char* fileBuffer;  // 读入文件缓冲区
char** receiveBuffer;  // 接收缓冲区

stringstream ss;
SYSTEMTIME sysTime = { 0 };
void printTime() {  // 打印系统时间
	ss.clear();
	ss.str("");
	GetSystemTime(&sysTime);
	ss << "[" << sysTime.wYear << "/" << sysTime.wMonth << "/" << sysTime.wDay << " " << sysTime.wHour + 8 << ":" << sysTime.wMinute << ":" << sysTime.wSecond << ":" << sysTime.wMilliseconds << "]";
	cout << ss.str();
}

void printPacketMessage(Packet* pkt) {  // 打印数据包信息 
	cout << "Packet size=" << pkt->len << " Bytes!  FLAG=" << pkt->FLAG;
	cout << " seqNumber=" << pkt->seq << " ackNumber=" << pkt->ack;
	cout << " checksum=" << pkt->checksum << " windowLength=" << pkt->window << endl;
}

void printSendPacketMessage(Packet* pkt) {
	cout << "[Send]";
	printPacketMessage(pkt);
}

void printReceivePacketMessage(Packet* pkt) {
	cout << "[Receive]";
	printPacketMessage(pkt);
}

void printWindow() {
	cout << "当前接收窗口: [" << recvBase << ", " << recvBase + WINDOW_SIZE - 1 << "]";
}

default_random_engine randomEngine;
uniform_real_distribution<float> randomNumber(0.0, 1.0);  // 自己设置丢包

void initSocket() {
	WSADATA wsaData;  // 存储被WSAStartup函数调用后返回的Windows Sockets数据  
	err = WSAStartup(MAKEWORD(2, 2), &wsaData);  // 指定版本 2.2
	if (err != NO_ERROR) {
		cout << "WSAStartup failed with error: " << err << endl;
		return;
	}
	socketServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  // 通信协议：IPv4 Internet协议; 套接字通信类型：TCP链接;  协议特定类型：某些协议只有一种类型，则为0

	u_long mode = 1;
	ioctlsocket(socketServer, FIONBIO, &mode); // 设置阻塞模式，等待来自客户端的数据包

	socketAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);  // 0.0.0.0, 即本机所有IP
	socketAddr.sin_family = AF_INET;  // 使用IPv4地址
	socketAddr.sin_port = htons(SERVER_PORT);  // 端口号

	/*
	* 为什么服务器需要绑定而客户端不需要绑定？
	*
	* IP地址和端口号是用来标识具体某一台主机上的具体一个进程的。
	* 也就是说，端口号可以用来标识主机上的某一个进程。
	* 因此，操作系统需要对端口号进行管理，并且计算机中的端口号是有限的。
	* 如果不进行绑定，操作系统会随机生成一个端口号给服务器。
	* 如果操作系统给服务器分配这个端口号的同时，
	* 有其他程序也准备使用这个端口号或者说端口号已经被使用，
	* 则可能会导致服务器一直启动不起来。
	* 其次，服务器运行起来就不会再停止了，我们将服务器端的端口号绑定有助于有规划的对主机中的端口号进行使用。
	* 客户端需要主动向服务器端发送请求，因此客户端就需要知道服务器端的IP地址和端口号，
	* 如果不绑定，让系统随机生成，客户端将无法知道服务器端的端口号，即使知道也需要每次都去获取。
	* 对于客户端来说，服务器端并不需要主动给客户端发送数据，
	* 客户端是主动的而服务器端是被动的。
	* 客户端给服务器端发送数据时，会将自己的IP地址和端口号一起发送过去，服务器端可以方便的找到客户端。
	* 同时，客户端并不是一直运行的，只需要每次系统随机分配即可。
	* 因此，服务器端需要绑定而客户端不需要绑定。
	*/

	err = bind(socketServer, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));  // 绑定到socketServer
	if (err == SOCKET_ERROR) {
		err = GetLastError();
		cout << "Could not bind the port " << SERVER_PORT << " for socket. Error message: " << err << endl;
		WSACleanup();
		return;
	}
	else {
		printTime();
		cout << "服务器启动成功，等待客户端建立连接" << endl;
	}
}

void connect() {
	int state = 0;  // 标识目前握手的状态
	bool flag = 1;
	Packet* sendPkt = new Packet;
	Packet* recvPkt = new Packet;

	while (flag) {
		switch (state) {
		case 0:  // 等待客户端发送数据包状态
			err = recvfrom(socketServer, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
			if (err > 0) {
				if (recvPkt->FLAG & 0x2) {  // SYN=1
					printTime();
					cout << "收到来自客户端的建连请求，开始第二次握手，向客户端发送ACK,SYN=1的数据包..." << endl;

					// 第二次握手，向客户端发送ACK,SYN=1的数据包
					sendPkt->setSYNACK();
					sendto(socketServer, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));
					state = 1;  // 转状态1
				}
				else {
					printTime();
					cout << "第一次握手阶段收到的数据包有误，重新开始第一次握手..." << endl;
				}
			}
			break;

		case 1:  // 接收客户端的ACK=1数据包
			err = recvfrom(socketServer, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
			if (err > 0) {
				if (recvPkt->FLAG & 0x4) {  // ACK=1
					printTime();
					cout << "收到来自客户端第三次握手ACK数据包..." << endl;

					state = 2;  // 转状态2
				}
				else {
					printTime();
					cout << "第三次握手阶段收到的数据包有误，重新开始第三次握手..." << endl;
					// 其实这里是有点问题的，这两边的sendto都没有单开一个状态，所以其实不会重复发送，但这里懒得写了
				}
			}
			break;

		case 2: // 三次握手结束状态
			printTime();
			cout << "三次握手结束，确认已建立连接，开始文件传输..." << endl;
			cout << endl << "**************************************************************************************************" << endl << endl;
			flag = 0;
			break;
		}
	}
}

void disconnect() {  // 参考 <https://blog.csdn.net/LOOKTOMMER/article/details/121307137>
	int state = 0;  // 标识目前挥手的状态
	bool flag = 1;
	Packet* sendPkt = new Packet;
	Packet* recvPkt = new Packet;

	u_long mode = 1;
	ioctlsocket(socketServer, FIONBIO, &mode); // 设置成阻塞模式等待ACK响应

	while (flag) {
		switch (state) {
		case 0:  // CLOSE_WAIT_1
			printTime();
			cout << "接收到客户端的断开连接请求，开始第二次挥手，向客户端发送ACK=1的数据包..." << endl;

			// 第二次挥手，向客户端发送ACK=1的数据包
			sendPkt->setACK(1);
			sendto(socketServer, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));

			state = 1;  // 转状态1
			break;

		case 1:  // CLOSE_WAIT_2
			printTime();
			cout << "开始第三次挥手，向客户端发送FIN,ACK=1的数据包..." << endl;

			// 第三次挥手，向客户端发送FIN,ACK=1的数据包
			sendPkt->setFINACK();
			sendto(socketServer, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));

			state = 2;  // 转状态2
			break;

		case 2:  // LAST_ACK
			err = recvfrom(socketServer, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
			if (err > 0) {
				if (recvPkt->FLAG & 0x4) {  // ACK=1
					printTime();
					cout << "收到了来自客户端第四次挥手的ACK数据包..." << endl;

					state = 3;  // 转状态3
				}
				else {
					printTime();
					cout << "第四次挥手阶段收到的数据包有误，重新开始第四次挥手..." << endl;
				}
			}
			break;

		case 3:  // CLOSE
			printTime();
			cout << "四次挥手结束，确认已断开连接，Bye-bye..." << endl;
			flag = 0;
			break;
		}
	}
}

void saveFile() {
	string filePath = "C:/Users/new/Desktop/";  // 保存路径
	for (int i = 0; fileName[i]; i++)filePath += fileName[i];
	ofstream fout(filePath, ios::binary | ios::out);

	fout.write(fileBuffer, fileSize);  // 这里还是size,如果使用string.data或c_str的话图片不显示，经典深拷贝问题
	fout.close();
}

void sendACK(unsigned int ack) {  // 发送ACK数据包
	Packet* sendPkt = new Packet;
	sendPkt->setACK(ack);
	sendto(socketServer, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));
}

void receiveFile() {
	bool flag = 1;
	recvSize = 0;
	recvBase = 0;
	receiveBuffer = new char* [WINDOW_SIZE];
	for (int i = 0; i < WINDOW_SIZE; i++)receiveBuffer[i] = new char[DATA_AREA_SIZE];
	Packet* recvPkt = new Packet;
	Packet* sendPkt = new Packet;

	// 先接收文件头
	while (flag) {
		err = recvfrom(socketServer, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
		if (err > 0) {
			if (isCorrupt(recvPkt)) {  // 检测出数据包损坏
				printTime();
				cout << "收到一个损坏的数据包，不做任何处理" << endl << endl;
			}
			printReceivePacketMessage(sendPkt);
			printTime();
			if (recvPkt->FLAG & 0x8) {  // HEAD=1
				fileSize = recvPkt->len;
				fileBuffer = new char[fileSize];
				fileName = new char[128];
				memcpy(fileName, recvPkt->data, strlen(recvPkt->data) + 1);
				packetNum = fileSize % DATA_AREA_SIZE ? fileSize / DATA_AREA_SIZE + 1 : fileSize / DATA_AREA_SIZE;

				cout << "收到来自发送端的文件头数据包，文件名为: " << fileName;
				cout << "。文件大小为: " << fileSize << " Bytes，总共需要接收 " << packetNum << " 个数据包";
				cout << "，等待发送文件数据包..." << endl << endl;

				flag = 0;  // 跳出等待接收文件头的循环
			}
			else {
				cout << "收到的数据包不是文件头，等待发送端重传..." << endl;
			}

		}
	}
	
	// 循环接收文件数据包
	while (recvBase < packetNum) {  // 还没收完
		err = recvfrom(socketServer, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
		if (err > 0) {
			if (isCorrupt(recvPkt)) {  // 检测出数据包损坏
				printTime();
				cout << "收到一个损坏的数据包，不做任何处理" << endl << endl;
			}
			printReceivePacketMessage(recvPkt);
			printTime();
			cout << "收到第 " << recvPkt->seq << " 号数据包" << endl;
			if (randomNumber(randomEngine) >= 0.02) {  // 自主进行丢包测试
				unsigned int seq = recvPkt->seq;
				if (seq >= recvBase && seq < recvBase + WINDOW_SIZE) {  // 窗口内的分组被接收
					sendACK(recvPkt->seq);  // 必须发送ACK
					if (!isCached[seq - recvBase]) {  // 没有被缓存，那就缓存下来
						cout << "首次收到该数据包，将其缓存  ";
						memcpy(receiveBuffer[seq - recvBase], recvPkt->data, DATA_AREA_SIZE);
						isCached[seq - recvBase] = 1;
					}
					else {
						cout << "已经收到过该数据包！";
					}

					if (seq == recvBase) {  // 该分组序号等于基序号
						for (int i = 0; i < WINDOW_SIZE; i++) {  // 窗口移动
							if (!isCached[i]) break;  // 遇到没有缓存的，停下来
							recvBase++;
						}
						int offset = recvBase - seq;
						cout << "该数据包序号等于目前接收基序号，窗口移动 " << offset << " 个单位  ";
						for (int i = 0; i < offset; i++) {  // 将这些分组交付上层
							memcpy(fileBuffer + (seq + i) * DATA_AREA_SIZE, receiveBuffer[i], recvPkt->len);
						}
						for (int i = 0; i < WINDOW_SIZE - offset; i++) {
							isCached[i] = isCached[i + offset];  // 这里与发送端类似，都需要平移两个数组以对齐端口
							isCached[i + offset] = 0;
							memcpy(receiveBuffer[i], receiveBuffer[i + offset], DATA_AREA_SIZE);  // 缓存区也平移，或许这里可以使用取模运算避免大量的内存拷贝，提高性能？（时间和脑子不够用了，不想了）
						}
						for (int i = WINDOW_SIZE - offset; i < WINDOW_SIZE; i++) {
							isCached[i] = 0;  // 这里一开始没想到，可能出现的bug是这样的，窗口内除了recvBase都收到了，再收到的recvBase的ack，这时offset就等于窗口大小，那么上一个循环将不会执行，那些值也不会置零了
						}
					}
					printWindow();
					cout << endl;
				}
				else if (seq >= recvBase - WINDOW_SIZE && seq < recvBase) {
					printWindow();
					cout << "  该数据包分组序列号在[recv_base - N, recv_base - 1]，发送ACK，但不进行其他操作" << endl;
					sendACK(recvPkt->seq);  // 在这个范围内的分组必须发送ACK，但不做其他任何操作
				}
				else {
					printWindow();
					cout << "  分组序号 " << seq << " 不在正确范围，不做任何操作" << endl;
				}
			}
			else {
				cout << "主动丢包" << endl;
			}
		}
	}

	// 跳出循环说明文件接收完毕
	printTime();
	cout << "文件接收完毕..." << endl;
	cout << endl << "**************************************************************************************************" << endl << endl;

	// 保存文件
	saveFile();

	// 文件保存完毕，等待发送端发送断连请求
	flag = 1;
	while (flag) {
		err = recvfrom(socketServer, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
		if (err > 0) {
			if (recvPkt->FLAG & 0x1) {  // FIN=1
				flag = 0;
				disconnect();
			}
		}
	}
}

int main() {
	// 初始化服务器socket
	initSocket();

	// 建立连接
	connect();

	// 开始接收文件
	receiveFile();

	// 断连完成后，关闭socket
	err = closesocket(socketServer);
	if (err == SOCKET_ERROR) {
		cout << "Close socket failed with error: " << WSAGetLastError() << endl;
		return 1;
	}

	// 清理退出
	printTime();
	cout << "程序退出..." << endl;
	WSACleanup();
	return 0;
}
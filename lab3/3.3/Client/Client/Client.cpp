#include "Client.h"

#define SERVER_IP "127.0.0.1"  // 服务器的IP地址
#define SERVER_PORT 920
#define DATA_AREA_SIZE 1024
#define BUFFER_SIZE sizeof(Packet)  // 缓冲区大小
#define TIME_OUT 0.2 * CLOCKS_PER_SEC  // 超时重传，这里暂时设为0.2s
#define WINDOW_SIZE 16  // 滑动窗口大小

SOCKADDR_IN socketAddr;  // 服务器地址
SOCKET socketClient;  // 客户端套接字

int err;  // socket错误提示
int socketAddrLen = sizeof(SOCKADDR);
unsigned int fileSize;  // 文件大小
unsigned int packetNum;  // 发送数据包的数量
unsigned int sendBase;  // 窗口基序号，指向已发送还未被确认的最小分组序号
unsigned int nextSeqNum;  // 指向下一个可用但还未发送的分组序号

char* filePath;  // 文件(绝对)路径
char* fileName;  // 文件名
char* fileBuffer;  // 这个设计其实不好，一股脑把文件全部读入缓冲区，如果文件太大可能内存超限
char** selectiveRepeatBuffer;  // 选择重传缓冲区

/*
* 计时器SetTimer的返回值，即Timer的ID。
* 当窗口句柄为NULL时，系统会随机分配ID，因此如果该数组某个元素不为0，
* 就代表当前对应的分组有Timer在计时
* 所以这个数组有两个重要的作用：
* (1)数组元素的值对应计时器的ID
* (2)标识一个分组有没有对应的计时器，如果有(即数组元素不为0)，说明该分组未被ack
*/
int timerID[WINDOW_SIZE];

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
	cout << " checksum=" << pkt->checksum << " windowLength=" << pkt->window;
}

void printSendPacketMessage(Packet* pkt) {
	cout << "[Send]";
	printPacketMessage(pkt);
}

void printReceivePacketMessage(Packet* pkt) {
	cout << "[Receive]";
	printPacketMessage(pkt);
	cout << endl;
}

void printWindow() {
	cout << "  当前发送窗口: [" << sendBase << ", " << sendBase + WINDOW_SIZE - 1 << "]";
	cout << endl;
}

void initSocket() {
	WSADATA wsaData;
	err = WSAStartup(MAKEWORD(2, 2), &wsaData);  // 版本 2.2 
	if (err != NO_ERROR)
	{
		cout << "WSAStartup failed with error: " << err << endl;
		return;
	}
	socketClient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	socketAddr.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	socketAddr.sin_family = AF_INET;
	socketAddr.sin_port = htons(SERVER_PORT);

	printTime();
	cout << "客户端初始化成功，准备与服务器建立连接" << endl;
}

void connect() {
	int state = 0;  // 标识目前握手的状态
	bool flag = 1;
	Packet* sendPkt = new Packet;
	Packet* recvPkt = new Packet;

	u_long mode = 1;
	ioctlsocket(socketClient, FIONBIO, &mode); // 设置成阻塞模式等待ACK响应

	while (flag) {
		switch (state) {
		case 0:  // 发送SYN=1数据包状态
			printTime();
			cout << "开始第一次握手，向服务器发送SYN=1的数据包..." << endl;

			// 第一次握手，向服务器发送SYN=1的数据包，服务器收到后会回应SYN, ACK=1的数据包
			sendPkt->setSYN();
			sendto(socketClient, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));
			state = 1;  // 转状态1
			break;

		case 1:  // 等待服务器回复
			err = recvfrom(socketClient, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
			if (err >= 0) {
				if ((recvPkt->FLAG & 0x2) && (recvPkt->FLAG & 0x4)) {  // SYN=1, ACK=1
					printTime();
					cout << "开始第三次握手，向服务器发送ACK=1的数据包..." << endl;

					// 第三次握手，向服务器发送ACK=1的数据包，告知服务器自己接收能力正常
					sendPkt->setACK();
					sendto(socketClient, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));
					state = 2;  // 转状态2
				}
				else {
					printTime();
					cout << "第二次握手阶段收到的数据包有误，重新开始第一次握手..." << endl;
					state = 0;  // 转状态0
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
	ioctlsocket(socketClient, FIONBIO, &mode); // 设置成阻塞模式等待ACK响应

	while (flag) {
		switch (state) {
		case 0:  // 发送FIN=1数据包状态
			printTime();
			cout << "开始第一次挥手，向服务器发送FIN=1的数据包..." << endl;

			// 第一次挥手，向服务器发送FIN=1的数据包
			sendPkt->setFIN();
			sendto(socketClient, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));

			state = 1;  // 转状态1
			break;

		case 1:  // FIN_WAIT_1
			err = recvfrom(socketClient, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
			if (err >= 0) {
				if (recvPkt->FLAG & 0x4) {  // ACK=1
					printTime();
					cout << "收到了来自服务器第二次挥手ACK数据包..." << endl;

					state = 2;  // 转状态2
				}
				else {
					printTime();
					cout << "第二次挥手阶段收到的数据包有误，重新开始第二次挥手..." << endl;
				}
			}
			break;
		
		case 2:  // FIN_WAIT_2
			err = recvfrom(socketClient, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
			if (err >= 0) {
				if ((recvPkt->FLAG & 0x1) && (recvPkt->FLAG & 0x4)) {  // ACK=1, FIN=1
					printTime();
					cout << "收到了来自服务器第三次挥手FIN&ACK数据包，开始第四次挥手，向服务器发送ACK=1的数据包..." << endl;

					// 第四次挥手，向服务器发送ACK=1的数据包，通知服务器确认断开连接
					sendPkt->setFINACK();
					sendto(socketClient, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));
					state = 3;  // 转状态3
				}
				else {
					printTime();
					cout << "第三次挥手阶段收到的数据包有误，重新开始第三次挥手..." << endl;
				}
			}
			break;
		
		case 3:  // TIME_WAIT
			// 这里按照TCP的方法，需要等待2MSL，但是先不写了，直接退出
			state = 4;
			break;

		case 4:  // 四次挥手结束状态
			printTime();
			cout << "四次挥手结束，确认已断开连接，Bye-bye..." << endl;
			flag = 0;
			break;
		}
	}
}

void sendPacket(Packet* sendPkt) {  // 封装了一下发送数据包的过程
	cout << "发送第 " << sendPkt->seq << " 号数据包";
	printWindow();
	printSendPacketMessage(sendPkt);  // 检查一下文件的各个内容
	cout << endl;
	err = sendto(socketClient, (char*)sendPkt, BUFFER_SIZE, 0, (SOCKADDR*)&socketAddr, sizeof(SOCKADDR));
	if (err == SOCKET_ERROR) {
		cout << "Send Packet failed with ERROR: " << WSAGetLastError() << endl;
	}
}

void resendPacket(HWND hwnd, UINT uMsg, UINT idEvent, DWORD dwTime) {  // 重传函数
	// cout << endl << "resend" << " Timer ID " << idEvent << endl << endl;
	unsigned int seq = 0;
	for (int i = 0; i < WINDOW_SIZE; i++) {  // 找到是哪个Timer超时了
		if (timerID[i] == idEvent && timerID[i] != 0) {
			seq = i + sendBase;
			break;
		}
	}
	cout << "第 " << seq << " 号数据包对应的计时器超时，重新发送" << endl;

	Packet* resendPkt = new Packet;
	memcpy(resendPkt, selectiveRepeatBuffer[seq - sendBase], sizeof(Packet));  // 从缓冲区直接取出来
	sendPacket(resendPkt);
	printSendPacketMessage(resendPkt);
	cout << endl;
	// timerID[seq - sendBase] = SetTimer(NULL, 0, TIME_OUT, (TIMERPROC)resendPacket);  // 重设Timer
}

void ackHandler(unsigned int ack) { // 处理来自接收端的ACK
	// cout << endl << "ack " << ack << endl << endl;
	if (ack >= sendBase && ack < sendBase + WINDOW_SIZE) {  // 如果ack分组序号在窗口内
		// cout << "KillTimer " << timerID[ack - sendBase] << endl << endl;
		KillTimer(NULL, timerID[ack - sendBase]);
		timerID[ack - sendBase] = 0;  // timerID置零

		if (ack == sendBase) {  // 如果恰好=sendBase，那么sendBase移动到具有最小序号的未确认分组处
			for (int i = 0; i < WINDOW_SIZE; i++) {
				if (timerID[i]) break;  // 遇到一个有计时器的停下来(如果当前timerID数组元素不为0，说明有计时器在使用)
				sendBase++;  // sendBase后移
			}
			int offset = sendBase - ack;
			for (int i = 0; i < WINDOW_SIZE - offset; i++) {
				timerID[i] = timerID[i + offset];  // timerID也得平移，不然对不上了
				timerID[i + offset] = 0;
				memcpy(selectiveRepeatBuffer[i], selectiveRepeatBuffer[i + offset], sizeof(Packet));  // 缓冲区也要平移
			}
			for (int i = WINDOW_SIZE - offset; i < WINDOW_SIZE; i++) {
				timerID[i] = 0;  // 这里一开始没想到，可能出现的bug是这样的，窗口内除了recvBase都收到了，再收到的recvBase的ack，这时offset就等于窗口大小，那么上一个循环将不会执行，那些值也不会置零了
			}
		}
	}
}

void readFile() {  // 这个设计其实不好，一股脑把文件全部读入缓冲区，如果文件太大可能内存超限(但是几十兆几百兆还是能承受得了的)，存在改进空间
	ifstream f(filePath, ifstream::in | ios::binary);  // 以二进制方式打开文件
	if (!f.is_open()) {
		cout << "文件无法打开！" << endl;
		return;
	}
	f.seekg(0, std::ios_base::end);  // 将文件流指针定位到流的末尾
	fileSize = f.tellg();
	packetNum = fileSize % DATA_AREA_SIZE ? fileSize / DATA_AREA_SIZE + 1 : fileSize / DATA_AREA_SIZE;  // 解决了一个小bug，之前直接+1的话，如果是1024的整数倍，会多切出一个包来，所以这里要判断一下
	cout << "文件大小为 " << fileSize << " Bytes，总共要发送 " << packetNum + 1 << " 个数据包" << endl << endl;
	f.seekg(0, std::ios_base::beg);  // 将文件流指针重新定位到流的开始
	fileBuffer = new char[fileSize];
	f.read(fileBuffer, fileSize);
	f.close();
}

void sendFile() {
	sendBase = 0;
	nextSeqNum = 0;
	selectiveRepeatBuffer = new char* [WINDOW_SIZE];
	for (int i = 0; i < WINDOW_SIZE; i++)selectiveRepeatBuffer[i] = new char[sizeof(Packet)];  // char[32][1048]
	clock_t start = clock();
	
	// 先发一个记录文件名的数据包，并设置HEAD标志位为1，表示开始文件传输
	Packet* headPkt = new Packet;
	printTime();
	cout << "发送文件头数据包..." << endl;
	headPkt->setHEAD(0, fileSize, fileName);
	headPkt->checksum = checksum((uint32_t*)headPkt);
	sendPacket(headPkt);  // 这里还没有实现文件头的缺失重传机制

	// 开始发送装载文件的数据包
	printTime();
	cout << "开始发送文件数据包..." << endl;
	Packet* sendPkt = new Packet;
	Packet* recvPkt = new Packet;
	while (sendBase < packetNum) {
		while (nextSeqNum < sendBase + WINDOW_SIZE && nextSeqNum < packetNum) {  // 只要下一个要发送的分组序号还在窗口内，就继续发
			if (nextSeqNum == packetNum - 1) {  // 如果是最后一个包
				sendPkt->setTAIL();
				sendPkt->fillData(nextSeqNum, fileSize - nextSeqNum * DATA_AREA_SIZE, fileBuffer + nextSeqNum * DATA_AREA_SIZE);
				sendPkt->checksum = checksum((uint32_t*)sendPkt);
			}
			else {  // 正常的1024Bytes数据包
				sendPkt->fillData(nextSeqNum, DATA_AREA_SIZE, fileBuffer + nextSeqNum * DATA_AREA_SIZE);
				sendPkt->checksum = checksum((uint32_t*)sendPkt);
			}
			memcpy(selectiveRepeatBuffer[nextSeqNum - sendBase], sendPkt, sizeof(Packet));  // 存入缓冲区
			sendPacket(sendPkt);
			timerID[nextSeqNum - sendBase] = SetTimer(NULL, 0, TIME_OUT, (TIMERPROC)resendPacket);  // 启动计时器，这里nIDEvent=0，是因为窗口句柄设为NULL的情况下这里填什么都无所谓，反正它会重新分配，返回值才是真正的nIDEvent
			nextSeqNum++;
			Sleep(10);  // 睡眠10ms，包与包之间有一定的时间间隔
		}

		// 如果当前发送窗口已经用光了，就进入接收ACK阶段
		err = recvfrom(socketClient, (char*)recvPkt, BUFFER_SIZE, 0, (SOCKADDR*)&(socketAddr), &socketAddrLen);
		if (err > 0) {
			printReceivePacketMessage(recvPkt);
			ackHandler(recvPkt->ack);  // 处理ack
		}

		// 应该不能把监听消息的写在这里
		MSG msg;
		// GetMessage(&msg, NULL, 0, 0);  // 阻塞式，一开始用的这个，被坑了，他不得到消息不会结束，所以一定会触发重传
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {  // 以查看的方式从系统中获取消息，可以不将消息从系统中移除，是非阻塞函数；当系统无消息时，返回FALSE，继续执行后续代码。
			if (msg.message == WM_TIMER) {  // 定时器消息
				DispatchMessage(&msg);
			}
		}
	}

	clock_t end = clock();
	printTime();
	cout << "文件发送完毕，传输时间为：" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
	cout << "吞吐率为：" << ((float)fileSize) / ((end - start) / CLOCKS_PER_SEC) << " Bytes/s " << endl << endl;
	cout << endl << "**************************************************************************************************" << endl << endl;
}

int main() {
	// 初始化客户端
	initSocket();

	// 与服务器端建立连接
	connect();

	// 读取文件
	cout << "请输入您要传输的文件名(包括文件类型后缀)：";
	fileName = new char[128];
	cin >> fileName;
	cout << "请输入您要传输的文件所在路径(绝对路径)：";
	filePath = new char[128];
	cin >> filePath;
	readFile();
	// C:\\Users\\new\\Desktop\\Tree-Hole\\readme.md
	// C:\Users\new\Desktop\GRE填空机经1100题难度分级版（第二版）.pdf
	// C:\\Users\\new\\Desktop\\test\\1.jpg
	// C:\\Users\new\\Desktop\\test\\helloworld.txt

	// 开始发送文件
	sendFile();
	
	// 主动断开连接
	disconnect();

	// 断连完成后，关闭socket
	err = closesocket(socketClient);
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
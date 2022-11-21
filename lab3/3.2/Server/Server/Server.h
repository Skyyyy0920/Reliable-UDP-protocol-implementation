#ifndef __DEFINE_H__
#define __DEFINE_H__
#include "pkg.h"

class Packet {
public:
	uint32_t FLAG;  // 标志位，目前只用最后五位（二进制）分别是TAIL, HEAD, ACK，SYN，FIN

	/* 这里引申出一个小知识，类成员在内存中是要对齐的，这主要是因为如果不对齐，
	* 在本例中，会出现seq这个变量有3Byte出现在前一个word中，后1Byte在后面一个word
	* 不利于计算机去存取（需要两次存取，还要拼接操作）
	* 如果这里使用char类型，虽然char本身占1Byte，但是为了和后面的几个成员变量对齐（最大的那个，在这里是4Byte）
	* 这里tag也会占4Byte，其中的3Byte是填充位
	* 所以，既然无论如何都要消耗3Byte，那么为什么我们不给他要过来？
	* 如果真的想用char去省空间，那么剩下的3Byte我们可以用于声明其他的成员变量
	* 比如在FLAG后面加一个short类型的变量，这样只会浪费1Byte，但是注意必须紧跟着tag之后声明
	*/

	uint32_t seq;  // 序列号，uint32_t相当于unsigned int，只不过是不管是多少位的系统他都是32位
	uint32_t ack;  // 确认号
	uint32_t len;  // 数据部分长度
	uint32_t checksum;  // 校验和
	uint32_t window;  // 窗口
	char data[1024];  // 数据长度
public:
	Packet() : FLAG(0), seq(0), ack(0), len(0), checksum(0), window(0) { memset(data, 0, sizeof(data)); };
	Packet(uint32_t FLAG, uint32_t seq, uint32_t ack, uint32_t len, uint32_t checksum, uint32_t window);
	void setACK(unsigned int ack);
	void setSYN();
	void setSYNACK();
	void setFIN();
	void setFINACK();
	void setTAIL();
	void setHEAD(int seq, int fileSize, char* fileName);
	void fillData(int seq, int size, char* data);
	// void setChecksum();
};

Packet::Packet(uint32_t FLAG, uint32_t seq, uint32_t ack, uint32_t len, uint32_t checksum, uint32_t window) {
	this->FLAG = FLAG;
	this->seq = seq;
	this->ack = ack;
	this->len = len;
	this->checksum = checksum;
	this->window = window;
	memset(this->data, 0, sizeof(this->data));
}

void Packet::setACK(unsigned int ack) {
	// 设置ACK位为1
	this->FLAG = 0x4;  // FLAG -> 00000100
	this->ack = ack;
}

void Packet::setSYN() {
	// 设置SYN位为1
	this->FLAG = 0x2;  // FLAG -> 00000010
}

void Packet::setSYNACK() {
	// 设置SYN, ACK = 1
	this->FLAG = 0x4 + 0x2;  // FLAG -> 00000110
}

void Packet::setFIN() {
	// 设置FIN位为1
	this->FLAG = 0x1;  // FLAG -> 00000001
}

void Packet::setFINACK() {
	// 设置FIN, ACK = 1    
	this->FLAG = 0x4 + 0x1;  // FLAG -> 00000101
}

void Packet::setHEAD(int seq, int fileSize, char* fileName) {
	// 设置HEAD位为1    
	this->FLAG = 0x8;  // FLAG -> 00001000

	// 这里的len并不是data的len，而是整个文件的size
	this->len = fileSize;
	this->seq = seq;
	memcpy(this->data, fileName, strlen(fileName) + 1);
}

void Packet::setTAIL() {
	// 设置TAIL位为1
	this->FLAG = 0x16;  // FLAG -> 00010000
}

void Packet::fillData(int seq, int size, char* data) {
	// 将文件数据填入数据包data变量
	this->seq = seq;
	this->len = size;
	memcpy(this->data, data, size);
}

uint16_t checksum(uint32_t* pkt) {
	int size = sizeof(pkt);
	int count = (size + 1) / 2;
	uint16_t* buf = (uint16_t*)malloc(size);  // 可以+1也可以不+1
	memset(buf, 0, size);
	memcpy(buf, pkt, size);
	u_long sum = 0;
	while (count--) {
		sum += *buf++;
		if (sum & 0xFFFF0000) {
			sum &= 0xFFFF;
			sum++;
		}
	}
	return ~(sum & 0xFFFF);
}

bool isCorrupt(Packet* pkt) {
	if (pkt->checksum == checksum((uint32_t*)pkt))return false;
	return true;
}


#endif
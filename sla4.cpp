#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define SLAVEIP "127.0.0.1"
#define SLAVEPORT 502

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <iostream>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct MtoS {   //마스터 -> 슬레이브
	UINT16 TID;
	UINT16 PID;
	UINT16 Length;
	UINT8 UNIT;
	UINT8 FC;
	UINT16 StartAddr;
	UINT16 DLength;
} MtoS;

typedef struct StoM {   //슬레이브 -> 마스터
	UINT16 TID;
	UINT16 PID;
	UINT16 Length;
	UINT8 UNIT;
	UINT8 FC;
	UINT8 ByteCount;
	UINT8* Data;     //데이터 포인터 담기
} StoM;

typedef struct ERRtoM { //에러 -> 마스터
	UINT16 TID;
	UINT16 PID;
	UINT16 Length;
	UINT8 UNIT;
	UINT8 EC;
	UINT8 EX;
} ERRtoM;

class ModBus {
private:
	WSADATA wsaData;
	SOCKET slaveSock, masterSock;
	SOCKADDR_IN slaveAddr, masterAddr;

	int masterAddrSize;
	MtoS recvStruct;
	StoM sendStruct;
	ERRtoM errStruct;
	UINT16 regMap[200] = { 0, };

public:
	int SocketOn();
	void ErrorHandling(const char* message);
	int AcceptLoop();
	int ProtocolRecv();
	void PrintHexa(unsigned char* buff, size_t len);
	int CheckRecvData();
	void ErrorSend();
	int ProtocolSend(char* sendMsg, int size);
	void FC03Read(UINT16* memMap, UINT16 addr, UINT16 len);
	void MakeSendStruct(StoM* sS, MtoS* rS, UINT16* mM);
	void EndSocket();

};

int ModBus::SocketOn() {
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		ErrorHandling("WSAStartup() Error");
		return -1;
	}

	slaveSock = socket(PF_INET, SOCK_STREAM, 0);
	if (slaveSock == -1) {
		ErrorHandling("socket() Error!");
		return -1;
	}

	memset(&slaveAddr, 0, sizeof(slaveAddr));
	slaveAddr.sin_family = AF_INET;
	slaveAddr.sin_addr.s_addr = inet_addr(SLAVEIP);
	slaveAddr.sin_port = htons(SLAVEPORT);

	if (bind(slaveSock, (SOCKADDR*)&slaveAddr, sizeof(slaveAddr)) == -1) {
		ErrorHandling("bind() Error");
		return -1;
	}

	if (listen(slaveSock, 5) == -1) {
		ErrorHandling("listen() Error");
		return -1;
	}

	std::cout << "slave on" << std::endl;
	return 0;
}

void ModBus::ErrorHandling(const char* message) {
	fputs(message, stderr);
	fputc('/n', stderr);
}

int ModBus::AcceptLoop() {
	masterAddrSize = sizeof(masterAddr);

	masterSock = accept(slaveSock, (SOCKADDR*)&masterAddr, &masterAddrSize);
	if (masterSock == -1) {
		ErrorHandling("accept() Error");
		return -1;
	}
	else {
		std::cout << "connected" << std::endl;
		while (1) {
			if (ProtocolRecv() != 0) break;
			if (CheckRecvData() != 0) {
				ErrorSend();
				continue;
			}

			UINT16 mapData[150] = { 0, };
			if (recvStruct.FC == 0x03) {
				FC03Read(&mapData[0], recvStruct.StartAddr, recvStruct.DLength);
			}

			MakeSendStruct(&sendStruct, &recvStruct, &mapData[0]);

			char dumpSend[200];
			memset(dumpSend, 0, sizeof(char) * 200);
			//memcpy(dumpSend, &sendStruct, htons(sendStruct.Length) + 6);
			memcpy(dumpSend, &sendStruct, 9);
			memcpy(&dumpSend[9], *(&sendStruct.Data), htons(sendStruct.Length) + 6 - 9);
			ProtocolSend(dumpSend, htons(sendStruct.Length) + 6);
		}
	}
	closesocket(masterSock);
}

int ModBus::ProtocolRecv() {
	UINT8 recvingBuff[50];
	UINT8 recvBuff[50];
	memset(recvingBuff, 0x00, sizeof(UINT8) * 50);
	memset(recvBuff, 0x00, sizeof(UINT8) * 50);
	int recvingBuffSize = 0;
	int recvBuffSize = 0;
	time_t startT, nowT;
	startT = time(NULL);

	while (1) {
		int lengthSendToggle = 0;
		int lengthSaveToggle = 0;

		if (time(NULL) - startT >= 10) {
			ErrorHandling("recv time out");
			return 1;
		}
		if ((recvBuffSize = recv(masterSock, (char *)recvBuff, 50, 0)) > 0) {
			memcpy(&recvingBuff[recvingBuffSize], &recvBuff[0], recvBuffSize);
			recvingBuffSize += recvBuffSize;
			if (recvBuffSize >= 12) {
				memcpy(&recvStruct, &recvingBuff[0], 12);
				recvStruct.TID = htons(recvStruct.TID);
				recvStruct.PID = htons(recvStruct.PID);
				recvStruct.Length = htons(recvStruct.Length);
				recvStruct.StartAddr = htons(recvStruct.StartAddr);
				recvStruct.DLength = htons(recvStruct.DLength);
				PrintHexa(recvingBuff, 12);
				return 0;
			}
		}
		else return 3;
		startT = time(NULL);
	}
}

void ModBus::PrintHexa(unsigned char* buff, size_t len) {
	size_t i;
	for (i = 0; i < len; i++) {
		printf("%02X ", (unsigned char)buff[i]);
	}
	printf("\n");
}

int ModBus::CheckRecvData() {
	if (recvStruct.PID != 0x0000) return -1;
	if (recvStruct.Length != 0x0006) return -1;
	if (recvStruct.FC != 0x03) return -1;
	if (recvStruct.StartAddr != 100) return -1;
	if (recvStruct.DLength > 20) return -1;

	return 0;
}

void ModBus::ErrorSend() {
	std::cout << "received data error" << std::endl;
	errStruct.TID = htons(recvStruct.TID);
	errStruct.PID = htons(recvStruct.PID);
	errStruct.Length = htons(0x0003);
	errStruct.UNIT = recvStruct.UNIT;
	errStruct.EC = 0x80 + recvStruct.FC;
	errStruct.EX = 0x01;

	char errSend[10];
	memcpy(errSend, &errStruct, 9);

	ProtocolSend(errSend, 9);
}

int ModBus::ProtocolSend(char* sendMsg, int size) {
	PrintHexa((unsigned char*)sendMsg, size);
	time_t startT, nowT;
	startT = time(NULL);
	int sendToggle = 0;
	int sendLength = 0;
	int sendAllLength = 0;

	while (1) {
		if (sendToggle >= 1) return 0;
		if (time(NULL) - startT >= 10) {
			std::cout << "timeout" << std::endl;
			return 1;
		}
		sendLength = send(masterSock, *(&sendMsg + sendAllLength), size - sendAllLength, 0);
		if (sendLength != -1) {
			sendAllLength += sendLength;
			if (sendAllLength == size) { sendToggle++; return 0; }
		}
		else if (sendAllLength > size) {
			std::cout << "many data sended" << std::endl;
			return 2;
		}

		startT = time(NULL);
	}
	return -1;
}

void ModBus::FC03Read(UINT16* memMap, UINT16 addr, UINT16 len) {
	UINT16* cpyMem = memMap;

	for (int i = 0; i< 100; i++) {
		regMap[i + 99] += i;
	}

	memcpy(cpyMem, &regMap[addr], sizeof(UINT16)*len);
}

void ModBus::MakeSendStruct(StoM* sS, MtoS* rS, UINT16* mM) {
	StoM* sendStr = sS;
	MtoS* recvStr = rS;
	UINT16* cpMem = mM;

	sendStr->TID = htons(recvStr->TID);
	sendStr->PID = htons(recvStr->PID);
	sendStr->Length = htons((recvStr->DLength * 2) + 3);
	sendStr->UNIT = recvStr->UNIT;
	sendStr->FC = recvStr->FC;
	sendStr->ByteCount = recvStr->DLength * 2;
	//sendStr->Data = (UINT8*)&cpMem[0];

	UINT16 bb;
	for (int i = 0; i < recvStr->DLength; i++) {
		bb = htons(cpMem[i]);
		memcpy(&cpMem[i], &bb, sizeof(UINT16));
	} sendStr->Data = (UINT8*)&cpMem[0];

	//memcpy(&(sendStr->Data), (cpMem), sizeof(UINT8));
}

void ModBus::EndSocket() {
	closesocket(slaveSock);
	WSACleanup();
}










int main() {
	ModBus MO;
	MO.SocketOn();
	while (1) {
		MO.AcceptLoop();
	}
	MO.EndSocket();

	return 0;
}
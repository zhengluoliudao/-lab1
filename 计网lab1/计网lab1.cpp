// 计网lab1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include "pch.h"

#pragma comment(lib,"Ws2_32.lib")
#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http服务器端口

//Http重要头部数据
struct HttpHeader {
	char method[4]; // POST 或者 GET ，注意有些为 CONNECT ，本实验暂不考虑
	char url[1024]; // 请求的 url
	char host[1024]; // 目标主机
	char cookie[1024 * 10]; //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));
	}
};

struct Cache_Content {
	char LastModified[100];// 最新修改时间
	char Url[1024];// 目标url
	char Content[MAXSIZE];//报文
};

void AddCache(char* Buffer, char * Url, int recvSize);
int SearchInCache(char * Url);
void my_Website_Filter();
BOOL InitSocket();
BOOL ParseHttpHead(char *buffer, HttpHeader * httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket, char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 8080;
char ForbidenWebsite[100][1024];
struct Cache_Content Cache[20];
int Index = 0;

CRITICAL_SECTION g_cs;      //1.定义一个临界区对象

struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};

int _tmain(int argc, _TCHAR* argv[])
{
	printf(" 代理服务器正在启动\n");
	printf(" 初始化...\n");
	if (!InitSocket()) {
		printf("socket初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam *lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID; 
	//代理服务器不断监听
	while (true) {
		//从监听状态的ProxyServer的连接请求列表中取出第一个，并创建一个新套接字来创建连接通道
		acceptSocket = accept(ProxyServer, NULL, NULL);
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL) {
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		//创建新线程
		hThread = (HANDLE)_beginthreadex(NULL, 0,
			&ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		//结束线程句柄，归还系统内核资源
		CloseHandle(hThread);
		Sleep(200);
	}

	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}

BOOL InitSocket() {
	//初始化禁止访问网站列表
	//my_Website_Filter();
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);

	//创建代理服务器套接字
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	
	//协议族，端口号，通配符
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	//绑定套接字的本地端口地址
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf(" 绑定套接字失败 \n");
		return FALSE;
	}
	//将代理服务器的套接字置于监听状态，设置最大连接请求队列大小，SOMAXCONN为内核参数=5
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf(" 监听端口 %d 失败 ", ProxyPort);
		return FALSE;
	}
	return TRUE;
}

//错误处理
void my_error(LPVOID lpParameter) {
	//printf(" 关闭套接字\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete lpParameter;
	_endthreadex(0);
}

unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
	char Buffer[MAXSIZE];
	char *CacheBuffer;
	ZeroMemory(Buffer, MAXSIZE);
	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;
	//recv函数把客户端clientSocket的接收缓冲中的数据copy到buffer中，此处buffer为报文最大长度，应该不会溢出，返回实际接收到的长度
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	//HTTP头的结构
	HttpHeader* httpHeader = new HttpHeader();
	CacheBuffer = new char[recvSize + 1];
	//用0来填充一块内存区域，初始化cachebuffer，这是一个强力又危险的函数，，怎么代码路子这么野
	ZeroMemory(CacheBuffer, recvSize + 1);
	//将buffer中的数据复制recvsize个字节到cachebuffer中
	memcpy(CacheBuffer, Buffer, recvSize);
	//由cachebuffer即接收到的报文段，解析出http头复制到httpheader，网站过滤，如果该网站是在禁止访问列表中则结束
	if (!ParseHttpHead(CacheBuffer, httpHeader)) {
		my_error(lpParameter);
		return 0;
	}
	/*
	int FoundIndex;
	if ((FoundIndex = SearchInCache(httpHeader->url)) != -1) {
		printf(" 缓存中找到，代理连接主机 %s 成功\n", httpHeader->host);
	}else{
		AddCache(CacheBuffer, httpHeader->url, recvSize);
	}
	*/
	
	delete CacheBuffer;
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		my_error(lpParameter);
		return 0;
	}
	printf(" 代理连接主机 %s 成功\n", httpHeader->host);
	//将客户端发送的 HTTP 数据报文直接转发给目标服务器
	ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
	//等待目标服务器返回数据
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	//将目标服务器返回的数据直接转发给客户端
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
	my_error(lpParameter);
	return 0;
}


BOOL ParseHttpHead(char *buffer, HttpHeader * httpHeader) {
	char *p;
	char *ptr;
	const char * delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);// 提取第一行
	printf("%s\n", p);
	if (p[0] == 'G') {//GET 方式
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST 方式
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	printf("%s\n", httpHeader->url);
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H'://Host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			if (!strcmp(httpHeader->host, "activity.windows.com:443")) {
				return FALSE;
			}
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie")) {
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
	return TRUE;
}

BOOL ConnectToServer(SOCKET *serverSocket, char *host) {
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	//域名解析，通过域名还原IP地址
	HOSTENT *hostent = gethostbyname(host);
	if (!hostent) {
		printf("host 域名：  ");
		printf(host);
		printf("\n");
		return FALSE;
	}
	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	//创建目标服务器套接字
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		printf("INVALID_SOCKET\n");
		return FALSE;
	}
	//套接字serverSocket 与 发送网页数据报的HTTP服务器端口连接
	if (connect(*serverSocket, (SOCKADDR *) &serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		printf("SOCKET_ERROR\n");
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}

//支持cache功能，代理服务器。 要求能缓存原服务器响应的对象，并能够通过修改请求报文
void AddCache(char* Buffer, char * Url, int recvSize) {
	time_t timep;
	struct tm *p;
	time(&timep);
	p = gmtime(&timep);
	memcpy((Cache[Index]).LastModified, p, sizeof(tm));
	memcpy((Cache[Index]).Url, Url, strlen(Url));
	memcpy((Cache[Index]).Content, Buffer, recvSize);
	Index++;
}

int SearchInCache(char * Url) {
	for (int i = 0; i < Index; i++) {
		if (!strcmp(Cache[i].Url, Url)) {
			return i;
		}
	}
	return -1;
}

//网站过滤
void my_Website_Filter() {
	char website1[1024] = "activity.windows.com:443";
	char website2[1024] = "google.com";
	memcpy(ForbidenWebsite[0], &website1[0], strlen(website1));
	memcpy(ForbidenWebsite[1], &website2[0], strlen(website2));
	//memcpy(ForbidenWebsite[1], &website[0],  strlen(website));
}

//用户过滤
void my_User_Filter() {

}

//网站引导
void Website_Lead() {

}
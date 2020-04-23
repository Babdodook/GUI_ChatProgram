#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

/*
	���������� Ŭ���̾�Ʈ�� ������ ������
	Ŭ���̾�Ʈ�� ������ Ŭ���̾�Ʈ���� ����ü�� �����Ͽ�
	Ŭ���̾�Ʈ���� �迭�� �����ϰ�
	�� Ŭ���̾�Ʈ���� �����带 �������Ѽ�
	Ŭ���̾�Ʈ�� �����Ҷ����� ����ϵ��� �Ѵ�.
	������ ���� ��

*/

#define ROOMLIST "����Ʈ\r\n1.�ڿ��ù�\r\n2.����ȯ��\r\n3.��õ����\r\n"
#define ROOM_ONE "225.0.0.1"
#define ROOM_TWO "225.0.0.2"
#define ROOM_THREE "225.0.0.3"
#define MAX_COUNT 100			// Ŭ���̾�Ʈ �迭 �ִ� ī��Ʈ
#define SIZE 20					// ���� �̸� ������
#define SERVERPORT 9000
#define REMOTEPORT 9001
#define BUFSIZE    512

enum PROTOCOL
{
	CREATE_USER = 0,		// ���� �̸� ����
	LOBBY,					// �κ�, �� �����ϱ�
	CHAT,					// ä���ϱ�
	EXIT					// ����
};

enum Client_State
{
	INIT_STATE = 0,
	SET_STATE,
	LOBBY_STATE,
	CHAT_STATE,
	EXIT_STATE
};

enum SELECT_TYPE
{
	LIST = 0,
	ONE,
	TWO,
	THREE
};


typedef struct user					// ���� ����
{
	char name[SIZE];
}_UserInfo;

typedef struct clientinfo			// Ŭ���̾�Ʈ ����
{
	SOCKET sock;
	SOCKADDR_IN addr;
	char buf[BUFSIZE];
	_UserInfo* user;			// ���� ������ �ޱ� ���� ��
	Client_State state;			// ����
	HANDLE wait_exit_event;		// Ŭ���̾�Ʈ ���� �̺�Ʈ
	HANDLE hthread;				// ���� ����ϴ� ������
}_ClientInfo;

struct SockInfo
{
	SOCKET sock;				// ����
	SOCKADDR_IN sockaddr;		// ���� �ּ� ����ü
	ip_mreq mreq;
};

_UserInfo* UserInfoArr[MAX_COUNT];		// ���� ������

_ClientInfo* ClientInfo[MAX_COUNT];		// Ŭ���̾�Ʈ ������
int userCount = 0;
int clientCount = 0;

HANDLE hThread[MAX_COUNT];		// ���� ������ �������
int ThreadCount = 0;

//ũ��Ƽ�ü���, Ŭ���̾�Ʈ ���� �� ������ �� �ʿ�
CRITICAL_SECTION cs;

void err_quit(char* msg);								// ���� �Լ� ���� ��� �� ����
void err_display(char* msg);							// ���� �Լ� ���� ���
int recvn(SOCKET s, char* buf, int len, int flags);

// ���� ����
SOCKET TCP_sock_init();									// ���� ����
SockInfo* CreateMSendSocket(const char* MULTICASTIP);		// udp ��Ƽĳ��Ʈ send ���� ����
ip_mreq Add_Membership(SOCKET sock, const char* MULTICASTIP);		// ��Ƽĳ��Ʈ ����
void Drop_Membership(SockInfo* ptr);								// ��Ƽĳ��Ʈ Ż��

// �������� ���
void AddUserInfo(_UserInfo user);

// Ŭ���̾�Ʈ ���� ���
_ClientInfo* AddClientInfo(SOCKET _sock, SOCKADDR_IN _addr);		// Ŭ���̾�Ʈ �߰�
void RemoveClientInfo(_ClientInfo* _ptr);							// Ŭ���̾�Ʈ ����
_ClientInfo* SearchClientInfo(HANDLE _hthread);					// �ش� �����带 ���� Ŭ���̾�Ʈ �˻�

bool PacketRecv(SOCKET _sock, char* _buf);
PROTOCOL GetProtocol(const char* _ptr);


/*
	��ŷ
*/
int Pack_protocol(char* _buf, PROTOCOL _protocol);
int Pack_msg(char* _buf, PROTOCOL _protocol, const char* msg);

/*
	����ŷ
*/
void UnPack_userInfo(const char* _buf, _UserInfo* user);
void UnPack_msg(const char* _buf, int& _size, char* msg);


/*
	������ ����
*/
DWORD WINAPI ProcessClient(LPVOID);	// Ŭ���̾�Ʈ ��� ������
DWORD WINAPI RemoveClient(LPVOID);	// Ŭ���̾�Ʈ �� ������ ���� ������

bool AddThread(LPTHREAD_START_ROUTINE process, _ClientInfo* _ptr);	// ������ �߰�
void RemoveThread(HANDLE _hthread);									// ������ ����

int main(int argc, char* argv[])
{
	int retval;

	// ���� �ʱ�ȭ
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;
	InitializeCriticalSection(&cs);
	// ���� �ʱ�ȭ
	SOCKET listen_sock = TCP_sock_init();

	// ������ ��ſ� ����� ����
	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;

	// 0��°�� �̺�Ʈ �ֱ�, ThreadCount �����ϱ� ����
	hThread[ThreadCount++] = CreateEvent(nullptr, false, false, nullptr);

	CreateThread(nullptr, 0, RemoveClient, nullptr, 0, nullptr);

	while (1)
	{
		addrlen = sizeof(clientaddr);
		client_sock = accept(listen_sock, (SOCKADDR*)&clientaddr,
			&addrlen);
		if (client_sock == INVALID_SOCKET)
		{
			err_display("accept()");
			break;
		}

		// Ŭ���̾�Ʈ �迭�� Ŭ���̾�Ʈ ���� �߰�
		_ClientInfo* ptr = AddClientInfo(client_sock, clientaddr);

		// ������ �����, ������ �迭�� ������ �߰�
		AddThread(ProcessClient, ptr);

		SetEvent(hThread[0]);

	}

	closesocket(listen_sock);
	DeleteCriticalSection(&cs);

	WSACleanup();
	return 0;
}

SOCKET TCP_sock_init()
{
	// socket()
	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET) err_quit("socket()");

	// bind()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	int retval = bind(listen_sock, (SOCKADDR*)&serveraddr,
		sizeof(serveraddr));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	// listen()
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit("listen()");

	return listen_sock;
}

int recvn(SOCKET s, char* buf, int len, int flags)
{
	int received;
	char* ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if (received == 0)
			break;
		left -= received;
		ptr += received;
	}

	return (len - left);
}

// ���� ���� �Լ� ����
void AddUserInfo(_UserInfo user)
{
	EnterCriticalSection(&cs);

	UserInfoArr[userCount] = new _UserInfo;
	strcpy(UserInfoArr[userCount]->name, user.name);
	userCount++;

	LeaveCriticalSection(&cs);
}

// Ŭ���̾�Ʈ ���� ���� �Լ� ����
_ClientInfo* AddClientInfo(SOCKET _sock, SOCKADDR_IN _addr)
{
	EnterCriticalSection(&cs);
	_ClientInfo* ptr = new _ClientInfo;
	ptr->sock = _sock;
	memcpy(&ptr->addr, &_addr, sizeof(SOCKADDR_IN));
	ptr->user = nullptr;
	ptr->state = INIT_STATE;
	ptr->wait_exit_event = CreateEvent(nullptr, false, false, nullptr);

	ClientInfo[clientCount++] = ptr;

	LeaveCriticalSection(&cs);

	printf("\n[TCP ����] Ŭ���̾�Ʈ ����: IP �ּ�=%s, ��Ʈ ��ȣ=%d\n",
		inet_ntoa(ptr->addr.sin_addr), ntohs(ptr->addr.sin_port));

	return ptr;
}
void RemoveClientInfo(_ClientInfo* _ptr)
{
	EnterCriticalSection(&cs);

	for (int i = 0; i < clientCount; i++)
	{
		if (ClientInfo[i] == _ptr)
		{
			closesocket(ClientInfo[i]->sock);

			delete ClientInfo[i];

			for (int j = i; j < clientCount - 1; j++)
			{
				ClientInfo[j] = ClientInfo[j + 1];
			}
			ClientInfo[clientCount - 1] = nullptr;
			clientCount--;

			printf("[TCP ����] Ŭ���̾�Ʈ ����: IP �ּ�=%s, ��Ʈ ��ȣ=%d\n",
				inet_ntoa(_ptr->addr.sin_addr), ntohs(_ptr->addr.sin_port));

			break;
		}
	}

	LeaveCriticalSection(&cs);
}
_ClientInfo* SearchClientInfo(HANDLE _hthread)
{
	EnterCriticalSection(&cs);

	_ClientInfo* ptr = nullptr;

	for (int i = 0; i < clientCount; i++)
	{
		if (ClientInfo[i]->hthread == _hthread)
		{
			ptr = ClientInfo[i];
			break;
		}
	}

	LeaveCriticalSection(&cs);
	return ptr;
}


bool AddThread(LPTHREAD_START_ROUTINE process, _ClientInfo* _ptr)
{
	EnterCriticalSection(&cs);
	_ptr->hthread = CreateThread(nullptr, 0, process, _ptr, 0, nullptr);
	if (_ptr->hthread == nullptr)
	{
		LeaveCriticalSection(&cs);
		return false;
	}

	hThread[ThreadCount++] = _ptr->hthread;
	LeaveCriticalSection(&cs);
	return true;
}
void RemoveThread(HANDLE _hthread)
{
	EnterCriticalSection(&cs);

	for (int i = 1; i < ThreadCount; i++)
	{
		if (hThread[i] == _hthread)
		{
			CloseHandle(_hthread);

			for (int j = i; j < ThreadCount - 1; j++)
			{
				hThread[j] = hThread[j + 1];
			}

			hThread[ThreadCount - 1] = nullptr;
			ThreadCount--;
			break;
		}
	}

	LeaveCriticalSection(&cs);
}

// Ŭ���̾�Ʈ ���� ������
DWORD WINAPI ProcessClient(LPVOID _ptr)
{
	_ClientInfo* ptr = (_ClientInfo*)_ptr;

	PROTOCOL protocol;
	int size;
	int retval;
	char msg[BUFSIZE];
	ZeroMemory(msg, sizeof(msg));

	_UserInfo user;
	ZeroMemory(user.name, sizeof(user.name));

	bool endflag = false;

	char MULTICASTIP[50];
	SockInfo* multiSender;
	bool createOnce = false;


	while (1)
	{
		switch (ptr->state)
		{
		case INIT_STATE:
			ptr->state = SET_STATE;
			break;
		case SET_STATE:
			if (!PacketRecv(ptr->sock, ptr->buf))		// ���� �̸� �޴´�
			{
				ptr->state = EXIT_STATE;
				break;
			}

			protocol = GetProtocol(ptr->buf);			// �������� �и�

			UnPack_userInfo(ptr->buf, &user);			// ���� ���� ����ŷ

			//strcpy(ptr->user->name, user.name);
			//printf("%s\n", ptr->user->name);

			protocol = LOBBY;

			size = Pack_protocol(ptr->buf, protocol);		// �κ� �������� ��ŷ

			retval = send(ptr->sock, ptr->buf, size, 0);	// �κ� �������� ������.
			if (retval == SOCKET_ERROR)
			{
				err_display("lobby protocol send()");
				break;
			}

			ptr->state = LOBBY_STATE;
			break;
		case LOBBY_STATE:
			size = Pack_msg(ptr->buf, LOBBY, ROOMLIST);		// �渮��Ʈ �޽��� ��ŷ

			retval = send(ptr->sock, ptr->buf, size, 0);			// �渮��Ʈ ������
			if (retval == SOCKET_ERROR)
			{
				err_display("lobby protocol send()");
				break;
			}

			printf("�渮��Ʈ ����\n");

			if (!PacketRecv(ptr->sock, ptr->buf))			// ���� �� �����ߴ��� �ޱ�
			{
				ptr->state = EXIT_STATE;
				break;
			}

			protocol = GetProtocol(ptr->buf);				// �������� �и�

			UnPack_msg(ptr->buf, size, msg);						// �޽��� ����ŷ

			switch (atoi(msg))		// ���� �� �����Ͽ�����
			{
			case SELECT_TYPE::ONE:
				strcpy(msg, ROOM_ONE);
				break;
			case SELECT_TYPE::TWO:
				strcpy(msg, ROOM_TWO);
				break;
			case SELECT_TYPE::THREE:
				strcpy(msg, ROOM_THREE);
				break;
			default:
				printf("�߸� ������\n");
				continue;
			}

			/*
			if (atoi(msg) < 1 || 3 < atoi(msg))
			{
				printf("�߸� ������\n");
				continue;
			}
			*/

			protocol = CHAT;

			strcpy(MULTICASTIP, msg);			// Ŭ���̾�Ʈ�� ������ ������ ����

			size = Pack_msg(ptr->buf, protocol, msg);

			retval = send(ptr->sock, ptr->buf, size, 0);	// ������ ���� ������ ������
			if (retval == SOCKET_ERROR)
			{
				err_display("lobby protocol send()");
				break;
			}

			printf("������ ����\n");

			ptr->state = CHAT_STATE;

			break;
		case CHAT_STATE:
			if (!PacketRecv(ptr->sock, ptr->buf))			// Ŭ���̾�Ʈ ������ ���� ��ٸ���
			{
				ptr->state = EXIT_STATE;
				break;
			}

			protocol = GetProtocol(ptr->buf);


			if (!createOnce)		// �ѹ��� ���ϻ��� �ϵ���
			{
				createOnce = true;
				multiSender = CreateMSendSocket(MULTICASTIP);
			}
			else // �̹� ������ �������ִ� ���, ��Ƽĳ��Ʈ �����Ǹ� ����
			{
				multiSender->sockaddr.sin_addr.s_addr = inet_addr(MULTICASTIP);
			}

			sprintf(msg, "<SYSTEM> %s ���� �����Ͽ����ϴ�.", user.name);


			// ���� �޽��� ������
			retval = sendto(multiSender->sock, msg, strlen(msg), 0,
				(SOCKADDR*)&multiSender->sockaddr, sizeof(multiSender->sockaddr));
			if (retval == SOCKET_ERROR) {
				err_display("sendto()");
				continue;
			}

			if (!PacketRecv(ptr->sock, ptr->buf))			// Ŭ���̾�Ʈ ������ ���� ��ٸ���
			{
				ptr->state = EXIT_STATE;
				break;
			}

			protocol = GetProtocol(ptr->buf);

			sprintf(msg, "<SYSTEM> %s ���� �����Ͽ����ϴ�.", user.name);

			// ���� �޽��� ������
			retval = sendto(multiSender->sock, msg, strlen(msg), 0,
				(SOCKADDR*)&multiSender->sockaddr, sizeof(multiSender->sockaddr));
			if (retval == SOCKET_ERROR) {
				err_display("sendto()");
				continue;
			}

			// Ŭ���̾�Ʈ ���ú� ������ ���� ���� ��Ŷ�ϳ� ������
			protocol = LOBBY;

			size = Pack_protocol(ptr->buf, protocol);		// �κ� �������� ��ŷ

			retval = send(ptr->sock, ptr->buf, size, 0);	// �κ� �������� ������.
			if (retval == SOCKET_ERROR)
			{
				err_display("lobby protocol send()");
				break;
			}

			ptr->state = LOBBY_STATE;

			break;
		case EXIT_STATE:
			endflag = true;
			break;
		}

		if (endflag)
			break;
	}

	return 0;
}

DWORD WINAPI RemoveClient(LPVOID _ptr)			// Ŭ���̾�Ʈ �� ������ ����
{
	while (1)
	{
		int index = WaitForMultipleObjects(ThreadCount, hThread, false, INFINITE);	// ������ ���Ḧ ��ٸ���
		index -= WAIT_OBJECT_0;
		EnterCriticalSection(&cs);
		if (index == 0)
		{
			LeaveCriticalSection(&cs);
			continue;
		}

		_ClientInfo* ptr = SearchClientInfo(hThread[index]);		// �����尡 ����ϴ� Ŭ���̾�Ʈ�� ã�´�
		if (ptr == nullptr)					// Ŭ���̾�Ʈ�� ������?
		{
			RemoveThread(hThread[index]);	// �ٷ� ���� ��
			LeaveCriticalSection(&cs);		// Ű �ݳ�
			continue;
		}

		RemoveThread(hThread[index]);		// ������ ���� ����

		ptr->hthread = nullptr;

		switch (ptr->state)					// Ŭ���̾�Ʈ�� ������ ���¿� ���� ���� ����
		{
		case INIT_STATE:
		case SET_STATE:
		case LOBBY_STATE:
		case CHAT_STATE:
		case EXIT_STATE:

			RemoveClientInfo(ptr);			// Ŭ���̾�Ʈ ���� ����
			break;
		}

		LeaveCriticalSection(&cs);
	}
}

PROTOCOL GetProtocol(const char* _ptr)
{
	PROTOCOL protocol;
	memcpy(&protocol, _ptr, sizeof(PROTOCOL));

	return protocol;
}

// ���� �Լ� ���� ��� �� ����
void err_quit(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}
// ���� �Լ� ���� ���
void err_display(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char*)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

bool PacketRecv(SOCKET _sock, char* _buf)
{
	int size;

	int retval = recvn(_sock, (char*)&size, sizeof(size), 0);
	if (retval == SOCKET_ERROR)
	{
		err_display("recv error()");
		return false;
	}
	else if (retval == 0)
	{
		return false;
	}

	retval = recvn(_sock, _buf, size, 0);
	if (retval == SOCKET_ERROR)
	{
		err_display("recv error()");
		return false;

	}
	else if (retval == 0)
	{
		return false;
	}

	return true;
}


// ��ŷ �Լ� ����
int Pack_protocol(char* _buf, PROTOCOL _protocol)
{
	int size = 0;
	char* ptr = _buf;

	ptr = ptr + sizeof(size);

	memcpy(ptr, &_protocol, sizeof(_protocol));
	ptr = ptr + sizeof(_protocol);
	size = size + sizeof(_protocol);

	ptr = _buf;
	memcpy(ptr, &size, sizeof(size));

	size = size + sizeof(size);

	return size;
}
int Pack_msg(char* _buf, PROTOCOL _protocol, const char* msg)
{
	int size = 0;
	char* ptr = _buf;
	int strsize = strlen(msg);

	ptr = ptr + sizeof(size);

	memcpy(ptr, &_protocol, sizeof(_protocol));
	ptr = ptr + sizeof(_protocol);
	size = size + sizeof(_protocol);

	memcpy(ptr, &strsize, sizeof(strsize));
	ptr = ptr + sizeof(strsize);
	size = size + sizeof(strsize);

	memcpy(ptr, msg, strsize);
	ptr = ptr + strsize;
	size = size + strsize;

	ptr = _buf;
	memcpy(ptr, &size, sizeof(size));

	size = size + sizeof(size);

	return size;
}


// ����ŷ �Լ� ����
void UnPack_userInfo(const char* _buf, _UserInfo* user)
{
	const char* ptr = _buf + sizeof(PROTOCOL);
	int strsize;

	memcpy(&strsize, ptr, sizeof(int));
	ptr = ptr + sizeof(int);

	memcpy(user->name, ptr, strsize);
	ptr = ptr + strsize;
}
void UnPack_msg(const char* _buf, int& _size, char* msg)
{
	const char* ptr = _buf + sizeof(PROTOCOL);

	memcpy(&_size, ptr, sizeof(_size));
	ptr = ptr + sizeof(_size);

	memcpy(msg, ptr, _size);
}


SockInfo* CreateMSendSocket(const char* MULTICASTIP)
{
	SockInfo* sockinfo = new SockInfo();
	ZeroMemory(sockinfo, sizeof(sockinfo));

	int retval;

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET) err_quit("socket()");

	// ��Ƽĳ��Ʈ TTL ����
	int ttl = 2;		// ����͸� � ���������� �ִ���??  ����� ���������� -1 -> 0�Ǹ� ����
	retval = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,		// time to live? �������
		(char*)&ttl, sizeof(ttl));
	if (retval == SOCKET_ERROR) err_quit("setsockopt()");

	// ���� �ּ� ����ü �ʱ�ȭ
	SOCKADDR_IN remoteaddr;
	ZeroMemory(&remoteaddr, sizeof(remoteaddr));
	remoteaddr.sin_family = AF_INET;
	remoteaddr.sin_addr.s_addr = inet_addr(MULTICASTIP);
	remoteaddr.sin_port = htons(REMOTEPORT);

	sockinfo->sock = sock;
	memcpy(&sockinfo->sockaddr, &remoteaddr, sizeof(SOCKADDR_IN));

	return sockinfo;
}

ip_mreq Add_Membership(SOCKET sock, const char* MULTICASTIP)
{
	int retval;

	struct ip_mreq mreq;
	mreq.imr_multiaddr.s_addr = inet_addr(MULTICASTIP);		// ���� �ּҷ� ����
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);			// �� �ּҰ� ����
	retval = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		(char*)&mreq, sizeof(mreq));
	if (retval == SOCKET_ERROR) err_quit("setsockopt()");

	return mreq;
}

void Drop_Membership(SockInfo* ptr)
{
	int retval;

	retval = setsockopt(ptr->sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		(char*)&ptr->mreq, sizeof(ptr->mreq));
	if (retval == SOCKET_ERROR) err_quit("setsockopt()");
}
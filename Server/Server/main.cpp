#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

/*
	서버에서는 클라이언트가 접속할 때마다
	클라이언트의 정보를 클라이언트인포 구조체로 저장하여
	클라이언트인포 배열에 저장하고
	각 클라이언트마다 스레드를 생성시켜서
	클라이언트가 종료할때까지 통신하도록 한다.
	스레드 시작 후

*/

#define ROOMLIST "리스트\r\n1.박용택방\r\n2.오지환방\r\n3.이천웅방\r\n"
#define ROOM_ONE "225.0.0.1"
#define ROOM_TWO "225.0.0.2"
#define ROOM_THREE "225.0.0.3"
#define MAX_COUNT 100			// 클라이언트 배열 최대 카운트
#define SIZE 20					// 유저 이름 사이즈
#define SERVERPORT 9000
#define REMOTEPORT 9001
#define BUFSIZE    512

enum PROTOCOL
{
	CREATE_USER = 0,		// 유저 이름 설정
	LOBBY,					// 로비, 방 선택하기
	CHAT,					// 채팅하기
	EXIT					// 종료
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


typedef struct user					// 유저 정보
{
	char name[SIZE];
}_UserInfo;

typedef struct clientinfo			// 클라이언트 정보
{
	SOCKET sock;
	SOCKADDR_IN addr;
	char buf[BUFSIZE];
	_UserInfo* user;			// 유저 정보를 받기 위한 것
	Client_State state;			// 상태
	HANDLE wait_exit_event;		// 클라이언트 종료 이벤트
	HANDLE hthread;				// 나를 담당하는 스레드
}_ClientInfo;

struct SockInfo
{
	SOCKET sock;				// 소켓
	SOCKADDR_IN sockaddr;		// 소켓 주소 구조체
	ip_mreq mreq;
};

_UserInfo* UserInfoArr[MAX_COUNT];		// 유저 정보들

_ClientInfo* ClientInfo[MAX_COUNT];		// 클라이언트 정보들
int userCount = 0;
int clientCount = 0;

HANDLE hThread[MAX_COUNT];		// 현재 생성된 스레드들
int ThreadCount = 0;

//크리티컬섹션, 클라이언트 종료 시 삭제할 때 필요
CRITICAL_SECTION cs;

void err_quit(char* msg);								// 소켓 함수 오류 출력 후 종료
void err_display(char* msg);							// 소켓 함수 오류 출력
int recvn(SOCKET s, char* buf, int len, int flags);

// 소켓 생성
SOCKET TCP_sock_init();									// 소켓 생성
SockInfo* CreateMSendSocket(const char* MULTICASTIP);		// udp 멀티캐스트 send 소켓 생성
ip_mreq Add_Membership(SOCKET sock, const char* MULTICASTIP);		// 멀티캐스트 가입
void Drop_Membership(SockInfo* ptr);								// 멀티캐스트 탈퇴

// 유저관련 기능
void AddUserInfo(_UserInfo user);

// 클라이언트 관련 기능
_ClientInfo* AddClientInfo(SOCKET _sock, SOCKADDR_IN _addr);		// 클라이언트 추가
void RemoveClientInfo(_ClientInfo* _ptr);							// 클라이언트 삭제
_ClientInfo* SearchClientInfo(HANDLE _hthread);					// 해당 스레드를 가진 클라이언트 검색

bool PacketRecv(SOCKET _sock, char* _buf);
PROTOCOL GetProtocol(const char* _ptr);


/*
	팩킹
*/
int Pack_protocol(char* _buf, PROTOCOL _protocol);
int Pack_msg(char* _buf, PROTOCOL _protocol, const char* msg);

/*
	언팩킹
*/
void UnPack_userInfo(const char* _buf, _UserInfo* user);
void UnPack_msg(const char* _buf, int& _size, char* msg);


/*
	스레드 관련
*/
DWORD WINAPI ProcessClient(LPVOID);	// 클라이언트 담당 스레드
DWORD WINAPI RemoveClient(LPVOID);	// 클라이언트 및 스레드 삭제 스레드

bool AddThread(LPTHREAD_START_ROUTINE process, _ClientInfo* _ptr);	// 스레드 추가
void RemoveThread(HANDLE _hthread);									// 스레드 삭제

int main(int argc, char* argv[])
{
	int retval;

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;
	InitializeCriticalSection(&cs);
	// 소켓 초기화
	SOCKET listen_sock = TCP_sock_init();

	// 데이터 통신에 사용할 변수
	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;

	// 0번째에 이벤트 넣기, ThreadCount 갱신하기 위함
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

		// 클라이언트 배열에 클라이언트 정보 추가
		_ClientInfo* ptr = AddClientInfo(client_sock, clientaddr);

		// 스레드 만들고, 스레드 배열에 스레드 추가
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

// 유저 관련 함수 정의
void AddUserInfo(_UserInfo user)
{
	EnterCriticalSection(&cs);

	UserInfoArr[userCount] = new _UserInfo;
	strcpy(UserInfoArr[userCount]->name, user.name);
	userCount++;

	LeaveCriticalSection(&cs);
}

// 클라이언트 정보 관련 함수 정의
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

	printf("\n[TCP 서버] 클라이언트 접속: IP 주소=%s, 포트 번호=%d\n",
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

			printf("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\n",
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

// 클라이언트 관련 스레드
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
			if (!PacketRecv(ptr->sock, ptr->buf))		// 유저 이름 받는다
			{
				ptr->state = EXIT_STATE;
				break;
			}

			protocol = GetProtocol(ptr->buf);			// 프로토콜 분리

			UnPack_userInfo(ptr->buf, &user);			// 유저 정보 언팩킹

			//strcpy(ptr->user->name, user.name);
			//printf("%s\n", ptr->user->name);

			protocol = LOBBY;

			size = Pack_protocol(ptr->buf, protocol);		// 로비 프로토콜 팩킹

			retval = send(ptr->sock, ptr->buf, size, 0);	// 로비 프로토콜 보낸다.
			if (retval == SOCKET_ERROR)
			{
				err_display("lobby protocol send()");
				break;
			}

			ptr->state = LOBBY_STATE;
			break;
		case LOBBY_STATE:
			size = Pack_msg(ptr->buf, LOBBY, ROOMLIST);		// 방리스트 메시지 팩킹

			retval = send(ptr->sock, ptr->buf, size, 0);			// 방리스트 보내기
			if (retval == SOCKET_ERROR)
			{
				err_display("lobby protocol send()");
				break;
			}

			printf("방리스트 보냄\n");

			if (!PacketRecv(ptr->sock, ptr->buf))			// 무슨 방 선택했는지 받기
			{
				ptr->state = EXIT_STATE;
				break;
			}

			protocol = GetProtocol(ptr->buf);				// 프로토콜 분리

			UnPack_msg(ptr->buf, size, msg);						// 메시지 언팩킹

			switch (atoi(msg))		// 무슨 방 선택하였는지
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
				printf("잘못 선택함\n");
				continue;
			}

			/*
			if (atoi(msg) < 1 || 3 < atoi(msg))
			{
				printf("잘못 선택함\n");
				continue;
			}
			*/

			protocol = CHAT;

			strcpy(MULTICASTIP, msg);			// 클라이언트가 선택한 아이피 저장

			size = Pack_msg(ptr->buf, protocol, msg);

			retval = send(ptr->sock, ptr->buf, size, 0);	// 선택한 방의 아이피 보내기
			if (retval == SOCKET_ERROR)
			{
				err_display("lobby protocol send()");
				break;
			}

			printf("아이피 보냄\n");

			ptr->state = CHAT_STATE;

			break;
		case CHAT_STATE:
			if (!PacketRecv(ptr->sock, ptr->buf))			// 클라이언트 나갈때 까지 기다리기
			{
				ptr->state = EXIT_STATE;
				break;
			}

			protocol = GetProtocol(ptr->buf);


			if (!createOnce)		// 한번만 소켓생성 하도록
			{
				createOnce = true;
				multiSender = CreateMSendSocket(MULTICASTIP);
			}
			else // 이미 소켓이 생성되있는 경우, 멀티캐스트 아이피만 변경
			{
				multiSender->sockaddr.sin_addr.s_addr = inet_addr(MULTICASTIP);
			}

			sprintf(msg, "<SYSTEM> %s 님이 입장하였습니다.", user.name);


			// 입장 메시지 보내기
			retval = sendto(multiSender->sock, msg, strlen(msg), 0,
				(SOCKADDR*)&multiSender->sockaddr, sizeof(multiSender->sockaddr));
			if (retval == SOCKET_ERROR) {
				err_display("sendto()");
				continue;
			}

			if (!PacketRecv(ptr->sock, ptr->buf))			// 클라이언트 나갈때 까지 기다리기
			{
				ptr->state = EXIT_STATE;
				break;
			}

			protocol = GetProtocol(ptr->buf);

			sprintf(msg, "<SYSTEM> %s 님이 퇴장하였습니다.", user.name);

			// 퇴장 메시지 보내기
			retval = sendto(multiSender->sock, msg, strlen(msg), 0,
				(SOCKADDR*)&multiSender->sockaddr, sizeof(multiSender->sockaddr));
			if (retval == SOCKET_ERROR) {
				err_display("sendto()");
				continue;
			}

			// 클라이언트 리시브 스레드 끄기 위해 패킷하나 보내기
			protocol = LOBBY;

			size = Pack_protocol(ptr->buf, protocol);		// 로비 프로토콜 팩킹

			retval = send(ptr->sock, ptr->buf, size, 0);	// 로비 프로토콜 보낸다.
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

DWORD WINAPI RemoveClient(LPVOID _ptr)			// 클라이언트 및 스레드 삭제
{
	while (1)
	{
		int index = WaitForMultipleObjects(ThreadCount, hThread, false, INFINITE);	// 스레드 종료를 기다린다
		index -= WAIT_OBJECT_0;
		EnterCriticalSection(&cs);
		if (index == 0)
		{
			LeaveCriticalSection(&cs);
			continue;
		}

		_ClientInfo* ptr = SearchClientInfo(hThread[index]);		// 스레드가 담당하던 클라이언트를 찾는다
		if (ptr == nullptr)					// 클라이언트가 없으면?
		{
			RemoveThread(hThread[index]);	// 바로 삭제 후
			LeaveCriticalSection(&cs);		// 키 반납
			continue;
		}

		RemoveThread(hThread[index]);		// 스레드 먼저 삭제

		ptr->hthread = nullptr;

		switch (ptr->state)					// 클라이언트의 마지막 상태에 따라서 삭제 진행
		{
		case INIT_STATE:
		case SET_STATE:
		case LOBBY_STATE:
		case CHAT_STATE:
		case EXIT_STATE:

			RemoveClientInfo(ptr);			// 클라이언트 삭제 진행
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

// 소켓 함수 오류 출력 후 종료
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
// 소켓 함수 오류 출력
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


// 팩킹 함수 정의
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


// 언팩킹 함수 정의
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

	// 멀티캐스트 TTL 설정
	int ttl = 2;		// 라우터를 몇개 빠져나갈수 있는지??  라우터 지날때마다 -1 -> 0되면 폐기됨
	retval = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,		// time to live? 살아있음
		(char*)&ttl, sizeof(ttl));
	if (retval == SOCKET_ERROR) err_quit("setsockopt()");

	// 소켓 주소 구조체 초기화
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
	mreq.imr_multiaddr.s_addr = inet_addr(MULTICASTIP);		// 여기 주소로 가입
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);			// 이 주소가 위의
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
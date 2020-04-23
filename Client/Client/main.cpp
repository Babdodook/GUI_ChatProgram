#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include"resource.h"

#define SERVERIP   "127.0.0.1"
#define SERVERPORT 9000
#define LOCALPORT   9001
#define REMOTEPORT 9001
#define BUFSIZE    512
#define SIZE 20

enum PROTOCOL
{
	CREATE_USER = 0,		// 유저 이름 설정
	LOBBY,					// 로비, 방 선택하기
	CHAT,					// 채팅하기
	EXIT					// 종료
};

typedef struct user					// 유저 정보
{
	char name[SIZE];
}_UserInfo;

_UserInfo user;

void err_quit(char* msg);
void err_display(char* msg);
int recvn(SOCKET s, char* buf, int len, int flags);

struct SockInfo					// udp통신할때 필요한 소켓정보 구조체
{
	SOCKET sock;				// 소켓
	SOCKADDR_IN sockaddr;		// 소켓 주소 구조체
	ip_mreq mreq;
};

bool PacketRecv(SOCKET _sock, char* _buf);
PROTOCOL GetProtocol(const char* _ptr);

// 소켓 만들기
SOCKET TCP_sock_init();
SockInfo* CreateNormalSocket();								// 단순 UDP클라이언트 소켓 생성
SockInfo* CreateMRecvSocket(const char* MULTICASTIP);		// 멀티캐스트 받기 소켓 생성
SockInfo* CreateMSendSocket(const char* MULTICASTIP);		// 멀티캐스트 보내기 소켓 생성

ip_mreq Add_Membership(SOCKET sock, const char* MULTICASTIP);	// 멀티캐스트 그룹 가입
void Drop_Membership(SockInfo* ptr);							// 멀티캐스트 그룹 탈퇴


/*
	팩킹
*/
int Pack_userInfo(char* _buf, PROTOCOL _protocol, _UserInfo user);
int Pack_msg(char* _buf, PROTOCOL _protocol, const char* msg);
int Pack_protocol(char* _buf, PROTOCOL _protocol);

/*
	언팩킹
*/
void UnPack_msg(const char* _buf, int& _size, char* msg);

// 스레드
DWORD WINAPI SendThread(LPVOID);
DWORD WINAPI ReceiveThread(LPVOID);
// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg);
// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// 편집 컨트롤 출력 함수
void DisplayText(char* fmt, ...);

char buf[BUFSIZE + 1];				// 데이터 송수신 버퍼
HANDLE hReadEvent, hWriteEvent;		// 이벤트
HWND hSendButton;					// 보내기 버튼
HWND hEnterButton;					// 입장 버튼
HWND hExitButton;					// 나가기 버튼
HWND hEdit1, hEdit2;				// 편집 컨트롤

HANDLE wait_chat_event = CreateEvent(nullptr, false, false, nullptr);;		// 채팅 끝나는 이벤트

// socket()
SOCKET TCPsock;				// tcp통신을 위한 소켓

int listSelect = 0;			// 입력창에서 숫자를 받기위한 전역 변수
bool closeThread = false;	// 스레드를 종료하기 위한 bool값, 나가기 누르면 true됨

HWND hDlg2;				// 전역으로 쓰기 위한 다이얼로그 핸들

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// 이벤트 생성
	hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (hReadEvent == NULL) return 1;
	hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (hWriteEvent == NULL) return 1;

	// 소켓 통신 스레드 생성
	CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);

	// 대화상자 생성
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// 이벤트 제거
	CloseHandle(hReadEvent);
	CloseHandle(hWriteEvent);

	// closesocket()
	closesocket(TCPsock);

	// 윈속 종료
	WSACleanup();
	return 0;
}


// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_INITDIALOG:
		hDlg2 = hDlg;
		hEdit1 = GetDlgItem(hDlg, IDC_EDIT1);
		hEdit2 = GetDlgItem(hDlg, IDC_EDIT2);
		hSendButton = GetDlgItem(hDlg, IDOK);
		hEnterButton = GetDlgItem(hDlg, IDENTER);
		hExitButton = GetDlgItem(hDlg, IDEXIT);
		EnableWindow(hEnterButton, FALSE);
		EnableWindow(hExitButton, FALSE);
		DisplayText("유저 이름 입력\r\n");
		SendMessage(hEdit1, EM_SETLIMITTEXT, BUFSIZE, 0);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			EnableWindow(hSendButton, FALSE);					// 보내기 버튼 비활성화
			WaitForSingleObject(hReadEvent, INFINITE);			// 읽기 완료 기다리기
			GetDlgItemText(hDlg, IDC_EDIT1, buf, BUFSIZE + 1);

			SetEvent(hWriteEvent);								// 쓰기 완료 알리기
			SetFocus(hEdit1);
			SendMessage(hEdit1, EM_SETSEL, 0, -1);

			SetDlgItemText(hDlg, IDC_EDIT1, "");
			return TRUE;
		case IDENTER:
			WaitForSingleObject(hReadEvent, INFINITE);						// 읽기 완료 기다리기
			listSelect = GetDlgItemInt(hDlg, IDC_EDIT1, FALSE, NULL);		// 정수 받아오기

			SetEvent(hWriteEvent);		// 쓰기 이벤트 켜기
			SetFocus(hEdit1);
			SendMessage(hEdit1, EM_SETSEL, 0, -1);

			SetDlgItemText(hDlg, IDC_EDIT1, "");
			SetDlgItemText(hDlg, IDC_EDIT2, "");
			return TRUE;
		case IDEXIT:
			closeThread = true;
			EnableWindow(hExitButton, FALSE);		// 나가기 버튼 비활성화
			SetEvent(hWriteEvent);					// 쓰기 이벤트 켜기
			SetEvent(wait_chat_event);				// 나가기 알리기

			SetDlgItemText(hDlg, IDC_EDIT1, "");
			SetDlgItemText(hDlg, IDC_EDIT2, "");
			return TRUE;
		}
		return FALSE;
	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return FALSE;
}

// TCP 클라이언트 시작 부분
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;

	TCPsock = TCP_sock_init();

	PROTOCOL protocol = CREATE_USER;


	char msg[BUFSIZE];
	bool endflag = false;
	int size;
	char MULTICASTIP[50];

	int selectroom;

	// 멀티캐스트를 위한 소켓 생성
	SockInfo* mrecvSock;		// 멀티캐스트 받기용 소켓 (bind)
	SockInfo* msendSock;		// 멀티캐스트 보내기용 소켓

	// 스레드 생성
	HANDLE Send;
	HANDLE Receive;


	bool createOnce = false;

	while (1)
	{
		switch (protocol)
		{
		case CREATE_USER:
			WaitForSingleObject(hWriteEvent, INFINITE); // 쓰기 완료 기다리기

			// 문자열 길이가 0이면 보내지 않음
			if (strlen(buf) == 0) {
				EnableWindow(hSendButton, TRUE);	// 보내기 버튼 활성화
				SetEvent(hReadEvent);				// 읽기 완료 알리기
				continue;
			}

			strcpy(user.name, buf);		// 버퍼에 있는 유저이름 저장

			size = Pack_userInfo(buf, protocol, user);

			retval = send(TCPsock, buf, size, 0);			// 서버에게 보낸다
			if (retval == SOCKET_ERROR)
			{
				err_display("userinfo send()");
				break;
			}

			if (!PacketRecv(TCPsock, buf))		// 서버로부터 답신을 받고(프로토콜 받음)
			{
				break;
			}

			protocol = GetProtocol(buf);	// 프로토콜 세팅

			SetEvent(hReadEvent); // 읽기 완료 알리기

			SetDlgItemText(hDlg2, IDC_EDIT2, "");		// 출력창 지우기

			break;
		case LOBBY:

			EnableWindow(hSendButton, FALSE);			// 보내기 버튼 비활성화
			EnableWindow(hEnterButton, TRUE);			// 입장 버튼 활성화

			if (!PacketRecv(TCPsock, buf))			// 방 리스트 받기
			{
				endflag = true;
				break;
			}

			protocol = GetProtocol(buf);				// 프로토콜 분리

			UnPack_msg(buf, size, msg);					// 방리스트 언팩킹

			msg[size] = '\0';
			DisplayText("%s\n", msg);						// 받음 방리스트 출력

			WaitForSingleObject(hWriteEvent, INFINITE); // 쓰기 완료 기다리기

			_itoa(listSelect, msg, 10);

			// 문자열 길이가 0이면 보내지 않음
			if (strlen(msg) == 0) {
				EnableWindow(hEnterButton, TRUE);		// 입장 버튼 활성화
				SetEvent(hReadEvent);					// 읽기 완료 알리기
				continue;
			}

			size = Pack_msg(buf, protocol, msg);
			retval = send(TCPsock, buf, size, 0);		// 선택한 방 보내기
			if (retval == SOCKET_ERROR)
			{
				err_display("userinfo send()");
				break;
			}

			// 1~3 외에 선택시 다시 선택하도록
			if (listSelect < 1 || 3 < listSelect) {
				SetEvent(hReadEvent);				// 읽기 완료 알리기
				continue;
			}

			if (!PacketRecv(TCPsock, buf))			// 멀티캐스트 아이피 받기
			{
				break;
			}

			protocol = GetProtocol(buf);			// 프로토콜 분리

			ZeroMemory(MULTICASTIP, sizeof(MULTICASTIP));	// 초기화
			UnPack_msg(buf, size, MULTICASTIP);			// 멀티캐스트 아이피 언팩킹

			MULTICASTIP[size] = '\0';

			DisplayText("<SYSTEM> %d번 방에 입장하였습니다 \r\n", listSelect);

			EnableWindow(hSendButton, TRUE);		// 보내기 버튼 활성화
			SetEvent(hReadEvent);					// 읽기 완료 알리기

			break;
		case CHAT:
			EnableWindow(hEnterButton, FALSE);		// 입장 버튼 비활성화
			EnableWindow(hExitButton, TRUE);		// 나가기 버튼 활성화

			if (!createOnce)		// 한번만 소켓 생성하도록
			{
				createOnce = true;

				// 멀티캐스트를 위한 소켓 생성
				mrecvSock = CreateMRecvSocket(MULTICASTIP);		// 멀티캐스트 받기용 소켓 (bind)
				msendSock = CreateMSendSocket(MULTICASTIP);		// 멀티캐스트 보내기용 소켓
			}
			else // 이미 생성된 경우 멀티캐스트 그룹만 가입 ( SEND는 가입할 필요가 없다. 아이피주소만 지정)
			{
				mrecvSock->mreq = Add_Membership(mrecvSock->sock, MULTICASTIP);
				msendSock->sockaddr.sin_addr.s_addr = inet_addr(MULTICASTIP);
			}

			// 스레드 생성
			Receive = CreateThread(NULL, 0, ReceiveThread, mrecvSock, 0, NULL);
			Send = CreateThread(NULL, 0, SendThread, msendSock, 0, NULL);

			protocol = CHAT;

			size = Pack_protocol(buf, protocol);
			retval = send(TCPsock, buf, size, 0);			// 나간다는 프로토콜 보내기
			if (retval == SOCKET_ERROR)
			{
				err_display("exit room send()");
				break;
			}

			while (1)
			{
				WaitForSingleObject(wait_chat_event, INFINITE);

				CloseHandle(Receive);
				CloseHandle(Send);

				break;
			}

			protocol = EXIT;

			size = Pack_protocol(buf, protocol);
			retval = send(TCPsock, buf, size, 0);			// 나간다는 프로토콜 보내기
			if (retval == SOCKET_ERROR)
			{
				err_display("exit room send()");
				break;
			}

			if (!PacketRecv(TCPsock, buf))		// 리시브 스레드 종료를 위해 패킷하나 받기
			{
				break;
			}

			protocol = GetProtocol(buf);	// 프로토콜 세팅

			// 멀티캐스트 그룹 탈퇴
			Drop_Membership(mrecvSock);

			closeThread = false;

			break;
		}
	}

	closesocket(TCPsock);

	WSACleanup();
	system("pause");
	return 0;
}

// 입력 스레드
DWORD WINAPI SendThread(LPVOID _ptr)
{
	SockInfo* info = (SockInfo*)_ptr;

	int retval;
	char msg[BUFSIZE + 1];
	int len;

	while (1)
	{
		WaitForSingleObject(hWriteEvent, INFINITE); // 쓰기 완료 기다리기

		if (closeThread)
			break;

		// 문자열 길이가 0이면 보내지 않음
		if (strlen(buf) == 0) {
			EnableWindow(hSendButton, TRUE); // 보내기 버튼 활성화
			SetEvent(hReadEvent); // 읽기 완료 알리기
			continue;
		}

		// '\n' 문자 제거
		len = strlen(buf);
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		if (strlen(buf) == 0)
			break;

		sprintf(msg, "[%s] %s", user.name, buf);

		// 데이터 보내기
		retval = sendto(info->sock, msg, strlen(msg), 0,
			(SOCKADDR*)&info->sockaddr, sizeof(info->sockaddr));
		if (retval == SOCKET_ERROR) {
			err_display("sendto()");
			continue;
		}

		EnableWindow(hSendButton, TRUE); // 보내기 버튼 활성화
		SetEvent(hReadEvent); // 읽기 완료 알리기
	}

	return 0;
}

// 받기 스레드
DWORD WINAPI ReceiveThread(LPVOID _ptr)
{
	SockInfo* info = (SockInfo*)_ptr;

	int retval;
	SOCKADDR_IN peeraddr;
	int addrlen;

	while (1)
	{
		// 데이터 받기
		addrlen = sizeof(peeraddr);
		retval = recvfrom(info->sock, buf, BUFSIZE, 0,
			(SOCKADDR*)&peeraddr, &addrlen);
		if (retval == SOCKET_ERROR) {
			err_display("recvfrom()");
			continue;
		}

		buf[retval] = '\0';
		DisplayText("%s\n", buf);

		if (closeThread)
			break;
	}

	return 0;
}


// 편집 컨트롤 출력 함수
void DisplayText(char* fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[BUFSIZE + 256];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(hEdit2);
	SendMessage(hEdit2, EM_SETSEL, nLength, nLength);
	SendMessage(hEdit2, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

	va_end(arg);
}


SockInfo* CreateNormalSocket()
{
	SockInfo* sockinfo = new SockInfo();
	ZeroMemory(sockinfo, sizeof(sockinfo));

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET) err_quit("socket()");

	// 소켓 주소 구조체 초기화
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(SERVERIP);
	serveraddr.sin_port = htons(SERVERPORT);

	sockinfo->sock = sock;
	memcpy(&sockinfo->sockaddr, &serveraddr, sizeof(SOCKADDR_IN));

	return sockinfo;
}

SockInfo* CreateMRecvSocket(const char* MULTICASTIP)
{
	SockInfo* sockinfo = new SockInfo();
	ZeroMemory(sockinfo, sizeof(sockinfo));

	int retval;

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET) err_quit("socket()");

	// SO_REUSEADDR 옵션 설정
	BOOL optval = TRUE;
	retval = setsockopt(sock, SOL_SOCKET,
		SO_REUSEADDR, (char*)&optval, sizeof(optval));
	if (retval == SOCKET_ERROR) err_quit("setsockopt()");

	// bind()
	SOCKADDR_IN localaddr;
	ZeroMemory(&localaddr, sizeof(localaddr));
	localaddr.sin_family = AF_INET;
	localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	localaddr.sin_port = htons(LOCALPORT);
	retval = bind(sock, (SOCKADDR*)&localaddr, sizeof(localaddr));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	// 멀티캐스트 그룹 가입
	struct ip_mreq mreq;
	mreq.imr_multiaddr.s_addr = inet_addr(MULTICASTIP);		// 여기 주소로 가입
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);			// 이 주소가 위의
	retval = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		(char*)&mreq, sizeof(mreq));
	if (retval == SOCKET_ERROR) err_quit("setsockopt()");

	sockinfo->sock = sock;
	memcpy(&sockinfo->sockaddr, &localaddr, sizeof(SOCKADDR_IN));
	sockinfo->mreq = mreq;

	return sockinfo;
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

SOCKET TCP_sock_init()
{
	// socket()
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) err_quit("socket()");

	// connect()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(SERVERIP);
	serveraddr.sin_port = htons(SERVERPORT);
	int retval = connect(sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) err_quit("connect()");

	return sock;
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

PROTOCOL GetProtocol(const char* _ptr)
{
	PROTOCOL protocol;
	memcpy(&protocol, _ptr, sizeof(PROTOCOL));

	return protocol;
}

//팩킹 함수 정의
int Pack_userInfo(char* _buf, PROTOCOL _protocol, _UserInfo user)
{
	int size = 0;
	char* ptr = _buf;
	int strsize = strlen(user.name);

	ptr = ptr + sizeof(size);

	memcpy(ptr, &_protocol, sizeof(_protocol));
	ptr = ptr + sizeof(_protocol);
	size = size + sizeof(_protocol);

	memcpy(ptr, &strsize, sizeof(strsize));
	ptr = ptr + sizeof(strsize);
	size = size + sizeof(strsize);

	memcpy(ptr, user.name, strsize);
	ptr = ptr + strsize;
	size = size + strsize;

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

//언팩킹 함수 정의
void UnPack_msg(const char* _buf, int& _size, char* msg)
{
	const char* ptr = _buf + sizeof(PROTOCOL);

	memcpy(&_size, ptr, sizeof(_size));
	ptr = ptr + sizeof(_size);

	memcpy(msg, ptr, _size);
}
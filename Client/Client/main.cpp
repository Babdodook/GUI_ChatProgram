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
	CREATE_USER = 0,		// ���� �̸� ����
	LOBBY,					// �κ�, �� �����ϱ�
	CHAT,					// ä���ϱ�
	EXIT					// ����
};

typedef struct user					// ���� ����
{
	char name[SIZE];
}_UserInfo;

_UserInfo user;

void err_quit(char* msg);
void err_display(char* msg);
int recvn(SOCKET s, char* buf, int len, int flags);

struct SockInfo					// udp����Ҷ� �ʿ��� �������� ����ü
{
	SOCKET sock;				// ����
	SOCKADDR_IN sockaddr;		// ���� �ּ� ����ü
	ip_mreq mreq;
};

bool PacketRecv(SOCKET _sock, char* _buf);
PROTOCOL GetProtocol(const char* _ptr);

// ���� �����
SOCKET TCP_sock_init();
SockInfo* CreateNormalSocket();								// �ܼ� UDPŬ���̾�Ʈ ���� ����
SockInfo* CreateMRecvSocket(const char* MULTICASTIP);		// ��Ƽĳ��Ʈ �ޱ� ���� ����
SockInfo* CreateMSendSocket(const char* MULTICASTIP);		// ��Ƽĳ��Ʈ ������ ���� ����

ip_mreq Add_Membership(SOCKET sock, const char* MULTICASTIP);	// ��Ƽĳ��Ʈ �׷� ����
void Drop_Membership(SockInfo* ptr);							// ��Ƽĳ��Ʈ �׷� Ż��


/*
	��ŷ
*/
int Pack_userInfo(char* _buf, PROTOCOL _protocol, _UserInfo user);
int Pack_msg(char* _buf, PROTOCOL _protocol, const char* msg);
int Pack_protocol(char* _buf, PROTOCOL _protocol);

/*
	����ŷ
*/
void UnPack_msg(const char* _buf, int& _size, char* msg);

// ������
DWORD WINAPI SendThread(LPVOID);
DWORD WINAPI ReceiveThread(LPVOID);
// ���� ��� ������ �Լ�
DWORD WINAPI ClientMain(LPVOID arg);
// ��ȭ���� ���ν���
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// ���� ��Ʈ�� ��� �Լ�
void DisplayText(char* fmt, ...);

char buf[BUFSIZE + 1];				// ������ �ۼ��� ����
HANDLE hReadEvent, hWriteEvent;		// �̺�Ʈ
HWND hSendButton;					// ������ ��ư
HWND hEnterButton;					// ���� ��ư
HWND hExitButton;					// ������ ��ư
HWND hEdit1, hEdit2;				// ���� ��Ʈ��

HANDLE wait_chat_event = CreateEvent(nullptr, false, false, nullptr);;		// ä�� ������ �̺�Ʈ

// socket()
SOCKET TCPsock;				// tcp����� ���� ����

int listSelect = 0;			// �Է�â���� ���ڸ� �ޱ����� ���� ����
bool closeThread = false;	// �����带 �����ϱ� ���� bool��, ������ ������ true��

HWND hDlg2;				// �������� ���� ���� ���̾�α� �ڵ�

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// �̺�Ʈ ����
	hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (hReadEvent == NULL) return 1;
	hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (hWriteEvent == NULL) return 1;

	// ���� ��� ������ ����
	CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);

	// ��ȭ���� ����
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// �̺�Ʈ ����
	CloseHandle(hReadEvent);
	CloseHandle(hWriteEvent);

	// closesocket()
	closesocket(TCPsock);

	// ���� ����
	WSACleanup();
	return 0;
}


// ��ȭ���� ���ν���
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
		DisplayText("���� �̸� �Է�\r\n");
		SendMessage(hEdit1, EM_SETLIMITTEXT, BUFSIZE, 0);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			EnableWindow(hSendButton, FALSE);					// ������ ��ư ��Ȱ��ȭ
			WaitForSingleObject(hReadEvent, INFINITE);			// �б� �Ϸ� ��ٸ���
			GetDlgItemText(hDlg, IDC_EDIT1, buf, BUFSIZE + 1);

			SetEvent(hWriteEvent);								// ���� �Ϸ� �˸���
			SetFocus(hEdit1);
			SendMessage(hEdit1, EM_SETSEL, 0, -1);

			SetDlgItemText(hDlg, IDC_EDIT1, "");
			return TRUE;
		case IDENTER:
			WaitForSingleObject(hReadEvent, INFINITE);						// �б� �Ϸ� ��ٸ���
			listSelect = GetDlgItemInt(hDlg, IDC_EDIT1, FALSE, NULL);		// ���� �޾ƿ���

			SetEvent(hWriteEvent);		// ���� �̺�Ʈ �ѱ�
			SetFocus(hEdit1);
			SendMessage(hEdit1, EM_SETSEL, 0, -1);

			SetDlgItemText(hDlg, IDC_EDIT1, "");
			SetDlgItemText(hDlg, IDC_EDIT2, "");
			return TRUE;
		case IDEXIT:
			closeThread = true;
			EnableWindow(hExitButton, FALSE);		// ������ ��ư ��Ȱ��ȭ
			SetEvent(hWriteEvent);					// ���� �̺�Ʈ �ѱ�
			SetEvent(wait_chat_event);				// ������ �˸���

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

// TCP Ŭ���̾�Ʈ ���� �κ�
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	// ���� �ʱ�ȭ
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

	// ��Ƽĳ��Ʈ�� ���� ���� ����
	SockInfo* mrecvSock;		// ��Ƽĳ��Ʈ �ޱ�� ���� (bind)
	SockInfo* msendSock;		// ��Ƽĳ��Ʈ ������� ����

	// ������ ����
	HANDLE Send;
	HANDLE Receive;


	bool createOnce = false;

	while (1)
	{
		switch (protocol)
		{
		case CREATE_USER:
			WaitForSingleObject(hWriteEvent, INFINITE); // ���� �Ϸ� ��ٸ���

			// ���ڿ� ���̰� 0�̸� ������ ����
			if (strlen(buf) == 0) {
				EnableWindow(hSendButton, TRUE);	// ������ ��ư Ȱ��ȭ
				SetEvent(hReadEvent);				// �б� �Ϸ� �˸���
				continue;
			}

			strcpy(user.name, buf);		// ���ۿ� �ִ� �����̸� ����

			size = Pack_userInfo(buf, protocol, user);

			retval = send(TCPsock, buf, size, 0);			// �������� ������
			if (retval == SOCKET_ERROR)
			{
				err_display("userinfo send()");
				break;
			}

			if (!PacketRecv(TCPsock, buf))		// �����κ��� ����� �ް�(�������� ����)
			{
				break;
			}

			protocol = GetProtocol(buf);	// �������� ����

			SetEvent(hReadEvent); // �б� �Ϸ� �˸���

			SetDlgItemText(hDlg2, IDC_EDIT2, "");		// ���â �����

			break;
		case LOBBY:

			EnableWindow(hSendButton, FALSE);			// ������ ��ư ��Ȱ��ȭ
			EnableWindow(hEnterButton, TRUE);			// ���� ��ư Ȱ��ȭ

			if (!PacketRecv(TCPsock, buf))			// �� ����Ʈ �ޱ�
			{
				endflag = true;
				break;
			}

			protocol = GetProtocol(buf);				// �������� �и�

			UnPack_msg(buf, size, msg);					// �渮��Ʈ ����ŷ

			msg[size] = '\0';
			DisplayText("%s\n", msg);						// ���� �渮��Ʈ ���

			WaitForSingleObject(hWriteEvent, INFINITE); // ���� �Ϸ� ��ٸ���

			_itoa(listSelect, msg, 10);

			// ���ڿ� ���̰� 0�̸� ������ ����
			if (strlen(msg) == 0) {
				EnableWindow(hEnterButton, TRUE);		// ���� ��ư Ȱ��ȭ
				SetEvent(hReadEvent);					// �б� �Ϸ� �˸���
				continue;
			}

			size = Pack_msg(buf, protocol, msg);
			retval = send(TCPsock, buf, size, 0);		// ������ �� ������
			if (retval == SOCKET_ERROR)
			{
				err_display("userinfo send()");
				break;
			}

			// 1~3 �ܿ� ���ý� �ٽ� �����ϵ���
			if (listSelect < 1 || 3 < listSelect) {
				SetEvent(hReadEvent);				// �б� �Ϸ� �˸���
				continue;
			}

			if (!PacketRecv(TCPsock, buf))			// ��Ƽĳ��Ʈ ������ �ޱ�
			{
				break;
			}

			protocol = GetProtocol(buf);			// �������� �и�

			ZeroMemory(MULTICASTIP, sizeof(MULTICASTIP));	// �ʱ�ȭ
			UnPack_msg(buf, size, MULTICASTIP);			// ��Ƽĳ��Ʈ ������ ����ŷ

			MULTICASTIP[size] = '\0';

			DisplayText("<SYSTEM> %d�� �濡 �����Ͽ����ϴ� \r\n", listSelect);

			EnableWindow(hSendButton, TRUE);		// ������ ��ư Ȱ��ȭ
			SetEvent(hReadEvent);					// �б� �Ϸ� �˸���

			break;
		case CHAT:
			EnableWindow(hEnterButton, FALSE);		// ���� ��ư ��Ȱ��ȭ
			EnableWindow(hExitButton, TRUE);		// ������ ��ư Ȱ��ȭ

			if (!createOnce)		// �ѹ��� ���� �����ϵ���
			{
				createOnce = true;

				// ��Ƽĳ��Ʈ�� ���� ���� ����
				mrecvSock = CreateMRecvSocket(MULTICASTIP);		// ��Ƽĳ��Ʈ �ޱ�� ���� (bind)
				msendSock = CreateMSendSocket(MULTICASTIP);		// ��Ƽĳ��Ʈ ������� ����
			}
			else // �̹� ������ ��� ��Ƽĳ��Ʈ �׷츸 ���� ( SEND�� ������ �ʿ䰡 ����. �������ּҸ� ����)
			{
				mrecvSock->mreq = Add_Membership(mrecvSock->sock, MULTICASTIP);
				msendSock->sockaddr.sin_addr.s_addr = inet_addr(MULTICASTIP);
			}

			// ������ ����
			Receive = CreateThread(NULL, 0, ReceiveThread, mrecvSock, 0, NULL);
			Send = CreateThread(NULL, 0, SendThread, msendSock, 0, NULL);

			protocol = CHAT;

			size = Pack_protocol(buf, protocol);
			retval = send(TCPsock, buf, size, 0);			// �����ٴ� �������� ������
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
			retval = send(TCPsock, buf, size, 0);			// �����ٴ� �������� ������
			if (retval == SOCKET_ERROR)
			{
				err_display("exit room send()");
				break;
			}

			if (!PacketRecv(TCPsock, buf))		// ���ú� ������ ���Ḧ ���� ��Ŷ�ϳ� �ޱ�
			{
				break;
			}

			protocol = GetProtocol(buf);	// �������� ����

			// ��Ƽĳ��Ʈ �׷� Ż��
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

// �Է� ������
DWORD WINAPI SendThread(LPVOID _ptr)
{
	SockInfo* info = (SockInfo*)_ptr;

	int retval;
	char msg[BUFSIZE + 1];
	int len;

	while (1)
	{
		WaitForSingleObject(hWriteEvent, INFINITE); // ���� �Ϸ� ��ٸ���

		if (closeThread)
			break;

		// ���ڿ� ���̰� 0�̸� ������ ����
		if (strlen(buf) == 0) {
			EnableWindow(hSendButton, TRUE); // ������ ��ư Ȱ��ȭ
			SetEvent(hReadEvent); // �б� �Ϸ� �˸���
			continue;
		}

		// '\n' ���� ����
		len = strlen(buf);
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		if (strlen(buf) == 0)
			break;

		sprintf(msg, "[%s] %s", user.name, buf);

		// ������ ������
		retval = sendto(info->sock, msg, strlen(msg), 0,
			(SOCKADDR*)&info->sockaddr, sizeof(info->sockaddr));
		if (retval == SOCKET_ERROR) {
			err_display("sendto()");
			continue;
		}

		EnableWindow(hSendButton, TRUE); // ������ ��ư Ȱ��ȭ
		SetEvent(hReadEvent); // �б� �Ϸ� �˸���
	}

	return 0;
}

// �ޱ� ������
DWORD WINAPI ReceiveThread(LPVOID _ptr)
{
	SockInfo* info = (SockInfo*)_ptr;

	int retval;
	SOCKADDR_IN peeraddr;
	int addrlen;

	while (1)
	{
		// ������ �ޱ�
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


// ���� ��Ʈ�� ��� �Լ�
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

	// ���� �ּ� ����ü �ʱ�ȭ
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

	// SO_REUSEADDR �ɼ� ����
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

	// ��Ƽĳ��Ʈ �׷� ����
	struct ip_mreq mreq;
	mreq.imr_multiaddr.s_addr = inet_addr(MULTICASTIP);		// ���� �ּҷ� ����
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);			// �� �ּҰ� ����
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

//��ŷ �Լ� ����
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

//����ŷ �Լ� ����
void UnPack_msg(const char* _buf, int& _size, char* msg)
{
	const char* ptr = _buf + sizeof(PROTOCOL);

	memcpy(&_size, ptr, sizeof(_size));
	ptr = ptr + sizeof(_size);

	memcpy(msg, ptr, _size);
}
#include "Acer_Socket.h"

AcerSocket::AcerSocket(bool IsServer, const char *IPAddress, int port, int type, int protocol, int IPFormat)
{
	//Windows socket start
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	m_MainSocket = INVALID_SOCKET;
	m_ClientSocket = INVALID_SOCKET;

	memset(&m_sockAddr, 0, sizeof(m_sockAddr));  //每?字?都用0填充    
	m_sockAddr.sin_family = PF_INET;
	inet_pton(type, IPAddress, &(m_sockAddr.sin_addr.s_addr));
	m_sockAddr.sin_port = htons(port);  //端口

	m_type = type;
	m_protocol = protocol;
	m_IPFormat = IPFormat;
	m_IsServer = IsServer;
}

AcerSocket::~AcerSocket()
{
	socket_close();
}

bool AcerSocket::socket_client_Init()
{
	bool rv = 0;
	rv = socket_create();
	if (!rv)
	{
		//DriverLog("socket create failed");
		//delete this;
		return rv;
	}

	rv = socket_connect();
	if (!rv)
	{
		//DriverLog("socket create failed");
		//delete this;
		return rv;
	}

	return true;
}

bool AcerSocket::socket_server_Init()
{
	bool rv = 0;
	rv = socket_create();
	if (!rv)
	{
		//DriverLog("socket create failed");
		//delete this;
		return rv;
	}

	rv = socket_set_socket_option();
	if (!rv)
	{
		//DriverLog("set socket option failed");
		return rv;
	}

	rv = socket_bind();
	if (!rv)
	{
		//DriverLog("socket bind %s.%d failed", m_sockAddr.sin_addr.s_addr, m_sockAddr.sin_port);
		return rv;
	}

	rv = socket_listen();
	if (!rv)
	{
		//DriverLog("socket listen failed");
		return rv;
	}

	return true;
}


bool AcerSocket::socket_create()
{
	m_MainSocket = socket(m_type, m_protocol, m_IPFormat);

	if (m_MainSocket != INVALID_SOCKET)
	{
		return true;
	}
	else
	{
		//TODO return socket error code
		return false;
	}
}

void AcerSocket::socket_close()
{
  if (m_MainSocket)
  {
	if (m_MainSocket != INVALID_SOCKET) {
	  closesocket(m_MainSocket);
	}
  }	
}

bool AcerSocket::socket_set_socket_option()
{
	//TODO : need to modify
	int opt = 1;
	if (setsockopt(m_MainSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(int)) < 0)
		return false;
	unsigned long mode = 1;
	if (ioctlsocket(m_MainSocket, FIONBIO, &mode) != 0)
		return false;

	return true;
}

bool AcerSocket::socket_bind()
{
	int iResult = bind(m_MainSocket, (SOCKADDR*)&m_sockAddr, sizeof(SOCKADDR));

	if (iResult == SOCKET_ERROR)
	{
		return false;
	}
	else
	{
		return true;
	}
}

bool AcerSocket::socket_listen()
{
	int iResult = listen(m_MainSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		return false;
	}
	else
	{
		return true;
	}
}

void AcerSocket::socket_accept()
{
	m_ClientSocket = accept(m_MainSocket, NULL, NULL);
}

bool AcerSocket::socket_send(const void *data, size_t size)
{
	int rv = 0;
	if (m_IsServer)
	{
		rv = send(m_ClientSocket, (const char*)data, size, NULL);
	}
	else
	{
		rv = send(m_MainSocket, (const char*)data, size, NULL);
	}

	if (rv)
	{
		return true;
	}
	else
	{
		return false;
	}
}

int AcerSocket::socket_receive(void *rcvbuf, int size)
{
	int bytes = 0;
	if (m_IsServer)
	{
		bytes = recv(m_ClientSocket, (char*)rcvbuf, size, 0);
	}
	else
	{
		bytes = recv(m_MainSocket, (char*)rcvbuf, size, 0);
	}
	return bytes;
}

bool AcerSocket::socket_connect()
{
	if (connect(m_MainSocket, (struct sockaddr *)&m_sockAddr, sizeof(m_sockAddr)) < 0)
	{
		return false;
	}
	return true;
}

SOCKET AcerSocket::get_main_socket()
{
	return m_MainSocket;
}

SOCKET AcerSocket::get_client_socket()
{
	return m_ClientSocket;
}

void AcerSocket::close_client_socket()
{
  if (m_ClientSocket)
	{
		if (m_ClientSocket != INVALID_SOCKET) {
		  closesocket(m_ClientSocket);
		}
		m_ClientSocket = INVALID_SOCKET;
	}	
}

void AcerSocket::SetKeepAlive(bool Alive)
{
	m_keepAlive = Alive;
}

bool AcerSocket::GetKeepAlive()
{
	return m_keepAlive;
}

void AcerSocket::GetErrorMessage()
{
	wchar_t *s = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&s, 0, NULL);
	fprintf(stderr, "%S\n", s);
	LocalFree(s);
}



static DWORD WINAPI TimeOut(void* Param)
{
	AcerSocket *Socket = (AcerSocket *)Param;
	int count = 0;
	// char recvBuf[128];

	while (1)
	{
		//check client alive at 2 sec
		if (Socket->GetKeepAlive())
		{
			count = 0;
			Socket->SetKeepAlive(false);
		}
		else
		{
			count++;
		}

		if (count == CHECKKEEPALIVETIMES)
		{
			Socket->close_client_socket();
			break;
		}
		Sleep(TIMEOUTSLEEP);
	}
	return true;
}

void AcerSocket::GreateTimeOut()
{
	DWORD ThreadID;
	CreateThread(NULL, 0, TimeOut, (void*)this, 0, &ThreadID);
}


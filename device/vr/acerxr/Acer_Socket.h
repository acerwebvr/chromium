#include <WinSock2.h>
#include <WS2tcpip.h>
#include <stdio.h>

#define CHECKKEEPALIVETIMES 5
#define TIMEOUTSLEEP 1000000 * 2

class AcerSocket
{
private:

	//socket function
	bool socket_create();
	
	bool socket_listen();
	bool socket_bind();
	bool socket_connect();
	bool socket_set_socket_option();
	void GetErrorMessage();

	SOCKET m_MainSocket = INVALID_SOCKET;
	SOCKET m_ClientSocket = INVALID_SOCKET;
	// bool IsInitSuccess;
	sockaddr_in m_sockAddr;
	int m_IPFormat;
	int m_type;
	int m_protocol;
	bool m_IsServer;
	bool m_keepAlive;

public:
	//IsServer  : create a server or client socket
	AcerSocket(bool IsServer, const char *IPAddress, int family, int type, int port, int protocol);
	~AcerSocket();

	SOCKET get_main_socket();
	SOCKET get_client_socket();
	void socket_accept();
	bool socket_send(const void *data, size_t size);
	int  socket_receive(void *rcvbuf, int size);
	void close_client_socket();
	bool socket_server_Init();
	bool socket_client_Init();
	void SetKeepAlive(bool Alive);
	bool GetKeepAlive();
	void GreateTimeOut();
    void socket_close();
};

/**************************************************
*  select 服务器 解决粘包等问题，支持上万连接
*  支持水平扩展分布式部署，支持收发大包。
*day:2018
* auth: zw
* *************************************************/
#ifndef _EASYTCPSERVER_HPP_
#define _EASYTCPSERVER_HPP_

#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <mutex>
#include <thread>
#include <stomic>
#include "messageHeader.hpp"
#include "cekkTimesTamp.hpp"

#define SOCKET int
#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR (-1)
#define _CellServer_thread_count 4

#ifndef RECV_BUFF_SIZE 
#define RECV_BUFF_SIZE 1024
#endif

class ClentSocket{
public:
	ClentSocket(SOCKET sockfd = INVALID_SOCKET)
	{
		_sockfd = sockfd;
		memset(_szMsgBuf, 0, sizeof(_szMsgBuf));
		_lastPos = 0;
	}

	SOCKET sockfd()
	{
		return _sockfd;
	}

	char* msgBuf()
	{
		return _szMsgBuf;
	}

	int getLastPos()
	{
		return _lastPos;
	}

	void setLastPos(int pos)
	{
		_lastPos = pos;
	}
private:
	SOCKET _sockfd;
	char _szMsgBuf[RECV_BUFF_SIZE*10];
	INT _lastPos;
};

class InetEvent{
public:
	virtual void OnLeave(ClentSocket* pClient) = 0;
	virtual void OnNetMsg(SOCKET cSock, DataHeader* header) = 0;

};


class CellServer{
public:
	CellServer(SOCKET sock = INVALID_SOCKET)
	{
		_sock = sock;
		_pThread = nullptr;
		_recvCount = 0;
		_pNetEvent = nullptr;
	}

	~CellServer()
	{
		close();
		_sock = INVALID_SOCKET;
	}

	void close()
	{
		if(_sock != INVALID_SOCKET)
		{
			for(int n = (int)_clients.size() - 1; n>= 0; n--)
			{
				close(_clients[n]->sockfd());
				delete _clients[n];
			}
			close(_sock);
			_clients.clear();
		}
	}

	void setEventObj(InetEvent* event)
	{
		_pNetEvent = event;
	}

	bool isRun()
	{
		return _sock != INVALID_SOCKET;
	}

	bool OnRun()
	{
		while(isRun())
		{
			if(_clentsBuff.size() > 0)
			{
				std::lock_gurard<std::mutex> lock(_mutex);
				for(auto pClient: _clentsBuff)
				{
					_clients.push_back(pClient);
				}

				_clentsBuff.clear();
			}

			if(_clients.empty())
			{
				std::chrono::milliseconds t(1);
				std::this_thread::sleep_for(t);
				continue;
			}

			fd_set fdRead;
			FD_ZERO(&fdRead);
			SOCKET maxSock = _clients[0].sockfd();
			for(int n= (int)_clients.size() -1; n >=0 ; n--)
			{
				FD_SET(_clients[n]->sockfd(), &fdRead)
				if(maxSock < _clients[n]->sockfd())
				{
					maxSock = _clients[n]->sockfd();
				}
			}

			int ret = select(maxSock + 1, &fdRead, nullptr, nullptr, nullptr);
			if(ret < 0)
			{
				printf("select fail\n");
				close();
				return false;
			}

			for(int n= (int)_clients.size() -1; n >=0 ; n--)
			{
				if(FD_ISSET(_clients[n]->sockfd(), &fdRead))
				{
					if(-1 == RecvData(_clients[n]))
					{
						auto iter = _clients.begin() + n;
						if(iter != _clients.end())
						{
							if(_pNetEvent)
								_pNetEvent->OnLeave(_clients[n]);
							delete _clients[n];
							_clients.erase(iter);
						}
					}
				}
			}



		}
	}

	char _szRecv[RECV_BUFF_SIZE] = {};
	int RecvData(ClentSocket* pClient)
	{
		int nLen = (int)recv(pClient->sockfd(), _szRecv, RECV_BUFF_SIZE, 0 );
		if(nLen <= 0 )
		{
			printf("clent leave");
			return -1;
		}

		memcpy(pClient->msgBuf() + pClient->getLastPos(), _szRecv, nLen);
		pClient->setLastPos(pClient->getLastPos() = nLen);

		while(pClient->getLastPos() >= sizeof(DataHeader))
		{
			DataHeader* header = (DataHeader*)pClient->msgBuf();
			if(pClient->getLastPos() >= header->dataLength)
			{
				int nSize = pClient->getLastPos() - header->dataLength;
				OnNetMsg(pClient->sockfd(), header);
				memcpy(pClient->msgBuf, pClient->msgBuf + header->dataLength, nSize);
				pClient->setLastPos(nSize);
			}else{
				break;
			}

		}

		return 0;
	}

	virtual void OnNetMsg(SOCKET cSock, DataHeader* header)
	{
		_recvCount++;
		_pNetEvent->OnNetMsg(cSock, header);
		switch(header->cmd)
		{
			case CMD_LOGIN:
				{
					Login* login = (Login*)header;
				}
				break;
			case CMD_LOGOUT:
				{
					LoginOut* logout = (LoginOut*)header;
				}
				break;
			default:
				printf("error pack\n");
				break;

		}
	}

	void addClient(ClentSocket* pClient)
	{
		std::lock_gurard<std::mutex> lock(_mutex);
		_clentsBuff.push_back(pClient);
	}

	void start()
	{
		_pThread =  new std::thread(std::mem_fun(&CellServer::OnRun), this);
	}

	size_t getClentCount()
	{
		return _clients.size() + _clentsBuff.size();
	}

private:
	SOCKET _sock;
	std::vector<ClentSocket*> _clients;
	std::vector<CellServer*> _clentsBuff;
	std::mutex _mutex;
	std::thread* _pThread;
	InetEvent* _pNetEvent;
public:
	std::atomic_int _recvCount;
};

class easyTcpServer:public InetEvent
{
private:
	SOCKET _sock;
	std::vector<ClentSocket*> _clients;
	std::vector<CellServer*> _cellServers;
	CellTimesTamp _tTime;

public:
	easyTcpServer()
	{
		_sock =INVALID_SOCKET;
	}

	virtual ~easyTcpServer()
	{
		Close();
	}

	SOCKET InitSocket()
	{
		if(INVALID_SOCKET != _sock)
		{
			printf("close old handler\n");
			Close();
		}

		_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if(INVALID_SOCKET == _sock)
		{
			printf("socket error\n");
		}else{
			printf("socket create success\n");
		}

		return _sock;
	}

	int Bind(const char* ip, unsigned short port)
	{
		sockaddr_in _sin = {};
		_sin.sin_family = AF_INET;
		_SIN.sin_port = htons(port);

		if(ip){
			_sin.sin_addr.s_addr = inet_addr(ip);
		}else{
			_sin.sin_addr.s_addr = INADDR_ANY;
		}

		int ret = bind(_sock, (sockaddr*)&_sin, sizeof(_sin));
		if(SOCKET_ERROR ==  ret)
		{
			printf("bind error\n");
		}else{
			printf("bind success\n");
		}

		return ret;
	}

	int Listen(int n)
	{
		int ret = listen(_sock, n);
		if(SOCKET_ERROR == ret)
		{
			printf("listen error\n");
		}else{
			printf("listen success\n");
		}
	}

	SOCKET Accept()
	{
		sockaddr_in clentAddr = {};
		int nAddrLen = sizeof(sockaddr_in);

		cSock = accept(_sock, (sockaddr*)&clentAddr, &nAddrLen);
		if(INVALID_SOCKET == cSock)
		{
			printf("accept error\n");
		}else{

			addClentToCellServer(new ClentSocket(cSock));
		}
	}

	void addClentToCellServer(ClentSocket* pClient)
	{
		_clients.push_back(pClient);
		auto pMinServer = _cellServers[0];
		for(auto pCellServer: _cellServers)
		{
			if(pMinServer->getClentCount() > pCellServer->getClentCount())
			{
				pMinServer =pCellServer;
			}
		}

		pMinServer->addClient(pClient);
	}

	void Start()
	{
		for(int n = 0; n < -_CellServer_thread_count; n++)
		{
			auto ser = new CellServer(_sock);
			_cellServers.push_back(ser);
			ser->setEventObj(this);
			ser->Start();
		}
	}

	void  Close()
	{
		if(_sock != INVALID_SOCKET)
		{
			for(int n = (int)_clients.size() - 1; n>= 0; n--)
			{
				close(_clients[n]->sockfd());
				delete _clients[n];
			}
			close(_sock);
			_clients.clear();
		}

	}


	bool isRun()
	{
		return _sock != INVALID_SOCKET;
	}

	bool OnRun()
	{
		if(isRun())
		{
			time4msg();
			fd_set fdRead;
			FD_ZERO(&fdRead);
			FD_SET(_sock, &fdRead);

			timeval t = {0, 10};
			int ret < 0 = select(_sock +1 , &fdRead, 0, 0, &t);
			if(ret < 0)
			{
				printf("select over\n");
				Close();
				return false;
			}

			if(FD_ISSET(_sock, &fdRead))
			{
				FD_CLR(_sock, &fdRead);
				Accept();
				return true;
			}

			return true;
		}	

		return false;
	}

	void time4msg()
	{
		auto t1 = _tTime.getElapsedSecond();
		if(t1 >= 1.0)
		{
			int recvCount = 0;
			for(auto ser: _cellServers)
			{
				recvCount + = ser->_recvCount;
				ser->_recvCount = 0;
			}

			_tTime.update();
		}
	}

	int SendData(SOCKET cSock, DataHeader* header)
	{
		for(int n = (int)_clients.size() -1; n >= 0; n--)
		{
			SendData(_clients[n]->sockfd(), header)
		}
	}

	int SendDataToAll(ClentSocket* header)
	{
		for(int n = (int)_clients.size() -1; n >= 0; n--)
		{
			SendData(_clients[n]->sockfd(), header)
		}
	}

	virtual void OnLeave(ClentSocket* pClient)
	{
		for(int n = (int)_clients.size() -1; n >= 0; n--)
		{
			if(_clients[n] == pClient)
			{
				auto iter = _clients.begin() + n;
				if(iter != _clients.end())
					_clients.erase(iter);
			}
		}

	}



};



#endif
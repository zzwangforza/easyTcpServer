/**************************************************
*  select 服务器 解决粘包等问题，支持上万连接
*  支持水平扩展分布式部署，支持收发大包。
*day:2018
* auth: zw
* *************************************************/
#include <thread>
#include <iostream>
#include "easytcpserver.hpp"


using namespace std;

bool g_bRun = true;
void cmdThread()
{
	while(true)
	{
		char cmdBuf[128] = {};
		scanf("%s",cmdBuf);
		if(0==strcmp(cmdBuf, "exit"))
		{
			g_bRun = false;
			printf("exit cmdThread \n");
			break;
		}else{
			printf("unsupported command\n");
		}
	}
}

int main(int argc, char const *argv[])
{
	easytcpserver server;
	server.InitSocket();
	server.Bind("0.0.0.0", 8888);
	server.Listen(1024);
	server.Start();

	std::thread t1(cmdThread);
	t1.detach();

	while(g_bRun)
	{
		server.OnRun();
	}	

	server.Close();
	printf("exit server\n");
	getchar();
	return 0;
}

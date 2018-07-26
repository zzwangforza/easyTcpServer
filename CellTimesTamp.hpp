/**************************************************
*  select 服务器 解决粘包等问题，支持上万连接
*  支持水平扩展分布式部署，支持收发大包。
*day:2018
* auth: zw
* *************************************************/
#ifndef _CellTimesTamp_hpp
#define _CellTimesTamp_hpp

#include <chrono>
using namespace std::chrono;

class CellTimesTamp
{
public:
	CellTimesTamp()
	{
		update();
	}

	void update()
	{
		_begin = high_resolution_clock::now();
	}

	double getElapsedSecond()
	{
		return getElapsedTimeInMicroSec()*0.000001;
	}

	double getElapsedTimeInMilliSec()
	{
		return this->getElapsedTimeInMicroSec() * 0.001;
	}

	long long getElapsedTimeInMicroSec()
	{
		return duration_cast<microseconds>(high_resolution_clock::now() - _begin).count();
	}
protected:
	time_point<high_resolution_clock> _begin;

};



#endif
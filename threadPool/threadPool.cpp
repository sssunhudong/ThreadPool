#include "threadPool.h"
#include <process.h>

ThreadPool::ThreadPool(size_t minNumOfThread, size_t maxNumOfThread)
{
	if (minNumOfThread < 2)
		this->minNumOfThread = 2;
	else
		this->minNumOfThread = minNumOfThread;

	if (maxNumOfThread < this->minNumOfThread * 2)
		this->maxNumOfThread = this->minNumOfThread * 2;
	else
		this->maxNumOfThread = maxNumOfThread;

	stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);

	//清空空闲线程列表
	idleThreadList.clear();
	//创建空闲线程
	CreateIdleThread(this->minNumOfThread);

	busyThreadList.clear();

	dispatchThrad = (HANDLE)_beginthreadex(0, 0, GetTaskThreadProc, this, 0, 0);

	numOfLongFun = 0;
}
ThreadPool::~ThreadPool()
{
	SetEvent(stopEvent);
	PostQueuedCompletionStatus(completionPort, 0, (DWORD)EXIT, NULL);
	CloseHandle(stopEvent);
}
BOOL ThreadPool::QueueTaskItem(TaskFun task, PVOID param, TaskCallbackFun taskCb, BOOL longFun)
{
	waitTaskLock.Lock();
	WaitTask *waitTask = new WaitTask(task, param, taskCb, longFun);
	waitTaskList.push_back(waitTask);
	PostQueuedCompletionStatus(completionPort, 0, (DWORD)GET_TASK, NULL);
	waitTaskLock.UnLock();
	return TRUE;
}
void ThreadPool::CreateIdleThread(size_t size)
{
	idleThreadLock.Lock();
	for (size_t i = 0; i < size; ++i)
	{
		idleThreadList.push_back(new Thread(this));
	}
	idleThreadLock.UnLock();
}
void ThreadPool::DeleteIdleThread(size_t size)
{
	idleThreadLock.Lock();
	size_t t = idleThreadList.size();
	//如果想要删除的线程数小于总的线程数量，按传入的数量删，如果比总的线程数量大则将列表全部删除
	//删除从尾部开始删除
	if (t >= size)
	{
		for (size_t i = 0; i < size; ++i)
		{
			auto thread = idleThreadList.back();
			delete thread;
			idleThreadList.pop_back();
		}
	}
	else
	{
		for (size_t i = 0; i < t; ++i)
		{
			auto thread = idleThreadList.back();
			delete thread;
			idleThreadList.pop_back();
		}
	}
	idleThreadLock.UnLock();
}
ThreadPool::Thread *ThreadPool::GetIdleThread()
{
	Thread *thread = NULL;
	idleThreadLock.Lock();
	//获取从头部开始获取
	if (idleThreadList.size() > 0)
	{
		thread = idleThreadList.front();
		idleThreadList.pop_front();
	}
	idleThreadLock.UnLock();

	if (thread == NULL && getCurNumOfThread() < maxNumOfThread)
	{
		thread = new Thread(this);
	}

	if (thread == NULL && waitTaskList.size() > THRESHOLE_OF_WAIT_TASK)
	{
		thread = new Thread(this);
		InterlockedIncrement(&maxNumOfThread);
	}
	return thread;
}
void ThreadPool::MoveBusyThreadToIdleList(Thread * busyThread)
{
	//将线程加入空闲列表
	idleThreadLock.Lock();
	idleThreadList.push_back(busyThread);
	idleThreadLock.UnLock();
	//删除忙碌线程列表中的该线程
	busyThreadLock.Lock();
	for (auto it = busyThreadList.begin(); it != busyThreadList.end(); ++it)
	{
		if (*it = busyThread)
		{
			busyThreadList.erase(it);
			break;
		}
	}
	busyThreadLock.UnLock();
	//如果线程池内线程数不为0 && 空闲线程列表中的数量 > 线程池内线程数的8成，则将空闲线程列表中的一半删除
	if (maxNumOfThread != 0 && idleThreadList.size() > maxNumOfThread * 0.8)
	{
		DeleteIdleThread(idleThreadList.size() / 2);
	}

	PostQueuedCompletionStatus(completionPort, 0, (DWORD)GET_TASK, NULL);
}
void ThreadPool::MoveThreadToBusyList(Thread * thread)
{
	busyThreadLock.Lock();
	busyThreadList.push_back(thread);
	busyThreadLock.UnLock();
}
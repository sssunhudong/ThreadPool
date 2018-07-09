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

	//��տ����߳��б�
	idleThreadList.clear();
	//���������߳�
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
	//�����Ҫɾ�����߳���С���ܵ��߳������������������ɾ��������ܵ��߳����������б�ȫ��ɾ��
	//ɾ����β����ʼɾ��
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
	//��ȡ��ͷ����ʼ��ȡ
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
	//���̼߳�������б�
	idleThreadLock.Lock();
	idleThreadList.push_back(busyThread);
	idleThreadLock.UnLock();
	//ɾ��æµ�߳��б��еĸ��߳�
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
	//����̳߳����߳�����Ϊ0 && �����߳��б��е����� > �̳߳����߳�����8�ɣ��򽫿����߳��б��е�һ��ɾ��
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
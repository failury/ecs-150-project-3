//
// Created by failury on 4/24/20.
//
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "VirtualMachine.h"
#include "Machine.h"
#include "VirtualMachine.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <bits/stdc++.h>

extern "C" {
#define PL() std::cout << "@ line" <<__LINE__
typedef void (*TVMMainEntry)(int, char *[]);
#define VMPrint(format, ...) VMFilePrint ( 1, format, ##__VA_ARGS__)
#define VMPrintError(format, ...) VMFilePrint ( 2, format, ##__VA_ARGS__)
TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);
#define VM_TIMEOUT_INFINITE ((TVMTick)0)
#define VM_TIMEOUT_IMMEDIATE ((TVMTick)-1)
void Scheduler(TVMThreadState NextState);
volatile int Tickms = 0;
volatile int tickCount = 0;
volatile TVMThreadID RunningThreadID;
struct TCB {
    //define tcb model
    TVMThreadID ID;
    TVMThreadState State;
    TVMThreadPriority Priority;
    TVMThreadEntry Entry;
    SMachineContext Context;
    volatile int SleepTime;
    TVMMemorySize MemorySize;
    void *Param;
    void *StackAddress;
    int FileData;
    bool HaveMutex;
};
std::vector<TCB> ThreadList;
std::vector<TCB> Ready;
std::vector<TCB> Sleep;
struct Mutex {
    TVMMutexID ID;
    TVMThreadID TID;
    bool Locked;
    std::vector<TCB> WaitingList;
};
std::vector<Mutex> MutexList;
void debug() {
    write(STDOUT_FILENO, "Thread List ", 12);
    for (int i = 0; i < (int) ThreadList.size(); i++) {
//        char tmp[3]={0x0};
//        sprintf(tmp,"%d: ", int(ThreadList[i].ID));
//        write(STDOUT_FILENO,tmp,sizeof(tmp));
        char amp[10] = {0x0};
        sprintf(amp, "State: %d ", int(ThreadList[i].State));
        write(STDOUT_FILENO, amp, sizeof(amp));
    }
    write(STDOUT_FILENO, "||", 2);
    write(STDOUT_FILENO, "Ready List ", 11);
    for (int i = 0; i < (int) Ready.size(); i++) {
        char tmp[4] = {0x0};
        sprintf(tmp, "%d: ", int(Ready[i].ID));
        write(STDOUT_FILENO, tmp, sizeof(tmp));
        char amp[10] = {0x0};
        sprintf(amp, "State: %d ", int(Ready[i].State));
        write(STDOUT_FILENO, amp, sizeof(amp));
    }
    write(STDOUT_FILENO, "||", 2);
    write(STDOUT_FILENO, "Sleep List ", 11);
    for (int i = 0; i < (int) Sleep.size(); i++) {
        char tmp[4] = {0x0};
        sprintf(tmp, "%d: ", int(Sleep[i].ID));
        write(STDOUT_FILENO, tmp, sizeof(tmp));
        char amp[10] = {0x0};
        sprintf(amp, "State: %d ", int(Sleep[i].State));
        write(STDOUT_FILENO, amp, sizeof(amp));
    }
    write(STDOUT_FILENO, "\n", 1);
}
void SORT(std::vector<TCB> &Input) {//Bubble sort algorithm https://www.geeksforgeeks.org/bubble-sort/
    int i, j;
    int n = Input.size();
    for (i = 0; i < n - 1; i++)
        for (j = 0; j < n - i - 1; j++)
            if (Input[j].Priority < Input[j + 1].Priority)
                std::swap(Input[j], Input[j + 1]);
}
void Callback(void *CallData) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    tickCount++;
    for (int i = 0; i < (int) Sleep.size(); i++) {
        if (Sleep[i].SleepTime != -1000000) {
            Sleep[i].SleepTime--;
            if (Sleep[i].SleepTime < 0 && ThreadList[Sleep[i].ID].State != VM_THREAD_STATE_DEAD) {
                TCB AwakeThread = Sleep[i];//push to ready list if the thread is done with sleeping
                Sleep.erase(Sleep.begin() + i);
                AwakeThread.State = VM_THREAD_STATE_READY;
                ThreadList[AwakeThread.ID].State = VM_THREAD_STATE_READY;
                Ready.push_back(AwakeThread);

            }
        }
    }
    Scheduler(VM_THREAD_STATE_READY);
//    if (!Ready.empty() && ThreadList[RunningThreadID].Priority == ThreadList[Ready.front().ID].Priority){
//        Scheduler(VM_THREAD_STATE_READY);
//    }
    MachineResumeSignals(&signal);
}
void Scheduler(TVMThreadState NextState) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    TCB NextThread;
    int TempID = RunningThreadID;
    NextThread = ThreadList[1];
    if (!Ready.empty()) {
        SORT(Ready);//sort the ready list by priority
        NextThread = Ready.front();
        Ready.erase(Ready.begin());
        if (ThreadList[NextThread.ID].State == VM_THREAD_STATE_READY) {
            NextThread = ThreadList[NextThread.ID];
        }
    }
    if (NextState == VM_THREAD_STATE_READY) {
        ThreadList[RunningThreadID].State = VM_THREAD_STATE_READY;
        if (RunningThreadID != 1) {
            Ready.push_back(ThreadList[RunningThreadID]);
        }
    } else if (NextState == VM_THREAD_STATE_WAITING) {
        ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
        Sleep.push_back(ThreadList[RunningThreadID]);
    } else if (NextState == VM_THREAD_STATE_DEAD) {
        ThreadList[RunningThreadID].State = VM_THREAD_STATE_DEAD;
    }
    if (NextThread.ID == RunningThreadID) {
        //no neeed to switch
        MachineResumeSignals(&signal);
        return;
    }
    ThreadList[NextThread.ID].State = VM_THREAD_STATE_RUNNING;
    RunningThreadID = NextThread.ID;
//    char tmp[12]={0x0};
//    sprintf(tmp,"%11d", int(TempID));
//    write(STDOUT_FILENO,tmp,sizeof(tmp));
//    write(STDOUT_FILENO,"Switchto",8);
//    sprintf(tmp,"%11d", int(NextThread.ID));
//    write(STDOUT_FILENO,tmp,sizeof(tmp));
//    write(STDOUT_FILENO,"\n",1);
    // debug();
    MachineContextSwitch(&ThreadList[TempID].Context, &ThreadList[NextThread.ID].Context);
    MachineResumeSignals(&signal);
}
void IdleThread(void *param) {
    MachineEnableSignals();
    while (1) {

    }
}
void skeleton(void *param) {
    int ID = *((int *) param);
    MachineEnableSignals();
    ThreadList[ID].Entry(ThreadList[ID].Param);
    VMThreadTerminate(ThreadList[ID].ID);
}
TVMStatus VMStart(int tickms, TVMMemorySize sharedsize, int argc, char *argv[]) {
//    void * share_memory =
    MachineInitialize(sharedsize);
    Tickms = tickms;
    MachineRequestAlarm(tickms * 1000, Callback, NULL);
    TVMMainEntry entry = VMLoadModule(argv[0]);
    if (entry != NULL) {
        TVMThreadID id;
        MachineEnableSignals();
        VMThreadCreate(skeleton, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, &id);//creating main thread
        VMThreadCreate(IdleThread, NULL, 0x100000, 0, &id);// creating idel thread
        VMThreadActivate(id);
        ThreadList[0].State = VM_THREAD_STATE_RUNNING;
    } else {
        MachineTerminate();
        return VM_STATUS_FAILURE;
    }
    entry(argc, argv);
    VMUnloadModule();
    MachineTerminate();
    return VM_STATUS_SUCCESS;
}
TVMStatus VMTickMS(int *tickmsref) {
    if (tickmsref != NULL) {
        TMachineSignalState signal;
        MachineSuspendSignals(&signal);
        *tickmsref = Tickms;
        MachineResumeSignals(&signal);
        return VM_STATUS_SUCCESS;
    } else {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
}
TVMStatus VMTickCount(TVMTickRef tickref) {
    if (tickref != NULL) {
        TMachineSignalState signal;
        MachineSuspendSignals(&signal);
        *tickref = tickCount;
        MachineResumeSignals(&signal);
        return VM_STATUS_SUCCESS;
    } else {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
}
TVMStatus
VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid) {
    if (entry == NULL || tid == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    } else {
        TMachineSignalState signal;
        MachineSuspendSignals(&signal);
        //initialize TCB
        TCB thread{};
        thread.ID = ThreadList.size();
        *tid = thread.ID;
        thread.Priority = prio;
        thread.Entry = entry;
        thread.Param = param;
        thread.MemorySize = memsize;
        thread.SleepTime = 0;
        thread.State = VM_THREAD_STATE_DEAD;
        thread.StackAddress = new uint8_t[memsize];
        thread.HaveMutex = false;
        thread.Context = SMachineContext();
        ThreadList.push_back(thread);
        MachineResumeSignals(&signal);
    }

    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadDelete(TVMThreadID thread) {
    if (ThreadList[thread].State == VM_THREAD_STATE_DEAD) {
        TMachineSignalState signal;
        MachineSuspendSignals(&signal);
        ThreadList.erase(ThreadList.begin() + thread);
        MachineResumeSignals(&signal);
        return VM_STATUS_SUCCESS;
    } else if (thread >= ThreadList.size() || thread < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    }
    return VM_STATUS_ERROR_INVALID_STATE;
}
TVMStatus VMThreadActivate(TVMThreadID thread) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (thread > ThreadList.size() || thread < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (ThreadList[thread].State != VM_THREAD_STATE_DEAD) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    MachineContextCreate(&ThreadList[thread].Context, skeleton, (void *) (&(ThreadList[thread].ID)),
                         ThreadList[thread].StackAddress,
                         ThreadList[thread].MemorySize);
    ThreadList[thread].State = VM_THREAD_STATE_READY;
    if (ThreadList[thread].Priority > 0) {
        Ready.push_back(ThreadList[thread]);
    }
    if (ThreadList[thread].Priority > ThreadList[RunningThreadID].Priority) {
        ThreadList[RunningThreadID].State = VM_THREAD_STATE_READY;
        Ready.push_back(ThreadList[RunningThreadID]);
        Scheduler(VM_THREAD_STATE_READY);
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadTerminate(TVMThreadID thread) {
    if (thread > ThreadList.size() || thread < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (ThreadList[thread].State == VM_THREAD_STATE_DEAD) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    for (int i = 0; i < MutexList.size(); i++) {
        if (MutexList[i].Locked == true && MutexList[i].TID == thread) {
            MutexList[i].Locked = false;
            TCB ReadyThread;
            if (!MutexList[i].WaitingList.empty()) {
                SORT(MutexList[i].WaitingList);
                ReadyThread = MutexList[i].WaitingList.front();
                MutexList[i].WaitingList.erase(MutexList[i].WaitingList.begin());
                if (ThreadList[i].HaveMutex) {
                    MutexList[i].TID = ReadyThread.ID;
                    MutexList[i].Locked = true;
                    if (ThreadList[ReadyThread.ID].Priority > ThreadList[RunningThreadID].Priority) {
                        ThreadList[RunningThreadID].State = VM_THREAD_STATE_READY;
                        Ready.push_back(ThreadList[RunningThreadID]);
                        Scheduler(VM_THREAD_STATE_READY);
                    } else {
                        ThreadList[ReadyThread.ID].State = VM_THREAD_STATE_READY;
                        Ready.push_back(ThreadList[ReadyThread.ID]);
                    }

                }

            }
        }
    }
    for (auto &i : ThreadList) {
        if (i.ID == thread) {
            i.State = VM_THREAD_STATE_DEAD;
            Scheduler(VM_THREAD_STATE_DEAD);
        }
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadID(TVMThreadIDRef threadref) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (threadref == NULL) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    *threadref = RunningThreadID;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef state) {
    if (thread > ThreadList.size() || thread < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (state == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    } else {
        *state = ThreadList[thread].State;
        return VM_STATUS_SUCCESS;
    }
}
TVMStatus VMThreadSleep(TVMTick tick) {
    if (tick == VM_TIMEOUT_INFINITE) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    TCB &CurrentThread = ThreadList[RunningThreadID];
    CurrentThread.SleepTime = tick;
    Scheduler(VM_THREAD_STATE_WAITING);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
void FileCallback(void *calldata, int result) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    TCB *thread = (TCB *) calldata;
    thread->FileData = result;
    thread->State = VM_THREAD_STATE_READY;
    Scheduler(VM_THREAD_STATE_READY);
    MachineResumeSignals(&signal);
}
TVMStatus VMFileOpen(const char *filename, int flags, int mode,
                     int *filedescriptor) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (filename == NULL || filedescriptor == NULL) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    MachineFileOpen(filename, flags, mode, FileCallback, &ThreadList[RunningThreadID]);
    Scheduler(VM_THREAD_STATE_WAITING);
    *filedescriptor = ThreadList[RunningThreadID].FileData;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileClose(int filedescriptor) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MachineFileClose(filedescriptor, FileCallback, &ThreadList[RunningThreadID]);
    Scheduler(VM_THREAD_STATE_WAITING);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileRead(int filedescriptor, void *data, int *length) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (length == NULL || data == NULL) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    MachineFileRead(filedescriptor, data, *length, FileCallback, &ThreadList[RunningThreadID]);
    Scheduler(VM_THREAD_STATE_WAITING);
    *length = ThreadList[RunningThreadID].FileData;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileWrite(int filedescriptor, void *data, int *length) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (length == NULL || data == NULL) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    MachineFileWrite(filedescriptor, data, *length, FileCallback, &ThreadList[RunningThreadID]);
    Scheduler(VM_THREAD_STATE_WAITING);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (newoffset == NULL) {
        MachineResumeSignals(&signal);
        return VM_STATUS_FAILURE;
    }
    *newoffset = offset;
    MachineFileSeek(filedescriptor, offset, whence, FileCallback, &ThreadList[RunningThreadID]);
    Scheduler(VM_THREAD_STATE_WAITING);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMMutexCreate(TVMMutexIDRef mutexref) {
    if (mutexref == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    Mutex M;
    M.ID = MutexList.size();
    M.Locked = false;
    *mutexref = M.ID;
    MutexList.push_back(M);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMMutexDelete(TVMMutexID mutex) {
    if (mutex >= MutexList.size() || mutex < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if (MutexList[mutex].Locked) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MutexList.erase(MutexList.begin() + mutex);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref) {
    if (ownerref == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (mutex >= MutexList.size() || mutex < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (!MutexList[mutex].Locked) {
        *ownerref = VM_STATUS_ERROR_INVALID_ID;
    }
    *ownerref = MutexList[mutex].TID;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout) {
    if (mutex >= MutexList.size() || mutex < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if (timeout == VM_TIMEOUT_IMMEDIATE && MutexList[mutex].Locked == true) {
        return VM_STATUS_FAILURE;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (MutexList[mutex].Locked && MutexList[mutex].TID == RunningThreadID) {
        MachineResumeSignals(&signal);
        return VM_STATUS_SUCCESS;
    } else if (!MutexList[mutex].Locked) {
        MutexList[mutex].Locked = true;
        MutexList[mutex].TID = RunningThreadID;
        MachineResumeSignals(&signal);
        return VM_STATUS_SUCCESS;
    }
    TCB WaitingThread = ThreadList[RunningThreadID];
    WaitingThread.HaveMutex = true;
    ThreadList[RunningThreadID] = WaitingThread;
    MutexList[mutex].WaitingList.push_back(WaitingThread);
    if (timeout == VM_TIMEOUT_INFINITE) {
        VMThreadSleep(-1000000);//special value for infinite waiting.
    } else {
        VMThreadSleep(timeout);
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMMutexRelease(TVMMutexID mutex) {
    if (mutex >= MutexList.size() || mutex < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if (MutexList[mutex].TID == RunningThreadID) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MutexList[mutex].Locked = false;
    TCB ReadyThread;
    if (!MutexList[mutex].WaitingList.empty()) {
        SORT(MutexList[mutex].WaitingList);
        ReadyThread = MutexList[mutex].WaitingList.front();
        MutexList[mutex].WaitingList.erase(MutexList[mutex].WaitingList.begin());
        if (ThreadList[ReadyThread.ID].HaveMutex) {
            MutexList[mutex].Locked = true;
            MutexList[mutex].TID = ReadyThread.ID;
            if (ThreadList[ReadyThread.ID].Priority > ThreadList[RunningThreadID].Priority) {
                ThreadList[ReadyThread.ID].State = VM_THREAD_STATE_READY;
                Ready.push_back(ThreadList[ReadyThread.ID]);
                Scheduler(VM_THREAD_STATE_READY);
            } else {
                ThreadList[ReadyThread.ID].State = VM_THREAD_STATE_READY;
                Ready.push_back(ThreadList[ReadyThread.ID]);
            }
        }
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
}

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
#include <fcntl.h>


extern "C" {
#define PL() std::cout << "@ line" <<__LINE__
typedef void (*TVMMainEntry)(int, char *[]);
#define VMPrint(format, ...) VMFilePrint ( 1, format, ##__VA_ARGS__)
#define VMPrintError(format, ...) VMFilePrint ( 2, format, ##__VA_ARGS__)
TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);
#define VM_TIMEOUT_INFINITE ((TVMTick)0)
#define VM_TIMEOUT_IMMEDIATE ((TVMTick)-1)
void Scheduler(TVMThreadState NextState,int Next);
volatile int Tickms = 0;
volatile int tickCount = 0;
volatile TVMThreadID RunningThreadID;
int NumOfChunks;
void CreateShareMemory(void *pointer, int Size);
bool AllocateMemory(void **pointer);
void DeallocateMemory(void *pointer);
void Mount(const char *Image);
void ReadSector(int SectorOffset, int BytesOffset, int NumBytes);
void WriteSector(int SectorOffset, int BytesOffset, int NumBytes);
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
    int AllocatedSize;
    bool Dead;
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
struct Chunk { //from OH
    void *Base;
    bool Occupied;
};
std::vector<Chunk> MemoryChunks;
std::vector<TCB> MemoryWaitingList;
TVMMutexID FSMutex;
volatile int FileSystemFD;
uint8_t * Buffer;
struct BPB{
    uint32_t BS_jmpBoot;
    uint8_t BS_OEMName[8];
    uint16_t BPB_BytsPerSec;
    uint8_t BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;
    uint8_t BS_DrvNum;
    uint8_t BS_Reserved1;
    uint8_t BS_BootSig;
    uint32_t BS_VolID;
    uint8_t BS_VolLab[11];
    uint8_t BS_FilSysType[8];
    int FirstRootSector;
    int RootDirectorySectors;
    int FirstDataSector;
    int ClusterCount;
} BPB;
class FAT{
public:
    std::vector<uint16_t> Entries;
    void Read(){
        for(int i = 0; i < BPB.BPB_FATSz16; i++){
            ReadSector(BPB.BPB_RsvdSecCnt + i, 0, 512);
            for(int j = 0;j < 512; j+=2){
                uint16_t result = Buffer[i] + ((uint16_t)(Buffer[i + 1]) << 8 );
                Entries.push_back(result);
            }
        }
    }
//    void Write(){
//        for ( int a = 0;a < BPB.BPB_NumFATs; a++){
//            for(int i = 0; i < BPB.BPB_FATSz16; i++){
//                for(int j = 0;j < 256; j++){
//                    int result = Buffer[i] + ((uint16_t)(Buffer[i + 1]) << 8 );
//                    Entries.push_back(result);
//                }
//            }
//        }
//    }
};
FAT Fat;
class Directory{

};
void debug() {
    std:: cout<< "CurrerntThread: " << RunningThreadID << std::endl;
    for (auto & i : MutexList){
        std::cout<< "ID: " << i.ID <<" TID: " << i.TID << " Locked: " << i.Locked << " Waiting: ";
        for ( auto & j : i.WaitingList){
            std::cout << j.ID<< " ";
        }
        std::cout << std::endl;
    }
//    for (auto & i : ThreadList){
//        std::cout<< "ID: " << i.ID <<" State: " << i.State << std::endl;
//    }
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
    if( ThreadList.size() > 0){
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
        Scheduler(VM_THREAD_STATE_READY, -1);
    }

    MachineResumeSignals(&signal);
}
void Scheduler(TVMThreadState NextState, int Next) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    TCB NextThread;
    int TempID = RunningThreadID;
    if (Next == -1){

        NextThread = ThreadList[1];
        if (!Ready.empty()) {
            SORT(Ready);//sort the ready list by priority
            NextThread = Ready.front();
            Ready.erase(Ready.begin());
            if (ThreadList[NextThread.ID].State == VM_THREAD_STATE_READY) {
                NextThread = ThreadList[NextThread.ID];
            }
        }
    }else {
        NextThread = ThreadList[Next];
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
        //no need to switch
        MachineResumeSignals(&signal);
        return;
    }
    ThreadList[NextThread.ID].State = VM_THREAD_STATE_RUNNING;
    RunningThreadID = NextThread.ID;
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
TVMStatus VMStart(int tickms, TVMMemorySize sharedsize, const char *mount, int argc, char *argv[]){
    void *Sharememory = MachineInitialize(sharedsize);
    CreateShareMemory(Sharememory, sharedsize);
    Tickms = tickms;
    MachineRequestAlarm(tickms * 1000, Callback, NULL);
    TVMMainEntry entry = VMLoadModule(argv[0]);
    if (entry != NULL) {
        TVMThreadID id;
        VMThreadCreate(skeleton, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, &id);//creating main thread
        VMThreadCreate(IdleThread, NULL, 0x100000, 0, &id);// creating idel thread
        VMThreadActivate(id);
        ThreadList[0].State = VM_THREAD_STATE_RUNNING;
    } else {
        MachineTerminate();
        return VM_STATUS_FAILURE;
    }
    Mount(mount);
    MachineEnableSignals();
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
        thread.Dead = false;
        ThreadList.push_back(thread);
        MachineResumeSignals(&signal);
    }
    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadDelete(TVMThreadID thread) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if(thread > ThreadList.size() || thread < 0){
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (!ThreadList[thread].Dead){
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_STATE;
    } else if (ThreadList[thread].Dead){
        ThreadList.erase(ThreadList.begin() + thread);
    }

    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadActivate(TVMThreadID thread) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (thread > ThreadList.size() || thread < 0) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (ThreadList[thread].State != VM_THREAD_STATE_DEAD) {
        MachineResumeSignals(&signal);
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
        Scheduler(VM_THREAD_STATE_READY, -1);
    }

    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadTerminate(TVMThreadID thread) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (thread > ThreadList.size() || thread < 0) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (ThreadList[thread].State == VM_THREAD_STATE_DEAD) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    for (auto &i : MutexList) {
        if (i.Locked && i.TID == thread) {
            i.Locked = false;
            TCB ReadyThread{};
            if(!i.WaitingList.empty()){
                SORT(i.WaitingList);
                ReadyThread = i.WaitingList.front();
                i.WaitingList.erase(i.WaitingList.begin());
                if(ThreadList[ReadyThread.ID].HaveMutex){
                    i.TID = ReadyThread.ID;
                    i.Locked = true;
                    if(ThreadList[ReadyThread.ID].Priority > ThreadList[RunningThreadID].Priority){
                        ThreadList[ReadyThread.ID].State = VM_THREAD_STATE_READY;
                        Scheduler(VM_THREAD_STATE_READY,ReadyThread.ID);
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
            if ( i.State == VM_THREAD_STATE_RUNNING){
                if(Ready.empty() && Sleep.empty()){
                    MachineResumeSignals(&signal);
                    return VM_STATUS_SUCCESS;
                }
                Scheduler(VM_THREAD_STATE_DEAD, -1);
            }else if ( ThreadList[thread].State == VM_THREAD_STATE_WAITING){
                for (int j = 0 ; j < (int)Sleep.size();j++){
                    if(ThreadList[Sleep[j].ID].ID == thread){
                        Sleep.erase(Sleep.begin() + j);
                        break;
                    }
                }
            }
        }
    }
    ThreadList[thread].State = VM_THREAD_STATE_DEAD;
    ThreadList[thread].Dead = true;
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
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
void FileCallback(void *calldata, int result) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    TCB *thread = (TCB *) calldata;
    thread->FileData = result;
    thread->State = VM_THREAD_STATE_READY;
    Scheduler(VM_THREAD_STATE_READY, -1);
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
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    *filedescriptor = ThreadList[RunningThreadID].FileData;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileClose(int filedescriptor) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MachineFileClose(filedescriptor, FileCallback, &ThreadList[RunningThreadID]);
    Scheduler(VM_THREAD_STATE_WAITING, -1);
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
    int Byteleft = *length;
    *length = 0;
    int ReadSize = 0;
    char *Dataptr = (char *) data;
    void *SharePtr = nullptr;
    while (Byteleft > 0) {
        if (Byteleft >= 512) {
            ReadSize = 512;
            Byteleft -= 512;
        } else if (Byteleft < 512) {
            ReadSize = Byteleft;
            Byteleft = 0;
        }
        if (!AllocateMemory(&SharePtr)) {
            //if no memory available, schedule and wait for there is memory available
            ThreadList[RunningThreadID].AllocatedSize = ReadSize;
            MemoryWaitingList.push_back(ThreadList[RunningThreadID]);
            Scheduler(VM_THREAD_STATE_WAITING, -1);
            AllocateMemory(&SharePtr);
        }
        MachineFileRead(filedescriptor, SharePtr, ReadSize, FileCallback, &ThreadList[RunningThreadID]);
        Scheduler(VM_THREAD_STATE_WAITING, -1);
        std::memcpy(Dataptr, SharePtr, ReadSize);
        DeallocateMemory(SharePtr);
        *length += ThreadList[RunningThreadID].FileData;
        Dataptr += ReadSize;
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileWrite(int filedescriptor, void *data, int *length) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (length == nullptr || data == nullptr) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    int Byteleft = *length;
    *length = 0;
    int ReadSize = 0;
    char *Dataptr = (char *) data;
    void *SharePtr = nullptr;
    while (Byteleft > 0) {
        if (Byteleft >= 512) {
            ReadSize = 512;
            Byteleft -= 512;
        } else if (Byteleft < 512) {
            ReadSize = Byteleft;
            Byteleft = 0;
        }
        if (!AllocateMemory(&SharePtr)) {
            //if no memory available, schedule and wait for there is memory available
            ThreadList[RunningThreadID].AllocatedSize = ReadSize;
            MemoryWaitingList.push_back(ThreadList[RunningThreadID]);
            Scheduler(VM_THREAD_STATE_WAITING, -1);
            AllocateMemory(&SharePtr);
        }
        std::memcpy(SharePtr, Dataptr, ReadSize);
        MachineFileWrite(filedescriptor, SharePtr, ReadSize, FileCallback, &ThreadList[RunningThreadID]);
        Scheduler(VM_THREAD_STATE_WAITING, -1);
        DeallocateMemory(SharePtr);
        *length += ThreadList[RunningThreadID].FileData;
        Dataptr += ReadSize;
    }
    //std::cout << (char*)SharePtr <<" "<<ReadSize << std::endl;
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
    Scheduler(VM_THREAD_STATE_WAITING, -1);
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
//    std::cout <<MutexList[mutex].TID << std::endl;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout) {
//    debug();
//    std::cout<< RunningThreadID << " acquire " <<mutex <<" " << MutexList[mutex].TID;
//    std:: cout << " "<< MutexList[mutex].Locked << " " << MutexList[mutex].WaitingList.size() << std:: endl;
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (mutex >= MutexList.size() || mutex < 0) {
        MachineResumeSignals(&signal);
//        std::cout<<"VM_STATUS_ERROR_INVALID_ID" << std::endl;
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if (MutexList[mutex].Locked && MutexList[mutex].TID == RunningThreadID) {
        MachineResumeSignals(&signal);
        return VM_STATUS_SUCCESS;
    } else if (!MutexList[mutex].Locked) {
        MutexList[mutex].Locked = true;
        MutexList[mutex].TID = RunningThreadID;
        MachineResumeSignals(&signal);
        return VM_STATUS_SUCCESS;
    }
    if (timeout == VM_TIMEOUT_IMMEDIATE && MutexList[mutex].Locked) {
        MachineResumeSignals(&signal);
        return VM_STATUS_FAILURE;
    }


    TCB WaitingThread = ThreadList[RunningThreadID];
    WaitingThread.HaveMutex = true;
    ThreadList[RunningThreadID] = WaitingThread;
    MutexList[mutex].WaitingList.push_back(WaitingThread);

    VMThreadSleep(timeout);
    MachineResumeSignals(&signal);
    if(MutexList[mutex].TID != RunningThreadID){
        return VM_STATUS_FAILURE;
    }

    return VM_STATUS_SUCCESS;
}
TVMStatus VMMutexRelease(TVMMutexID mutex) {
//    std::cout << "Thread: " << RunningThreadID << " release mutex: " << mutex << std::endl;
//    debug();
    if (mutex >= MutexList.size() || mutex < 0) {
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if (MutexList[mutex].TID != RunningThreadID) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MutexList[mutex].Locked = false;
    MutexList[mutex].TID = -1;
    TCB ReadyThread{};
    if (!MutexList[mutex].WaitingList.empty()) {// if a thread is waiting for this mutex.
        SORT(MutexList[mutex].WaitingList);
        ReadyThread = MutexList[mutex].WaitingList.front();
        MutexList[mutex].WaitingList.erase(MutexList[mutex].WaitingList.begin());
        if (ThreadList[ReadyThread.ID].HaveMutex) {
            MutexList[mutex].Locked = true;
            MutexList[mutex].TID = ReadyThread.ID;
            if (ThreadList[ReadyThread.ID].Priority > ThreadList[RunningThreadID].Priority) {
                ThreadList[ReadyThread.ID].State = VM_THREAD_STATE_READY;
                Scheduler(VM_THREAD_STATE_READY, ReadyThread.ID);
            }
            else {
                ThreadList[ReadyThread.ID].State = VM_THREAD_STATE_READY;
                Ready.push_back(ThreadList[ReadyThread.ID]);
            }
        }
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
bool AllocateMemory(void **pointer) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    for (auto &MemoryChunk : MemoryChunks) {
        if (!MemoryChunk.Occupied) {
            MemoryChunk.Occupied = true;
            *pointer = MemoryChunk.Base;
            MachineResumeSignals(&signal);
            return true;
        }
    }
    MachineResumeSignals(&signal);
    // no memory available
    return false;
}
void CreateShareMemory(void *pointer, int Size) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    auto temp = (char *) pointer + 512;
    if (Size % 512 == 0)
        NumOfChunks = Size / 512;
    else
        NumOfChunks = Size / 512 + 1;
    do {
        Chunk C{};
        C.Base = temp;
        C.Occupied = false;
        MemoryChunks.push_back(C);
        temp += 512;
    } while ((int) MemoryChunks.size() <= NumOfChunks);
    MachineResumeSignals(&signal);
}
void DeallocateMemory(void *pointer) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    for (auto &MemoryChunk : MemoryChunks) {
        if (MemoryChunk.Base == pointer) {
            MemoryChunk.Occupied = false;
        }
    }
    if (!MemoryWaitingList.empty()) {
        int id = MemoryWaitingList.front().ID;
        for (auto & MemoryChunk : MemoryChunks) {
            if (MemoryChunk.Occupied) {
                MemoryWaitingList.erase(MemoryWaitingList.begin());
                if (ThreadList[id].Priority > ThreadList[RunningThreadID].Priority) {
                    ThreadList[id].State = VM_THREAD_STATE_READY;
                    Ready.push_back(ThreadList[id]);
                    Scheduler(VM_THREAD_STATE_READY, -1);
                } else {
                    ThreadList[id].State = VM_THREAD_STATE_READY;
                    Ready.push_back(ThreadList[id]);
                }
            }
        }
    }
    MachineResumeSignals(&signal);
}
void ReadSector(int SectorOffset, int BytesOffset, int NumBytes){
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MachineFileSeek(FileSystemFD, SectorOffset * 512 + BytesOffset, 0, FileCallback,&ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineFileRead(FileSystemFD, Buffer, NumBytes, FileCallback,&ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineResumeSignals(&signal);
}
void WriteSector(int SectorOffset, int BytesOffset, int NumBytes){
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MachineFileSeek(FileSystemFD, SectorOffset * 512 + BytesOffset, 0, FileCallback,&ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineFileWrite(FileSystemFD, Buffer, NumBytes, FileCallback,&ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineResumeSignals(&signal);
}
void Mount(const char *Image) {
    VMMutexCreate(&FSMutex);
    MachineFileOpen(Image, O_RDWR, 0, FileCallback, &ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    FileSystemFD = ThreadList[0].FileData;
    AllocateMemory((void**)&Buffer);
    //Reading BPB
    ReadSector(0,0,512);
    BPB.BPB_SecPerClus = *(uint8_t*)(Buffer+ 13);
    BPB.BPB_RsvdSecCnt = *(uint16_t*)(Buffer+ 14);
    BPB.BPB_NumFATs =  *(uint8_t*)(Buffer+ 16);
    BPB.BPB_RootEntCnt = *(uint16_t*)(Buffer+ 17);
    BPB.BPB_TotSec16 = *(uint16_t*)(Buffer+ 19);
    BPB.BPB_FATSz16 = *(uint16_t*)(Buffer+ 22);
    BPB.BPB_SecPerTrk = *(uint16_t*)(Buffer+ 24);
    BPB.BPB_TotSec32 = *(uint32_t*)(Buffer+ 32);
    BPB.FirstRootSector = BPB.BPB_RsvdSecCnt + BPB.BPB_NumFATs * BPB.BPB_FATSz16;
    BPB.RootDirectorySectors = (BPB.BPB_RootEntCnt * 32) / 512;
    BPB.FirstDataSector = BPB.FirstRootSector + BPB.RootDirectorySectors;
    if (BPB.BPB_TotSec16 == 0)
        BPB.ClusterCount = (BPB.BPB_TotSec32 - BPB.FirstDataSector) / BPB.BPB_SecPerClus;
    else
        BPB.ClusterCount = (BPB.BPB_TotSec16 - BPB.FirstDataSector) / BPB.BPB_SecPerClus;
    //Reading FAT
    Fat.Read();
//    for(int i = 0; i <BPB.BPB_FATSz16 *256 ; i++ ){
//        printf("%04X",Fat.Entries[i]);
//        printf("%s"," ");
//    }
    std::cout << BPB.ClusterCount;
}
}



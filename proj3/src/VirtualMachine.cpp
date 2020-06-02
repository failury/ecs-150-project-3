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
#include <string>
extern "C" {
#define PL() std::cout << "@ line" <<__LINE__
typedef void (*TVMMainEntry)(int, char *[]);
#define VMPrint(format, ...) VMFilePrint ( 1, format, ##__VA_ARGS__)
#define VMPrintError(format, ...) VMFilePrint ( 2, format, ##__VA_ARGS__)
TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);
#define VM_TIMEOUT_INFINITE ((TVMTick)0)
#define VM_TIMEOUT_IMMEDIATE ((TVMTick)-1)
void Scheduler(TVMThreadState NextState, int Next);
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
void ReadRoot();
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
    std::vector<int> FDS;
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
uint8_t *Buffer;
volatile int DirectoryIndex;
std::vector<int> FileDescriptors;
volatile int GlobalFD = 4;
struct BPB {
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
class FAT {
public:
    std::vector<uint16_t> Entries;
    void Read() {
        for (int i = 0; i < BPB.BPB_FATSz16; i++) {
            ReadSector(BPB.BPB_RsvdSecCnt + i, 0, 512);
            for (int j = 0; j < 512; j += 2) {
                uint16_t result = (int) Buffer[j] + ((uint16_t) (Buffer[j + 1]) << 8);
                Entries.push_back(result);
            }
        }
    }
    void FreeCluster(int Offset){
        int NextCluster;
        int Previous = Offset;
        while(Entries[Offset] < 0xFFF8){
            NextCluster = Entries[Offset];
            Entries[Offset] = 0;
            Offset = NextCluster;
        }
        Entries[Offset] = 0;
        Entries[Previous] = 0xFFF8;
    }
    void Write(int Offset){
        for ( int i = 0; i < BPB.BPB_FATSz16;i++){
            for(int j = 0; j < 256; j++){
                uint16_t Content = Entries[i *256 + j];
                Content = ((Content & 0xff) << 8) + ((Content >> 8) & 0xff);
                memcpy(Buffer + j *2, &Content, 2);
            }
            WriteSector(BPB.BPB_RsvdSecCnt + Offset * BPB.BPB_FATSz16 + i, 0, 512);
        }
    }
};
FAT Fat;
class File {
public:
    bool Changed;
    bool Accessed;
    int DistToBegin;
    uint32_t Size;
    uint16_t FirstClusterNum;
    uint8_t Ntr;
    uint16_t DIR_FstClusLO;

    class fileinfo {
    public:
        int FD;
        TVMThreadID TID;
        int Flag;
        int ByteOffest;
        int ClustorOffest;
        int ClustorNum;
    };

    std::vector<fileinfo> FIS;

    SVMDirectoryEntry FileEntry;

    File() {};
    File(int i, int j) {
        Changed = false;
        DistToBegin = j * 512 + i;
        Size = (uint32_t) (Buffer[i + 28] + (((uint16_t) Buffer[i + 29]) << 8) + (((uint32_t) Buffer[i + 30]) << 16) +
                           (((uint32_t) Buffer[i + 31]) << 24));
        FileEntry.DSize = Size;
        FirstClusterNum = (uint16_t) (Buffer[i + 26] + (((uint16_t) Buffer[i + 27]) << 8));
        FileEntry.DAttributes = (uint8_t) Buffer[i + 11];
        std::string FileName;
        for (int k = 0; k < 11; k++) {
            if (k == 8 && !(FileEntry.DAttributes & 0x10)) FileName.push_back('.');
            if (Buffer[i + k] == ' ') continue;
            FileName.push_back(Buffer[i + k]);
        }
        Ntr = (uint8_t) Buffer[i + 12];
        DIR_FstClusLO = (uint16_t) Buffer[i + 26];
        strcpy(FileEntry.DShortFileName, FileName.c_str());
        uint16_t Temp = (uint16_t) (Buffer[i + 14] + (((uint16_t) Buffer[i + 15]) << 8));
        FileEntry.DCreate.DSecond = (Temp & 0x1f) << 1;
        FileEntry.DCreate.DMinute = (Temp >> 5) & 0x3f;
        FileEntry.DCreate.DHour = Temp >> 11;
        Temp = (uint16_t) (Buffer[i + 16] + (((uint16_t) Buffer[i + 17]) << 8));
        FileEntry.DCreate.DDay = Temp & 0x1f;
        FileEntry.DCreate.DMonth = (Temp >> 5) & 0x0f;
        FileEntry.DCreate.DYear = (Temp >> 9) + 1980;
        FileEntry.DCreate.DHundredth = Buffer[i + 13];
        Temp = (uint16_t) (Buffer[i + 18] + (((uint16_t) Buffer[i + 19]) << 8));
        FileEntry.DAccess.DDay = Temp & 0x1f;
        FileEntry.DAccess.DMonth = (Temp >> 5) & 0x0f;
        FileEntry.DAccess.DYear = (Temp >> 9) + 1980;
        Temp = (uint16_t) (Buffer[i + 22] + (((uint16_t) Buffer[i + 23]) << 8));
        FileEntry.DModify.DSecond = (Temp & 0x1f) << 1;
        FileEntry.DModify.DMinute = (Temp >> 5) & 0x3f;
        FileEntry.DModify.DHour = (Temp >> 11) & 0x1f;
        Temp = (uint16_t) (Buffer[i + 24] + (((uint16_t) Buffer[i + 25]) << 8));
        FileEntry.DModify.DDay = Temp & 0x1f;
        FileEntry.DModify.DMonth = (Temp >> 5) & 0x0f;
        FileEntry.DModify.DYear = (Temp >> 9) + 1980;
    }
    File(const char* filename, int offset){
        Changed = false;
        Accessed = true;
        Size = 0;
        Ntr = 0;
        DistToBegin = offset;
        FirstClusterNum = -1;
        for ( int i = 0; i < Fat.Entries.size(); i++){
            if( Fat.Entries[i] ==0 ){
                Fat.Entries[i] = 0xFFF8;
                FirstClusterNum = i;
                break;
            }
        }
        strcpy(FileEntry.DShortFileName, filename);
        FileEntry.DSize = Size;
        FileEntry.DShortFileName[strlen(filename)] = '\0';
        VMDateTime(&FileEntry.DCreate);
        VMDateTime(&FileEntry.DAccess);
        VMDateTime(&FileEntry.DModify);
        FileEntry.DAttributes = 0;
    }
    bool Open(int *FileDesscriptor, int Flags) {
        if (((Flags & O_RDWR) || (Flags & O_WRONLY) || (Flags & O_APPEND) || (Flags & O_TRUNC)) &&
            FileEntry.DAttributes == VM_FILE_SYSTEM_ATTR_READ_ONLY) {
            return false;
        }
        else if (FileEntry.DAttributes == VM_FILE_SYSTEM_ATTR_DIRECTORY ||
                 FileEntry.DAttributes == VM_FILE_SYSTEM_ATTR_VOLUME_ID){
            return false;
        }
        fileinfo FI;
        FI.TID = RunningThreadID;
        FI.Flag = Flags;
        FI.ByteOffest = 0;
        FI.ClustorOffest = 0;
        FI.ClustorNum = FirstClusterNum;
        Accessed = true;
        if (FileDescriptors.size() > 0) {
            FI.FD = FileDescriptors[0];
            FileDescriptors.erase(FileDescriptors.begin());
        } else {
            FI.FD = GlobalFD;
            GlobalFD++;
        }
        if (Flags & O_APPEND) {
            FI.ByteOffest = Size % (BPB.BPB_SecPerClus * 512);
            FI.ClustorOffest = Size / (BPB.BPB_SecPerClus * 512);
            while (Fat.Entries[FirstClusterNum] < 0xFFF8) {
                FirstClusterNum = Fat.Entries[FirstClusterNum];
            }
            FI.ClustorNum = FirstClusterNum;
        }
        if(Flags & O_TRUNC){
            Fat.FreeCluster(FirstClusterNum);
            Size = 0;
        }
        FIS.push_back(FI);
        ThreadList[RunningThreadID].FDS.push_back(FI.FD);
        *FileDesscriptor = FI.FD;
        VMDateTime(&FileEntry.DAccess);
        return true;
    }

    bool Close(int Index) {
        for (int i = 0; i < ThreadList[RunningThreadID].FDS.size(); i++) {
            if (ThreadList[RunningThreadID].FDS[i] == FIS[Index].FD) {
                FIS.erase(FIS.begin() + Index);
                ThreadList[RunningThreadID].FDS.erase(ThreadList[RunningThreadID].FDS.begin() + i);
                VMDateTime(&FileEntry.DAccess);
                return true;
            }
        }
        return false;
    }

    void ReadCluster(int ClusterNum, int &Offset, int Length, char *DataPtr) {
        VMMutexAcquire(FSMutex, VM_TIMEOUT_INFINITE);
        if (Length < 512) {
            ReadSector(ClusterNum * BPB.BPB_SecPerClus + BPB.FirstDataSector, Offset, Length);
            memcpy(DataPtr, Buffer, Length);
            Offset += Length;
        } else {
            int ReadSize = 0;
            while (Length > 0) {
                if (Length > 512) {
                    ReadSize = 512;
                    Length -= 512;
                } else {
                    ReadSize = Length;
                    Length = 0;
                }
                memcpy(DataPtr, Buffer, ReadSize);
                ReadSector(ClusterNum * BPB.BPB_SecPerClus + BPB.FirstDataSector, Offset, ReadSize);
                DataPtr += ReadSize;
                Offset += ReadSize;
            }
        }
        VMMutexRelease(FSMutex);
    }

    void WriteCluster(int ClusterNum, int &Offset, int Length, char *DataPtr) {
        VMMutexAcquire(FSMutex, VM_TIMEOUT_INFINITE);
        if (Length < 512) {
            memcpy(Buffer, DataPtr, Length);
            WriteSector(ClusterNum * BPB.BPB_SecPerClus + BPB.FirstDataSector, Offset, Length);
            Offset += Length;

        } else {
            int WriteSize = 0;
            while (Length > 0) {
                if (Length > 512) {
                    WriteSize = 512;
                    Length -= 512;
                } else {
                    WriteSize = Length;
                    Length = 0;
                }
                memcpy(Buffer, DataPtr, WriteSize);
                WriteSector(ClusterNum * BPB.BPB_SecPerClus + BPB.FirstDataSector, Offset, WriteSize);
                DataPtr += WriteSize;
                Offset += WriteSize;
            }
        }
        VMMutexRelease(FSMutex);
    }

    void Read(int Index, void *Data, int *Length) {
        if (*Length > (Size - FIS[Index].ClustorOffest * BPB.BPB_SecPerClus * 512 - FIS[Index].ByteOffest))
            *Length = Size - FIS[Index].ClustorOffest * BPB.BPB_SecPerClus * 512 - FIS[Index].ByteOffest;
        int NumByte = *Length;
        int SizeLeft = BPB.BPB_SecPerClus * 512 - FIS[Index].ByteOffest;
        char *DataPtr = (char *) Data;
        if (SizeLeft >= NumByte) {
            ReadCluster(FIS[Index].ClustorNum, FIS[Index].ByteOffest, NumByte, DataPtr);
        } else {
            while (NumByte > 0) {
                NumByte -= SizeLeft;
                ReadCluster(FIS[Index].ClustorNum, FIS[Index].ByteOffest, NumByte, DataPtr);
                FIS[Index].ByteOffest = 0;
                FIS[Index].ClustorOffest += 1;
                FIS[Index].ClustorNum = Fat.Entries[FIS[Index].ClustorNum];
                SizeLeft = BPB.BPB_SecPerClus * 512;
                if (SizeLeft > NumByte) {
                    ReadCluster(FIS[Index].ClustorNum, FIS[Index].ByteOffest, NumByte, DataPtr);
                    break;
                }
            }
        }
    }
    void Write(int Index, void *Data, int *Length){
        if(!(FIS[Index].Flag & O_APPEND)){
            FIS[Index].ByteOffest = 0;
            FIS[Index].ClustorOffest = 0;
        }
        int NumByte = *Length;
        char *DataPtr = (char *) Data;
        while ( NumByte > 0) {
            if( NumByte > BPB.BPB_SecPerClus * 512){
                NumByte -= 512 * BPB.BPB_SecPerClus;
                WriteCluster(FIS[Index].ClustorNum, FIS[Index].ByteOffest, 512 * BPB.BPB_SecPerClus, DataPtr);
                if(Fat.Entries[FIS[Index].ClustorNum] >= 0xfff8){//
                    int temp = -1;
                    for (int i = 0; i < (int) Fat.Entries.size(); i++) {
                        if (Fat.Entries[i] == 0) {
                            if(FIS[Index].ClustorNum != -1){
                                Fat.Entries[FIS[Index].ClustorNum] = i;
                            }
                            Fat.Entries[i] = 0xFFF8;
                            temp = i;
                            break;
                        }
                    }
                    FIS[Index].ClustorNum = temp;
                } else {
                    FIS[Index].ClustorNum = Fat.Entries[FIS[Index].ClustorNum];
                }
            } else {
                WriteCluster(FIS[Index].ClustorNum, FIS[Index].ByteOffest, 512 * BPB.BPB_SecPerClus, DataPtr);
                int NextCluster;
                int StartCluster = FIS[Index].ClustorNum;
                while(Fat.Entries[FIS[Index].ClustorNum] < 0xFFF8) {
                    NextCluster = Fat.Entries[FIS[Index].ClustorNum];
                    Fat.Entries[FIS[Index].ClustorNum] = 0;
                    FIS[Index].ClustorNum = NextCluster;
                }
                Fat.Entries[FIS[Index].ClustorNum] = 0;
                Fat.Entries[StartCluster] = 0xFFF8;
                NumByte = 0;
            }
        }
        Size = *Length;
        FileEntry.DSize = Size;
    }
    bool Seek( int Orign, int Offset, int *DestOffset, int Index){
        if(Orign > Size) return false;
        FIS[Index].ClustorOffest = 0;
        FIS[Index].ClustorNum = FirstClusterNum;
        while (Orign > BPB.BPB_SecPerClus * 512){
            Orign -= BPB.BPB_SecPerClus * 512;
            FIS[Index].ClustorOffest++;
            FIS[Index].ClustorNum = Fat.Entries[FIS[Index].ClustorNum];
        }
        FIS[Index].ByteOffest = Orign;
        if(Orign + Offset <= Size) *DestOffset = Orign + Offset;
        else *DestOffset = Size;
        while (Offset > 0) {
            if ( BPB.BPB_SecPerClus * 512 - FIS[Index].ByteOffest > Offset){
                FIS[Index].ByteOffest = Offset;
                Offset = 0;
            } else {
                Offset -= (BPB.BPB_SecPerClus * 512 - FIS[Index].ByteOffest);
                FIS[Index].ByteOffest = 0;
                FIS[Index].ClustorOffest++;
                FIS[Index].ClustorNum = Fat.Entries[FIS[Index].ClustorNum];
            }
        }
        return true;
    }
    void Save(){
        int length = strlen(FileEntry.DShortFileName);
        if( FileEntry.DAttributes & 0x10){
            for ( int i =0; i < 11; i++){
                if ( length > i) {
                    Buffer[i] = FileEntry.DShortFileName[i];
                } else {
                    Buffer[i] = ' ';
                }
            }
        }  else {
            int i = 0;
            while ( FileEntry.DShortFileName[i] != '.'){
                Buffer[i] = FileEntry.DShortFileName[i];
                i++;
            }
            for ( int j = i; j < 8; j++) {
                Buffer[j] = 32;
            }
            Buffer[8] = FileEntry.DShortFileName[length - 3];
            Buffer[9] = FileEntry.DShortFileName[length - 2];
            Buffer[10] = FileEntry.DShortFileName[length - 1];
        }
        Buffer[11] = FileEntry.DAttributes;
        Buffer[12] = Ntr;
        Buffer[13] = FileEntry.DCreate.DHundredth;
        uint16_t temp_buffer = ((FileEntry.DCreate.DSecond >> 1) & 0x1F) | ((FileEntry.DCreate.DMinute & 0x3F ) << 5) | ( (FileEntry.DCreate.DHour & 0x1F) << 11);
        memcpy(Buffer + 14, &temp_buffer, 2);
        temp_buffer = ((FileEntry.DCreate.DYear - 1980) << 9) | ((FileEntry.DCreate.DMonth & 0x0f) << 5) | (FileEntry.DCreate.DDay & 0x1f);
        memcpy(Buffer + 16, &temp_buffer, 2);
        temp_buffer = ((FileEntry.DAccess.DYear - 1980) << 9) | ((FileEntry.DAccess.DMonth & 0x0f) << 5) | (FileEntry.DAccess.DDay & 0x1f);
        memcpy(Buffer + 18, &temp_buffer, 2);
        uint8_t zero = 0x0;
        memcpy(Buffer + 20, &zero, 1);
        memcpy(Buffer + 21, &zero, 1);
        temp_buffer = ((FileEntry.DModify.DSecond >> 1) & 0x1F) | ((FileEntry.DModify.DMinute & 0x3F ) << 5) | ( (FileEntry.DModify.DHour & 0x1F) << 11);
        memcpy(Buffer + 22, &temp_buffer, 2);
        temp_buffer = ((FileEntry.DModify.DYear - 1980) << 9) | ((FileEntry.DModify.DMonth & 0x0f) << 5) | (FileEntry.DModify.DDay & 0x1f);
        memcpy(Buffer + 24, &temp_buffer, 2);
        temp_buffer = ((FirstClusterNum << 8) | (FirstClusterNum >> 8));
        memcpy(Buffer + 26, &FirstClusterNum, 2);
        memcpy(Buffer + 28, &Size, 4);

        WriteSector(BPB.FirstRootSector, DistToBegin, 32);
    }
};
class Directory {
public:
    File DirFile;
    std::vector<File> files;
    char Path[VM_FILE_SYSTEM_MAX_PATH];
    int FFB;
    bool isOpen = false;
    int DirectoryOffset;
    bool FindFile(int FileDescriptor, int &index, int&FI){
        for(int i = 0; i < files.size(); i++){
            for ( int j = 0; j < files[i].FIS.size(); j++){
                if(files[i].FIS[j].FD == FileDescriptor && files[i].FIS[j].TID != RunningThreadID){
                    return false;
                }
                if(files[i].FIS[j].FD == FileDescriptor && files[i].FIS[j].TID == RunningThreadID){
                    index = i;
                    FI = j;
                    return true;
                }
            }
        }
        return false;
    }
    bool CreateFile( const char * FileName){
        if(DirectoryIndex == 0) {
            if ( FFB >= BPB.RootDirectorySectors * 512) return false;

            File f = File(FileName, FFB);
            FFB += 32;
            files.push_back(f);
        }
        return true;
    }
};
std::vector<Directory> Directories;


void debug() {
    std::cout << "CurrerntThread: " << RunningThreadID << std::endl;
    for (auto &i : MutexList) {
        std::cout << "ID: " << i.ID << " TID: " << i.TID << " Locked: " << i.Locked << " Waiting: ";
        for (auto &j : i.WaitingList) {
            std::cout << j.ID << " ";
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
    if (ThreadList.size() > 0) {
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
    if (Next == -1) {

        NextThread = ThreadList[1];
        if (!Ready.empty()) {
            SORT(Ready);//sort the ready list by priority
            NextThread = Ready.front();
            Ready.erase(Ready.begin());
            if (ThreadList[NextThread.ID].State == VM_THREAD_STATE_READY) {
                NextThread = ThreadList[NextThread.ID];
            }
        }
    } else {
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
TVMStatus VMStart(int tickms, TVMMemorySize sharedsize, const char *mount, int argc, char *argv[]) {
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
        VMMutexCreate(&FSMutex);
        AllocateMemory((void **) &Buffer);
        Mount(mount);
    } else {
        MachineTerminate();
        return VM_STATUS_FAILURE;
    }
    MachineEnableSignals();
    entry(argc, argv);
    for ( int i =0; i < BPB.BPB_NumFATs; i++){
        Fat.Write(i);
    }
    for (auto & file : Directories[0].files){
        if(file.Accessed) file.Save();
    }
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
    if (thread > ThreadList.size() || thread < 0) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (!ThreadList[thread].Dead) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_STATE;
    } else if (ThreadList[thread].Dead) {
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
            if (!i.WaitingList.empty()) {
                SORT(i.WaitingList);
                ReadyThread = i.WaitingList.front();
                i.WaitingList.erase(i.WaitingList.begin());
                if (ThreadList[ReadyThread.ID].HaveMutex) {
                    i.TID = ReadyThread.ID;
                    i.Locked = true;
                    if (ThreadList[ReadyThread.ID].Priority > ThreadList[RunningThreadID].Priority) {
                        ThreadList[ReadyThread.ID].State = VM_THREAD_STATE_READY;
                        Scheduler(VM_THREAD_STATE_READY, ReadyThread.ID);
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
            if (i.State == VM_THREAD_STATE_RUNNING) {
                if (Ready.empty() && Sleep.empty()) {
                    MachineResumeSignals(&signal);
                    return VM_STATUS_SUCCESS;
                }
                Scheduler(VM_THREAD_STATE_DEAD, -1);
            } else if (ThreadList[thread].State == VM_THREAD_STATE_WAITING) {
                for (int j = 0; j < (int) Sleep.size(); j++) {
                    if (ThreadList[Sleep[j].ID].ID == thread) {
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
    std::string NameStr(filename, filename + strlen(filename));
    std::transform(NameStr.begin(), NameStr.end(), NameStr.begin(), ::toupper);
    for(auto & file : Directories[DirectoryIndex].files){
        auto a = file.FileEntry.DShortFileName;
        std:: string TempString(a, a + strlen(a));
        if (TempString == NameStr ){
            if (!file.Open(filedescriptor,flags)){
                MachineResumeSignals(&signal);
                return VM_STATUS_FAILURE;
            }
        }
    }
    if (flags & O_CREAT) {

        if(!Directories[DirectoryIndex].CreateFile(NameStr.c_str())){
            MachineResumeSignals(&signal);
            return VM_STATUS_FAILURE;
        }
        Directories[DirectoryIndex].files[Directories[DirectoryIndex].files.size() - 1].Open(filedescriptor, flags);
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileClose(int filedescriptor) {
    int index;
    int FI;
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (filedescriptor < 3) {
        MachineFileClose(filedescriptor, FileCallback, &ThreadList[RunningThreadID]);
        Scheduler(VM_THREAD_STATE_WAITING, -1);
        MachineResumeSignals(&signal);
        return VM_STATUS_SUCCESS;
    } else {
        if(!Directories[DirectoryIndex].FindFile(filedescriptor,index,FI)){
            MachineResumeSignals(&signal);
            return VM_STATUS_FAILURE;
        }
        Directories[DirectoryIndex].files[index].Close(FI);
    }
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileRead(int filedescriptor, void *data, int *length) {
    int index;
    int FI;
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if (length == nullptr || data == nullptr) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (filedescriptor < 3) {
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
    } else {

        if(!Directories[DirectoryIndex].FindFile(filedescriptor,index,FI)){
            MachineResumeSignals(&signal);
            return VM_STATUS_FAILURE;
        }
        Directories[DirectoryIndex].files[index].Read(FI,data, length);
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileWrite(int filedescriptor, void *data, int *length) {
    int index;
    int FI;
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);

    if (length == nullptr || data == nullptr) {
        MachineResumeSignals(&signal);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if( filedescriptor < 3) {
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
    } else {
        if(!Directories[DirectoryIndex].FindFile(filedescriptor,index,FI)){
            MachineResumeSignals(&signal);
            return VM_STATUS_FAILURE;
        }
        Directories[DirectoryIndex].files[index].Write(FI,data, length);
    }
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
    int index;
    int FI;
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    if(filedescriptor < 3) {
        if (newoffset == NULL) {
            MachineResumeSignals(&signal);
            return VM_STATUS_FAILURE;
        }
        *newoffset = offset;
        MachineFileSeek(filedescriptor, offset, whence, FileCallback, &ThreadList[RunningThreadID]);
        Scheduler(VM_THREAD_STATE_WAITING, -1);
        MachineResumeSignals(&signal);
    } else {
        if(!Directories[DirectoryIndex].FindFile(filedescriptor,index,FI)){
            MachineResumeSignals(&signal);
            return VM_STATUS_FAILURE;
        }
        Directories[DirectoryIndex].files[index].Seek(whence, offset, newoffset, FI);
        MachineResumeSignals(&signal);
    }

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
    if (MutexList[mutex].TID != RunningThreadID) {
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
            } else {
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
        for (auto &MemoryChunk : MemoryChunks) {
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
void ReadSector(int SectorOffset, int BytesOffset, int NumBytes) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MachineFileSeek(FileSystemFD, SectorOffset * 512 + BytesOffset, 0, FileCallback, &ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineFileRead(FileSystemFD, Buffer, NumBytes, FileCallback, &ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineResumeSignals(&signal);
}
void WriteSector(int SectorOffset, int BytesOffset, int NumBytes) {
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    MachineFileSeek(FileSystemFD, SectorOffset * 512 + BytesOffset, 0, FileCallback, &ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineFileWrite(FileSystemFD, Buffer, NumBytes, FileCallback, &ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    MachineResumeSignals(&signal);
}
void Mount(const char *Image) {
    MachineFileOpen(Image, O_RDWR, 0, FileCallback, &ThreadList[RunningThreadID]);
    ThreadList[RunningThreadID].State = VM_THREAD_STATE_WAITING;
    Scheduler(VM_THREAD_STATE_WAITING, -1);
    FileSystemFD = ThreadList[0].FileData;
    //Reading BPB
    ReadSector(0, 0, 512);
    BPB.BPB_SecPerClus = *(uint8_t *) (Buffer + 13);
    BPB.BPB_RsvdSecCnt = *(uint16_t *) (Buffer + 14);
    BPB.BPB_NumFATs = *(uint8_t *) (Buffer + 16);
    BPB.BPB_RootEntCnt = *(uint16_t *) (Buffer + 17);
    BPB.BPB_TotSec16 = *(uint16_t *) (Buffer + 19);
    BPB.BPB_FATSz16 = *(uint16_t *) (Buffer + 22);
    BPB.BPB_SecPerTrk = *(uint16_t *) (Buffer + 24);
    BPB.BPB_TotSec32 = *(uint32_t *) (Buffer + 32);
    BPB.FirstRootSector = BPB.BPB_RsvdSecCnt + BPB.BPB_NumFATs * BPB.BPB_FATSz16;
    BPB.RootDirectorySectors = (BPB.BPB_RootEntCnt * 32) / 512;
    BPB.FirstDataSector = BPB.FirstRootSector + BPB.RootDirectorySectors;
    if (BPB.BPB_TotSec16 == 0)
        BPB.ClusterCount = (BPB.BPB_TotSec32 - BPB.FirstDataSector) / BPB.BPB_SecPerClus;
    else
        BPB.ClusterCount = (BPB.BPB_TotSec16 - BPB.FirstDataSector) / BPB.BPB_SecPerClus;
    //Reading FAT
    Fat.Read();
    ReadRoot();
}
void ReadRoot() {
    Directory Root;
    for (int i = 0; i < BPB.RootDirectorySectors; i++) {
        ReadSector(BPB.FirstRootSector + i, 0, 512);
        for (int j = 0; j < 512; j += 32) {
            if (Buffer[j] == 0x00) {
                Root.FFB = 512 * i + j;
                Directories.push_back(Root);
                return;
            }
            if (Buffer[j] == 0x0E5) {
                //the directory entry is free
                continue;
            }
            if (Buffer[j + 11] == 0x0f) {
                if (Buffer[j] == 0x01) j += 32;
                continue;
            }
            File file(j, i);
            if (i == 0 && j == 0) Root.DirFile = file;
            Root.files.push_back(file);
        }
    }
    Directories.push_back(Root);
}
TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor){
    if( dirname == nullptr || dirdescriptor == nullptr){
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(VMFileSystemValidPathName(dirname) == VM_STATUS_ERROR_INVALID_PARAMETER){
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(!strcmp(dirname, "/")){
        return VM_STATUS_FAILURE;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    //only for root for now
    Directories[0].isOpen = true;
    Directories[0].DirectoryOffset = 0;
    *dirdescriptor = 3;
    strcpy(Directories[0].Path,dirname);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMDirectoryClose(int dirdescriptor){
    if(dirdescriptor != 3) {
        return VM_STATUS_FAILURE;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    Directories[0].isOpen = false;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMDirectoryRead(int dirdescriptor,
                          SVMDirectoryEntryRef dirent){
    if (dirent == nullptr || dirdescriptor != 3) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (Directories[0].DirectoryOffset >= Directories[DirectoryIndex].files.size()) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);

    *dirent = Directories[0].files[Directories[0].DirectoryOffset].FileEntry;
    Directories[0].DirectoryOffset++;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMDirectoryRewind(int dirdescriptor){
    if (dirdescriptor != 50) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    Directories[0].DirectoryOffset = 0;
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMDirectoryCurrent(char *abspath){
    if (abspath == nullptr) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    strcpy(abspath, "/");
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMDirectoryChange(const char *path){
    if (path == nullptr) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signal;
    MachineSuspendSignals(&signal);
    char temp[VM_FILE_SYSTEM_MAX_PATH + 1];
    VMFileSystemGetAbsolutePath(temp, Directories[0].Path, path);
    MachineResumeSignals(&signal);
    return VM_STATUS_SUCCESS;
}
}



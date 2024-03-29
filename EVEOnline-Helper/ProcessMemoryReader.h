//
// Created by allan on 2024/3/17.
//

#ifndef EVEONLINE_HELPER_PROCESSMEMORYREADER_H
#define EVEONLINE_HELPER_PROCESSMEMORYREADER_H

#define LOGURU_WITH_STREAMS 1

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <vector>
#include <loguru.hpp>
//#include <Python.h>
#include <boost/thread.hpp>
#include <boost/bind/bind.hpp>
#include <boost/asio.hpp>

using namespace boost::placeholders;


struct MemoryRegion {
    MemoryRegion(PVOID baseAddress, std::vector<byte> &content) : baseAddress(baseAddress), content(content) { }
    MemoryRegion(PVOID baseAddress, SIZE_T length) : baseAddress(baseAddress) {
        this->content = std::vector<byte>(length);
    }
    MemoryRegion(PVOID baseAddress, SIZE_T length, byte* content) : baseAddress(baseAddress) {
        if (content != nullptr) {
            this->content = std::vector<byte>(content, content + length);
        }
    }
    PVOID baseAddress = nullptr;
    std::vector<byte> content;
};


class ProcessMemoryReader {
public:
    static std::vector<PVOID>* getBaseAddresses(HANDLE hProcess);
    static std::map<PVOID, MemoryRegion*>* readCommittedRegions(HANDLE hProcess, bool withContent = false);
    static std::map<PVOID, MemoryRegion*>* readCommittedRegions(HANDLE hProcess, std::map<PVOID, MemoryRegion*>* committedRegionsWOContent, int numThreads= 4);
    static std::vector<byte>* readBytes(PVOID address, SIZE_T length, std::map<PVOID, MemoryRegion*>* committedRegions);
    static std::string* readNullTerminatedAsciiString(PVOID address, std::map<PVOID, MemoryRegion*>* committedRegions, SIZE_T maxLength = 255);

private:
    static MemoryRegion* readCommittedRegion(HANDLE hProcess, MemoryRegion* region);
};

#endif //EVEONLINE_HELPER_PROCESSMEMORYREADER_H

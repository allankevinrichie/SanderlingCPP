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
    explicit ProcessMemoryReader(DWORD processId, uint8_t numThreads = 4) : processId(processId), numThreads(numThreads) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
        if (hProcess == nullptr) {
            LOG_S(ERROR) << "Failed to open process.";
            exit(-1);
        }
        readCommittedRegionsWoContent();
        readCommittedRegionContents();
        if (committedRegions == nullptr || committedRegions->empty()) {
            LOG_S(ERROR) << "Failed to load committed regions.";
            exit(-1);
        }
        LOG_S(INFO) << std::format("{} committed regions loaded.", committedRegions->size());
    }

    ~ProcessMemoryReader() {
        if (hProcess != nullptr) {
            CloseHandle(hProcess);
        }
        if (committedRegions != nullptr) {
            for (auto &[_, region]: *committedRegions) {
                delete region;
            }
            delete committedRegions;
        }
    }

    inline std::vector<byte>* readCachedBytes(PVOID address, SIZE_T length) const {
        if (committedRegions == nullptr) {
            LOG_S(WARNING) << "No committed regions loaded.";
            return nullptr;
        }
        auto ge = committedRegions->lower_bound(address);
        if (ge == committedRegions->begin()) {
            return nullptr;
        }
        auto region = (--ge)->second;

        if (region == nullptr) {
            return nullptr;
        }
        int64_t offset = (LPBYTE)address - (LPBYTE)region->baseAddress;
        if (offset < 0 || length <= 0 || offset >= region->content.size()) {
            return nullptr;
        }
        return new std::vector<byte>(region->content.begin() + offset, region->content.begin() + min(offset + length, region->content.size()));
    }

    inline std::string* readCachedNullTerminatedAsciiString(PVOID address, SIZE_T maxLength = 255) const {
        auto bytes = readCachedBytes(address, maxLength);
        if (bytes == nullptr) {
            return nullptr;
        }
        auto nullTerminatorIndex = (int64_t)bytes->size();
        for (auto i = 0; i < bytes->size(); i++) {
            if (bytes->at(i) == 0) {
                nullTerminatorIndex = i;
                break;
            }
        }
        return new std::string(bytes->begin(), bytes->begin() + nullTerminatorIndex);
    }

    template<class T> inline T* readCachedMemory(PVOID address) const {
        return (T*)readCachedBytes(address, sizeof(T));
    }

    template<class T> inline T* readCachedMemory(PVOID address, SIZE_T size) const {
        return readCachedBytes(address, sizeof(T) * size);
    }

    inline std::vector<byte>* readBytes(PVOID address, SIZE_T length) const {
        SIZE_T bytesRead;
        auto buffer = new std::vector<byte>(length);
        int tries = 0;
        do {
            ReadProcessMemory(hProcess, address, (LPVOID)buffer -> data(), length, &bytesRead);
            tries += 1;
        } while (tries <= 3 and bytesRead != length);
        if (bytesRead != length) {
            return nullptr;
        }
        return buffer;
    }

    inline std::string* readNullTerminatedAsciiString(PVOID address, SIZE_T maxLength = 255) const {
        auto bytes = readBytes(address, maxLength);
        if (bytes == nullptr) {
            return nullptr;
        }
        auto nullTerminatorIndex = (int64_t)bytes->size();
        for (auto i = 0; i < bytes->size(); i++) {
            if (bytes->at(i) == 0) {
                nullTerminatorIndex = i;
                break;
            }
        }
        return new std::string(bytes->begin(), bytes->begin() + nullTerminatorIndex);
    }

    template<class T> inline T* readMemory(PVOID address) const {
        return (T*)readBytes(address, sizeof(T));
    }

    template<class T> inline T* readMemory(PVOID address, SIZE_T size) const {
        return readBytes(address, sizeof(T) * size);
    }


protected:
    DWORD processId = 0;
    uint8_t numThreads = 4;
    HANDLE hProcess = nullptr;
    std::map<PVOID, MemoryRegion*>* committedRegions = nullptr;

private:
    inline void readCommittedRegionsWoContent() {
        LPCVOID address = nullptr;
        committedRegions = new std::map<PVOID, MemoryRegion*>();
        while (true) {
            MEMORY_BASIC_INFORMATION memoryInfo;
            auto result = VirtualQueryEx(hProcess, address, &memoryInfo, sizeof(memoryInfo));

            if (result != sizeof(memoryInfo)) {
                break;
            }
            address = (LPBYTE)memoryInfo.BaseAddress + memoryInfo.RegionSize;
            if (memoryInfo.State != MEM_COMMIT || memoryInfo.Protect & PAGE_GUARD || memoryInfo.Protect & PAGE_NOACCESS) {
                continue;
            }
            committedRegions->insert(std::pair<PVOID, MemoryRegion*>(memoryInfo.BaseAddress, new MemoryRegion(memoryInfo.BaseAddress, memoryInfo.RegionSize)));
        }
    }

    static inline void readCommittedRegionContent(const HANDLE &hProcess, MemoryRegion* &region) {
        SIZE_T bytesRead;
        int tries = 0;
        do {
            ReadProcessMemory(hProcess, region->baseAddress, (LPVOID)region->content.data(), region->content.size(), &bytesRead);
            tries += 1;
        } while (tries <= 3 and bytesRead != region->content.size());
        if (bytesRead != region->content.size()) {
            region->content = std::vector<byte>();
        }
    }

    inline void readCommittedRegionContents() {
        boost::asio::io_service ioService;
        boost::asio::io_service::work work(ioService);

        // Create a thread pool
        std::vector<boost::shared_ptr<boost::thread>> threads;
        for (int i = 0; i < numThreads; ++i) {
            boost::shared_ptr<boost::thread> thread(new boost::thread(
                    [ObjectPtr = &ioService] { return ObjectPtr->run(); }
            ));
            threads.push_back(thread);
        }
        for (auto &[_, region]: *committedRegions) {
            ioService.post([&] { ProcessMemoryReader::readCommittedRegionContent(hProcess, region); });
        }
        ioService.stop();
        for (auto &thread: threads) {
            thread->join();
        }
    }
};

#endif //EVEONLINE_HELPER_PROCESSMEMORYREADER_H

//
// Created by allan on 2024/3/17.
//

#include "ProcessMemoryReader.h"

using namespace loguru;

std::map<PVOID, MemoryRegion*>* ProcessMemoryReader::readCommittedRegions(HANDLE hProcess, bool withContent) {
    LPCVOID address = nullptr;
    auto committedRegions = new std::map<PVOID, MemoryRegion*>();
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

        if (!withContent) {
            committedRegions->insert(std::pair<PVOID, MemoryRegion*>(memoryInfo.BaseAddress, new MemoryRegion(memoryInfo.BaseAddress, memoryInfo.RegionSize)));
            continue;
        }

        auto region = new MemoryRegion(memoryInfo.BaseAddress, memoryInfo.RegionSize);
        SIZE_T bytesRead;
        int tries = 0;
        do {
            ReadProcessMemory(hProcess, memoryInfo.BaseAddress, (LPVOID)region->content.data(), memoryInfo.RegionSize, &bytesRead);
            tries += 1;
        } while (tries <= 3 and bytesRead != memoryInfo.RegionSize);
        if (bytesRead != memoryInfo.RegionSize) {
            continue;
        }
        committedRegions->insert(std::pair<PVOID, MemoryRegion*>(memoryInfo.BaseAddress, region));
    }

    return committedRegions;
}

std::vector<byte>* ProcessMemoryReader::readBytes(PVOID address, SIZE_T length, std::map<PVOID, MemoryRegion*>* committedRegions) {
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

std::vector<PVOID>* ProcessMemoryReader::getBaseAddresses(HANDLE hProcess) {
    LPCVOID address = nullptr;
    auto baseAddresses = new std::vector<PVOID>();
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

        baseAddresses->push_back(memoryInfo.BaseAddress);
    }

    return baseAddresses;
}

std::map<PVOID, MemoryRegion *> *
ProcessMemoryReader::readCommittedRegions(HANDLE hProcess, std::map<PVOID, MemoryRegion *> *committedRegionsWOContent, int numThreads) {
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
    for (auto &[address, region]: *committedRegionsWOContent) {
        ioService.post([=] { ProcessMemoryReader::readCommittedRegion(hProcess, region); });
    }
    ioService.stop();
    for (auto &thread: threads) {
        thread->join();
    }
    auto committedRegions = new std::map<PVOID, MemoryRegion*>();
    for (auto &[address, region]: *committedRegionsWOContent) {
        if (region->content.empty()) {
            continue;
        }
        committedRegions->insert(std::pair<PVOID, MemoryRegion*>(address, region));
    }
    return committedRegions;
}

MemoryRegion* ProcessMemoryReader::readCommittedRegion(HANDLE hProcess, MemoryRegion *region) {
    SIZE_T bytesRead;
    int tries = 0;
    do {
        ReadProcessMemory(hProcess, region->baseAddress, (LPVOID)region->content.data(), region->content.size(), &bytesRead);
        tries += 1;
    } while (tries <= 3 and bytesRead != region->content.size());
    if (bytesRead != region->content.size()) {
        region->content = std::vector<byte>();
    }
    return region;
}

std::string*
ProcessMemoryReader::readNullTerminatedAsciiString(PVOID address, std::map<PVOID, MemoryRegion*>* committedRegions, SIZE_T maxLength) {
    auto bytes = ProcessMemoryReader::readBytes(address, maxLength, committedRegions);
    if (bytes == nullptr) {
        return nullptr;
    }
    auto nullTerminatorIndex = bytes->size();
    for (auto i = 0; i < bytes->size(); i++) {
        if (bytes->at(i) == 0) {
            nullTerminatorIndex = i;
            break;
        }
    }
    return new std::string(bytes->begin(), bytes->begin() + nullTerminatorIndex);

}

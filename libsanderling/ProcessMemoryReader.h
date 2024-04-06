//
// Created by allan on 2024/3/17.
//

#pragma once

#include "common.h"

namespace eve {
    using namespace std::literals;
    using namespace boost::placeholders;
    using std::vector, std::map, std::shared_ptr, std::unique_ptr, std::make_shared, std::make_unique, std::string, std::unordered_set, std::function, std::pair, std::make_pair, std::move, std::format;


    struct MemoryRegion {
        MemoryRegion(PVOID baseAddress, std::vector<byte> &content) : baseAddress(baseAddress), content(content) {}

        MemoryRegion(PVOID baseAddress, SIZE_T length) : baseAddress(baseAddress) {
            this->content = std::vector<byte>(length);
        }

        MemoryRegion(PVOID baseAddress, SIZE_T length, byte *content) : baseAddress(baseAddress) {
            if (content != nullptr) {
                this->content = std::vector<byte>(content, content + length);
            }
        }

        PVOID baseAddress = nullptr;
        std::vector<byte> content;
    };

    typedef MemoryRegion MR;
    typedef std::shared_ptr<MR> SPMR;
    typedef const SPMR &CPMR;

    typedef std::map<PVOID, SPMR> MMR;
    typedef std::unique_ptr<MMR> PMMR;
    typedef const PMMR &CPMMR;
    typedef std::vector<byte> BYTES;
    typedef std::unique_ptr<BYTES> PBYTES;
    typedef std::string STR;
    typedef std::unique_ptr<STR> PSTR;


    class ProcessMemoryReader {
    public:
        explicit ProcessMemoryReader(DWORD processId, uint8_t numThreads = 4) : processId(processId),
                                                                                numThreads(numThreads) {
            hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
            if (hProcess == nullptr) {
                LOG_S(ERROR) << "Failed to open process.";
                exit(-1);
            }
            reloadCache();
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
        }

        inline void reloadCache() {
            readCommittedRegionsWoContent();
            readCommittedRegionContents();
        }

        inline PBYTES readCachedBytes(PVOID address, SIZE_T length) const {
            if (committedRegions == nullptr) {
                LOG_S(WARNING) << "No committed regions loaded.";
                return nullptr;
            }
            auto ge = committedRegions->lower_bound(address);
            if (ge == committedRegions->begin()) {
                return nullptr;
            }
            const auto &region = (--ge)->second;

            if (region == nullptr) {
                return nullptr;
            }
            int64_t offset = (LPBYTE) address - (LPBYTE) region->baseAddress;
            if (offset < 0 || length == 0 || (uint64_t) offset >= region->content.size()) {
                return nullptr;
            }
            return make_unique<BYTES>(region->content.begin() + offset,
                                      region->content.begin() + min(offset + length, region->content.size()));
        }

        inline PSTR readCachedNullTerminatedAsciiString(PVOID address, SIZE_T maxLength = 255) const {
            auto bytes = readCachedBytes(address, maxLength);
            if (bytes == nullptr) {
                return nullptr;
            }
            auto nullTerminatorIndex = (int64_t) bytes->size();
            for (auto i = 0; i < bytes->size(); i++) {
                if (bytes->at(i) == 0) {
                    nullTerminatorIndex = i;
                    break;
                }
            }
            return make_unique<STR>(bytes->begin(), bytes->begin() + nullTerminatorIndex);
        }

        template<class T>
        inline unique_ptr<T> readCachedMemory(PVOID address) const {
            return static_cast<unique_ptr<T>>(readCachedBytes(address, sizeof(T)));
        }

        template<class T>
        inline unique_ptr<T[]> readCachedMemory(PVOID address, SIZE_T size) const {
            return static_cast<unique_ptr<T[]>>(readCachedBytes(address, sizeof(T) * size));
        }

        inline unique_ptr<byte[]> readRawBytes(PVOID address, SIZE_T length) const {
            SIZE_T bytesRead;
            auto buffer = make_unique<byte[]>(length);
            int tries = 0;
            do {
                ReadProcessMemory(hProcess, address, (LPVOID) buffer.get(), length, &bytesRead);
                tries += 1;
            } while (tries <= 3 and bytesRead != length);
            if (bytesRead != length) {
                return nullptr;
            }
            return buffer;
        }

        inline PBYTES readBytes(PVOID address, SIZE_T length) const {
            SIZE_T bytesRead;
            auto buffer = make_unique<BYTES>(length);
            int tries = 0;
            do {
                ReadProcessMemory(hProcess, address, (LPVOID) buffer->data(), length, &bytesRead);
                tries += 1;
            } while (tries <= 3 and bytesRead != length);
            if (bytesRead != length) {
                return nullptr;
            }
            return buffer;
        }

        inline PSTR readNullTerminatedAsciiString(PVOID address, SIZE_T maxLength = 255) const {
            auto bytes = readRawBytes(address, maxLength+1);
            if (bytes == nullptr) {
                return nullptr;
            }
            auto nullTerminatorIndex = maxLength;
            for (auto i = 0; i < maxLength; i++) {
                if (bytes[i] == 0) {
                    nullTerminatorIndex = i;
                    break;
                }
            }
            return make_unique<std::string>(bytes.get(), bytes.get() + nullTerminatorIndex);
        }

        template<class T>
        inline unique_ptr<T, std::function<void(T*)>> readMemory(PVOID address) const {
            auto bytes_array = static_cast<T*>(readRawBytes(address, sizeof(T)).release());
            return unique_ptr<T, std::function<void(T*)>>(bytes_array, [](void* ptr) {delete[] static_cast<byte*>(ptr);});
        }

        template<class T>
        inline unique_ptr<T[], std::function<void(T*)>> readMemory(PVOID address, SIZE_T size) const {
            auto bytes_array = reinterpret_cast<T*>(readRawBytes(address, sizeof(T) * size).release());
            return unique_ptr<T, std::function<void(T*)>>(bytes_array, [](void* ptr) {delete[] static_cast<byte*>(ptr);});
        }


    protected:
        DWORD processId = 0;
        uint8_t numThreads = 4;
        HANDLE hProcess = nullptr;
        PMMR committedRegions = nullptr;

    private:
        inline void readCommittedRegionsWoContent() {
            LPCVOID address = nullptr;
            committedRegions = std::make_unique<std::map<PVOID, SPMR>>();
            while (true) {
                MEMORY_BASIC_INFORMATION memoryInfo;
                auto result = VirtualQueryEx(hProcess, address, &memoryInfo, sizeof(memoryInfo));

                if (result != sizeof(memoryInfo)) {
                    break;
                }
                address = (LPBYTE) memoryInfo.BaseAddress + memoryInfo.RegionSize;
                if (memoryInfo.State != MEM_COMMIT || memoryInfo.Protect & PAGE_GUARD ||
                    memoryInfo.Protect & PAGE_NOACCESS) {
                    continue;
                }
                committedRegions->insert(std::pair<PVOID, SPMR>(memoryInfo.BaseAddress,
                                                                std::make_shared<MR>(memoryInfo.BaseAddress,
                                                                                    memoryInfo.RegionSize)));
            }
        }

        static inline void readCommittedRegionContent(const HANDLE &hProcess, CPMR region) {
            SIZE_T bytesRead;
            int tries = 0;
            do {
                ReadProcessMemory(hProcess, region->baseAddress, (LPVOID) region->content.data(),
                                  region->content.size(), &bytesRead);
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
}
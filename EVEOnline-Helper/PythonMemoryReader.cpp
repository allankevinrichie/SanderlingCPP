//
// Created by allan on 2024/3/23.
//

#include "PythonMemoryReader.h"


std::unordered_set<PVOID>* PythonMemoryReader::EnumerateCandidatesForPythonTypesInMemoryRegion(MemoryRegion* region) const {
    if (region == nullptr || region->content.empty()) {
        LOG_S(WARNING) << "No committed regions loaded.";
        return nullptr;
    }
    auto memoryRegionContentAsULongArray = (uint64_t *)region->content.data();
    auto baseAddress = (uint64_t *) region->baseAddress;
    auto longLength = region->content.size() / 8;
    auto candidates = new std::unordered_set<PVOID>();

    for (uint64_t candidateAddressIndex = 0; candidateAddressIndex < longLength - 4; candidateAddressIndex++) {
        auto candidateAddressInProcess = baseAddress + candidateAddressIndex;
        auto candidate_ob_type = (uint64_t *) memoryRegionContentAsULongArray[candidateAddressIndex + 1];
        if (candidate_ob_type != candidateAddressInProcess) {
            continue;
        }
        auto candidate_tp_name = readNullTerminatedAsciiString(
                (PVOID) memoryRegionContentAsULongArray[candidateAddressIndex + 3],
                committedRegions,
                16
        );
        if (candidate_tp_name == nullptr) {
            continue;
        }
        auto equal = *candidate_tp_name == "type";
        if (!equal) {
            continue;
        }
        candidates->insert(candidateAddressInProcess);
    }
    return candidates;
}

std::unordered_set<PVOID> *PythonMemoryReader::EnumerateCandidatesForPythonTypes() {
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
    auto regionList = new std::unordered_set<PVOID>*[committedRegions->size()];
    for (auto [it, i] = std::tuple{committedRegions->begin(), 0}; it != committedRegions->end(); it++, i++) {
        auto region = it->second;
        ioService.post([=, *this] {
            auto candidates = PythonMemoryReader::EnumerateCandidatesForPythonTypesInMemoryRegion(region);
            regionList[i] = candidates;
        });
    }
    ioService.stop();
    for (auto &thread: threads) {
        thread->join();
    }
    auto allCandidates = new std::unordered_set<PVOID>();
    for (auto i = 0; i < committedRegions->size(); i++) {
        auto regionCandidates = regionList[i];
        if (regionCandidates == nullptr || regionCandidates->empty()) {
            continue;
        }
        allCandidates->insert_range(*regionCandidates);
    }
    pythonTypes = allCandidates;
    return allCandidates;
}

std::unordered_set<PVOID> *PythonMemoryReader::EnumerateCandidatesForPythonObjects(
        const function<bool(uint64_t*)>& ob_type_filter,
        const function<bool(string*)>& tp_name_filter,
        const function<std::map<PVOID, MemoryRegion*>*(std::map<PVOID, MemoryRegion*>*)>& region_filter
) {
    boost::asio::io_service ioService;
    boost::asio::io_service::work work(ioService);

    auto committedRegions = region_filter(this->committedRegions);
    // Create a thread pool
    std::vector<boost::shared_ptr<boost::thread>> threads;
    for (int i = 0; i < numThreads; ++i) {
        boost::shared_ptr<boost::thread> thread(new boost::thread(
                [ObjectPtr = &ioService] { return ObjectPtr->run(); }
        ));
        threads.push_back(thread);
    }
    auto regionList = new std::unordered_set<PVOID>*[committedRegions->size()];
    for (auto [it, i] = std::tuple{committedRegions->begin(), 0}; it != committedRegions->end(); it++, i++) {
        auto region = it->second;
        ioService.post([=, *this] {
            auto candidates = PythonMemoryReader::EnumerateCandidatesForPythonObjectsInMemoryRegion(region, ob_type_filter, tp_name_filter);
            regionList[i] = candidates;
        });
    }
    ioService.stop();
    for (auto &thread: threads) {
        thread->join();
    }
    auto allCandidates = new std::unordered_set<PVOID>();
    for (auto i = 0; i < committedRegions->size(); i++) {
        auto regionCandidates = regionList[i];
        if (regionCandidates == nullptr || regionCandidates->empty()) {
            continue;
        }
        allCandidates->insert_range(*regionCandidates);
    }
    return allCandidates;
}

std::unordered_set<PVOID> *PythonMemoryReader::EnumerateCandidatesForPythonObjectsInMemoryRegion(
        MemoryRegion *region,
        const function<bool(uint64_t*)>& ob_type_filter,
        const function<bool(string*)>& tp_name_filter
) const {
    if (region == nullptr || region->content.empty()) {
        LOG_S(WARNING) << "No committed regions loaded.";
        return nullptr;
    }
    auto memoryRegionContentAsULongArray = (uint64_t *)region->content.data();
    auto baseAddress = (uint64_t *) region->baseAddress;
    auto longLength = region->content.size() / 8;
    auto candidates = new std::unordered_set<PVOID>();

    for (uint64_t candidateAddressIndex = 0; candidateAddressIndex < longLength - 4; candidateAddressIndex++) {
        auto candidateAddressInProcess = baseAddress + candidateAddressIndex;
        auto candidate_ob_type = (uint64_t *) memoryRegionContentAsULongArray[candidateAddressIndex + 1];
        if (!ob_type_filter(candidate_ob_type)) {
            continue;
        }
        auto candidate_tp_name = readNullTerminatedAsciiString(
                (PVOID) memoryRegionContentAsULongArray[candidateAddressIndex + 3],
                committedRegions,
                16
        );
        if (!tp_name_filter(candidate_tp_name)) {
            continue;
        }
        candidates->insert(candidateAddressInProcess);
    }
    return candidates;
}


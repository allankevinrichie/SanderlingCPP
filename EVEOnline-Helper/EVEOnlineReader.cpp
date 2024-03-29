//
// Created by allan on 2024/3/23.
//

#include <iostream>
#include "EVEOnlineReader.h"

//std::unordered_set<PVOID> *EVEOnlineReader::EnumerateCandidatesForPythonUIRootInMemoryRegion(MemoryRegion *region) const {
//    if (region == nullptr || region->content.empty()) {
//        LOG_S(WARNING) << "No committed regions loaded.";
//        return nullptr;
//    }
//    auto memoryRegionContentAsULongArray = (uint64_t *)region->content.data();
//    auto baseAddress = (uint64_t *) region->baseAddress;
//    auto longLength = region->content.size() / 8;
//    auto candidates = new std::unordered_set<PVOID>();
//
//    for (uint64_t candidateAddressIndex = 0; candidateAddressIndex < longLength - 4; candidateAddressIndex++) {
//        auto candidateAddressInProcess = baseAddress + candidateAddressIndex;
//        auto candidate_ob_type = (uint64_t *) memoryRegionContentAsULongArray[candidateAddressIndex + 1];
//        if (!pythonTypes->contains(candidate_ob_type)) {
//            continue;
//        }
//        auto candidate_tp_name = readCachedNullTerminatedAsciiString(
//                (PVOID) memoryRegionContentAsULongArray[candidateAddressIndex + 3],
//                committedRegions,
//                16
//        );
//        if (candidate_tp_name == nullptr) {
//            continue;
//        }
//        auto equal = *candidate_tp_name == "UIRoot";
//        if (!equal) {
//            continue;
//        }
//        candidates->insert(candidateAddressInProcess);
//    }
//    return candidates;
//}

//std::unordered_set<PVOID> *EVEOnlineReader::EnumerateCandidatesForPythonUIRoot() {
//    boost::asio::io_service ioService;
//    boost::asio::io_service::work work(ioService);
//
//    if (pythonTypes == nullptr || pythonTypes->empty()) {
//        LOG_S(WARNING) << "No python types loaded.";
//        return nullptr;
//    }
//
//    // Create a thread pool
//    std::vector<boost::shared_ptr<boost::thread>> threads;
//    for (int i = 0; i < numThreads; ++i) {
//        boost::shared_ptr<boost::thread> thread(new boost::thread(
//                [ObjectPtr = &ioService] { return ObjectPtr->run(); }
//        ));
//        threads.push_back(thread);
//    }
//    auto regionList = new std::unordered_set<PVOID>*[committedRegions->size()];
//    for (auto [it, i] = std::tuple{committedRegions->begin(), 0}; it != committedRegions->end(); it++, i++) {
//        auto region = it->second;
//        ioService.post([=, *this] {
//            auto candidates = EVEOnlineReader::EnumerateCandidatesForPythonUIRootInMemoryRegion(region);
//            regionList[i] = candidates;
//        });
//    }
//    ioService.stop();
//    for (auto &thread: threads) {
//        thread->join();
//    }
//    auto allCandidates = new std::unordered_set<PVOID>();
//    for (auto i = 0; i < committedRegions->size(); i++) {
//        auto regionCandidates = regionList[i];
//        if (regionCandidates == nullptr || regionCandidates->empty()) {
//            continue;
//        }
//        allCandidates->insert_range(*regionCandidates);
//    }
//    pythonUIRootTypes = allCandidates;
//    LOG_S(INFO) << std::format("{} python UIRoot found.", allCandidates->size());
//    return allCandidates;
//}

//std::unordered_set<PVOID> *EVEOnlineReader::EnumerateCandidatesForPythonUIRootObject() {
//    boost::asio::io_service ioService;
//    boost::asio::io_service::work work(ioService);
//
//    if (pythonUIRootTypes == nullptr || pythonUIRootTypes->empty()) {
//        LOG_S(WARNING) << "No python UIRoot loaded.";
//        return nullptr;
//    }
//
//    // Create a thread pool
//    std::vector<boost::shared_ptr<boost::thread>> threads;
//    for (int i = 0; i < numThreads; ++i) {
//        boost::shared_ptr<boost::thread> thread(new boost::thread(
//                [ObjectPtr = &ioService] { return ObjectPtr->run(); }
//        ));
//        threads.push_back(thread);
//    }
//    auto regionList = new std::unordered_set<PVOID>*[committedRegions->size()];
//    for (auto [it, i] = std::tuple{committedRegions->begin(), 0}; it != committedRegions->end(); it++, i++) {
//        auto region = it->second;
//        ioService.post([=, *this] {
//            auto candidates = EVEOnlineReader::EnumerateCandidatesForPythonUIRootObjectInMemoryRegion(region);
//            regionList[i] = candidates;
//        });
//    }
//    ioService.stop();
//    for (auto &thread: threads) {
//        thread->join();
//    }
//    auto allCandidates = new std::unordered_set<PVOID>();
//    for (auto i = 0; i < committedRegions->size(); i++) {
//        auto regionCandidates = regionList[i];
//        if (regionCandidates == nullptr || regionCandidates->empty()) {
//            continue;
//        }
//        allCandidates->insert_range(*regionCandidates);
//    }
//    pythonUIRootObjects = allCandidates;
//    LOG_S(INFO) << std::format("{} python UIRoot Objects found.", allCandidates->size());
//    return allCandidates;
//}
//
//std::unordered_set<PVOID> *
//EVEOnlineReader::EnumerateCandidatesForPythonUIRootObjectInMemoryRegion(MemoryRegion *region) const {
//    if (region == nullptr || region->content.empty()) {
//        LOG_S(WARNING) << "No committed regions loaded.";
//        return nullptr;
//    }
//    auto memoryRegionContentAsULongArray = (uint64_t *)region->content.data();
//    auto baseAddress = (uint64_t *) region->baseAddress;
//    auto longLength = region->content.size() / 8;
//    auto candidates = new std::unordered_set<PVOID>();
//
//    for (uint64_t candidateAddressIndex = 0; candidateAddressIndex < longLength - 4; candidateAddressIndex++) {
//        auto candidateAddressInProcess = baseAddress + candidateAddressIndex;
//        auto candidate_ob_type = (uint64_t *) memoryRegionContentAsULongArray[candidateAddressIndex + 1];
//        if (!pythonUIRootTypes->contains(candidate_ob_type)) {
//            continue;
//        }
//        candidates->insert(candidateAddressInProcess);
//    }
//    return candidates;
//}

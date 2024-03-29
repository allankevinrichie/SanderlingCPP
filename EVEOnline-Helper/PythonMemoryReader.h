//
// Created by allan on 2024/3/23.
//

#ifndef EVEONLINE_HELPER_PYTHONMEMORYREADER_H
#define EVEONLINE_HELPER_PYTHONMEMORYREADER_H

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
#include <unordered_set>

#include "ProcessMemoryReader.h"

using namespace boost::placeholders;
using namespace std;


namespace py27 {
    struct PyTypeObject;

    struct PyObject {
        uint64_t ob_refcnt;
        PyTypeObject* ob_type;
    };

    struct PyVarObject {
        uint64_t ob_refcnt;
        PyTypeObject* ob_type;
        uint64_t ob_size;
    };

    struct PyTypeObject {
        PyVarObject ob_base;
        PyObject *tp_name;
    };

    struct PyStrObject {
        PyVarObject ob_base;
        uint64_t ob_shash;
        uint64_t ob_sstate;
        char ob_sval[1];
    };

    struct PyFloatObject {
        PyObject ob_base;
        double ob_fval;
    };

    struct PyIntObject {
        PyObject ob_base;
        long ob_ival;
    };

    struct PyDictEntry {
        uint64_t me_hash;
        PyObject *me_key;
        PyObject *me_value;
    };

    struct PyDictObject {
        PyObject ob_base;
        uint64_t ma_fill;
        uint64_t ma_used;
        uint64_t ma_mask;
        PyDictEntry *ma_table;
        PyDictEntry *(*ma_lookup)(PyDictObject *mp, PyObject *key, long hash);
        PyDictEntry ma_smalltable[8];
    };
}


class PythonMemoryReader : public ProcessMemoryReader {
public:
    explicit PythonMemoryReader(DWORD processId, uint8_t numThreads = 4) :  ProcessMemoryReader(processId, numThreads) {
        EnumerateCandidatesForPythonTypes();
        LOG_S(INFO) << std::format("{} python type types found.", pythonTypes->size());
        EnumeratePythonBuiltinTypeAddresses();
    }

    ~PythonMemoryReader() {

            delete pythonTypes;
            delete builtinTypeRegions;

    }

    inline bool isPyTypeObject(PVOID nativeObjectAddress) const {
        auto* pyObjectPtr = (py27::PyObject*)nativeObjectAddress;
        return pythonTypes->contains(pyObjectPtr->ob_type);
    }

    inline const string* getPythonTypeObjectName(PVOID nativeObjectAddress) const {
        if (nativeObjectAddress == nullptr) {
            return nullptr;
        }
        auto* pyObjectPtr = (py27::PyObject*)nativeObjectAddress;
        auto ob_type = pyObjectPtr->ob_type;
        if (pythonBuiltinTypesMapping.contains(ob_type)) {
            return &pythonBuiltinTypesMapping.at(ob_type);
        }
        if (isPyTypeObject(nativeObjectAddress)) {
            auto pyTypeObject = (py27::PyTypeObject*)nativeObjectAddress;
            return readCachedNullTerminatedAsciiString(pyTypeObject->tp_name, 255);
        }
        return nullptr;
    }

    inline std::unordered_set<PVOID>* EnumerateCandidatesForPythonObjects(
            const function<bool(uint64_t*)>& ob_type_filter,
            const function<bool(string*)>& tp_name_filter,
            const function<std::map<PVOID, MemoryRegion*>*(std::map<PVOID, MemoryRegion*>*)>& region_filter=[](std::map<PVOID, MemoryRegion*>* regions){return regions;}
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
            ioService.post([=, &regionList, this] {
                auto candidates = EnumerateCandidatesForPythonObjectsInMemoryRegion(region, ob_type_filter, tp_name_filter);
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

    template<class T> auto readPythonObject(PVOID objectAddress) {
        auto pyObject = readCachedMemory<py27::PyObject>(objectAddress);
        if (pyObject == nullptr) {
            return nullptr;
        }
        if (pythonBuiltinTypesMapping.contains(pyObject->ob_type)) {
            auto typeName = pythonBuiltinTypesMapping.at(pyObject->ob_type);
        }
        auto PyObjectType = readCachedMemory<py27::PyTypeObject>(pyObject->ob_type);
        auto PyTypeName = getPythonTypeObjectName(PyObjectType);
        if (PyTypeName == nullptr) {
            return nullptr;
        }

    }

    template<class KT, class VT> inline std::map<KT, VT> * readPythonDict(PVOID dictObjectAddress) {
        auto* dictObject = (py27::PyDictObject*)dictObjectAddress;
        auto dict = new std::map<KT, VT>();

        auto numberOfSlots = (SIZE_T)dictObject->ma_mask + 1;
        if (numberOfSlots < 0 || 10000 < numberOfSlots)
        {
            //  Avoid stalling the whole reading process when a single dictionary contains garbage.
            return nullptr;
        }
        auto slotsMemory = readCachedMemory<py27::PyDictEntry>(dictObject->ma_table, numberOfSlots);
        if (slotsMemory == nullptr) {
            return nullptr;
        }
        for (auto slotIndex = 0; slotIndex < numberOfSlots; ++slotIndex) {
            auto slot = slotsMemory[slotIndex];
            if (slot.me_key == nullptr) {
                continue;
            }
            auto key = (KT)slot.me_key;
            auto value = (VT)slot.me_value;
            dict->insert(std::pair<KT, VT>(key, value));
        }
        for (uint64_t i = 0; i < dictObject->ma_used; i++) {
            auto entry = dictObject->ma_table[i];
            auto key = (KT)entry.me_key;
            auto value = (VT)entry.me_value;
            dict->insert(std::pair<KT, VT>(key, value));
        }
        return dict;
    }

protected:
    std::map<PVOID, MemoryRegion*>* builtinTypeRegions = nullptr;
    std::unordered_set<PVOID>* pythonTypes = nullptr;
    std::map<PVOID, string> pythonBuiltinTypesMapping = {};
    static constexpr std::array builtinTypeNames = {"str"sv, "float"sv, "dict"sv, "int"sv, "unicode"sv, "long"sv, "list"sv, "tuple"sv, "bool"sv, "set"sv};

private:
    inline std::unordered_set<PVOID> * EnumerateCandidatesForPythonTypesInMemoryRegion(MemoryRegion* region) const {
        if (region == nullptr || region->content.empty()) {
            // LOG_S(WARNING) << "No committed regions loaded.";
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
            auto candidate_tp_name = readCachedNullTerminatedAsciiString(
                    (PVOID) memoryRegionContentAsULongArray[candidateAddressIndex + 3],
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

    inline std::unordered_set<PVOID> * EnumerateCandidatesForPythonObjectsInMemoryRegion(
            MemoryRegion* region,
            const function<bool(uint64_t*)>& ob_type_filter,
            const function<bool(string*)>& tp_name_filter
    ) const {
        if (region == nullptr || region->content.empty()) {
            //LOG_S(WARNING) << "No committed regions loaded.";
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
            auto candidate_tp_name = readCachedNullTerminatedAsciiString(
                    (PVOID) memoryRegionContentAsULongArray[candidateAddressIndex + 3],
                    16
            );
            if (!tp_name_filter(candidate_tp_name)) {
                continue;
            }
            candidates->insert(candidateAddressInProcess);
        }
        return candidates;
    }

    void EnumeratePythonBuiltinTypeAddresses() {
        bool allFound = false;
        int tries = 0;
        while (!allFound) {
            for (auto const &type: builtinTypeNames) {
                auto candidates = EnumerateCandidatesForPythonObjects(
                        [this](uint64_t* ob_type) {
                            return pythonTypes->contains(ob_type);
                        },
                        [&](string* tp_name) {
                            return tp_name != nullptr and *tp_name == type;
                        },
                        [this](std::map<PVOID, MemoryRegion*>* regions) {
                            if (builtinTypeRegions != nullptr) {
                                return builtinTypeRegions;
                            }
                            return committedRegions;
                        }
                );
                if (candidates != nullptr && candidates->size() == 1) {
                    pythonBuiltinTypesMapping[*candidates->begin()] = type;
                    setBuiltinTypeRegions(*candidates->begin());
                    LOG_S(INFO) << std::format("builtin python type `{}` found @ 0x{:X}", type, (uint64_t)*candidates->begin());
                }
            }
            allFound = pythonBuiltinTypesMapping.size() == builtinTypeNames.size();
            if (!allFound) {
                tries += 1;
                if (tries > 3) {
                    LOG_S(ERROR) << "Failed to find all builtin python types.";
                    exit(-1);
                }
            }
        }

    }

    inline std::unordered_set<PVOID>* EnumerateCandidatesForPythonTypes() {
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
            ioService.post([=, this] {
                auto candidates = EnumerateCandidatesForPythonTypesInMemoryRegion(region);
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

    void inline setBuiltinTypeRegions(PVOID anyBuiltinTypeAddr) {
        if (this -> builtinTypeRegions != nullptr) {
            return;
        }
        auto builtinTypeRegionsFiltered = new std::map<PVOID, MemoryRegion*>;
        const uint64_t builtinTypeAddrMask = 0xFFFFFFFFFF000000;
        uint64_t builtinTypeAddrMin = 0;
        uint64_t builtinTypeAddrMax = 0x7FFFFFFFFF000000;

        builtinTypeAddrMin = (uint64_t )anyBuiltinTypeAddr & builtinTypeAddrMask;
        builtinTypeAddrMax = builtinTypeAddrMin + ~builtinTypeAddrMask;
        for (auto &[_, region] : *committedRegions) {
            if ((uint64_t )region->baseAddress >= builtinTypeAddrMax || (uint64_t )region->baseAddress + region->content.size() <= builtinTypeAddrMin) {
                continue;
            }
            builtinTypeRegionsFiltered->insert(std::pair<PVOID, MemoryRegion*>(region->baseAddress, region));
        }
        this -> builtinTypeRegions = builtinTypeRegionsFiltered;
        LOG_S(INFO) << std::format("builtin type regions located @ 0x{:X} - 0x{:X}", builtinTypeAddrMin, builtinTypeAddrMax);
    }
};

//template<class PyType> class ForeignPyType {
//public:
//    ForeignPyType(PyType* &foreignObjectAddress, const PythonMemoryReader& reader) : foreignObjectAddress(foreignObjectAddress) {}
//
//    inline const string* getPythonTypeObjectName() const {
//        if (foreignObjectAddress == nullptr) {
//            return nullptr;
//        }
//        auto ob_type = foreignObjectAddress->ob_type;
//        if (pythonBuiltinTypesMapping.contains(ob_type)) {
//            return &pythonBuiltinTypesMapping.at(ob_type);
//        }
//        if (isPyTypeObject(foreignObjectAddress)) {
//            auto pyTypeObject = (py27::PyTypeObject*)foreignObjectAddress;
//            return readCachedNullTerminatedAsciiString(pyTypeObject->tp_name, 255);
//        }
//        return nullptr;
//    }
//
//
//    PyType* foreignObjectAddress;
//};

#endif //EVEONLINE_HELPER_PYTHONMEMORYREADER_H

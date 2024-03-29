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
    explicit inline PythonMemoryReader(DWORD processId, uint8_t numThreads = 4) : processId(processId), numThreads(numThreads) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
        committedRegions = ProcessMemoryReader::readCommittedRegions(
                hProcess,
                ProcessMemoryReader::readCommittedRegions(hProcess, false),
                numThreads
        );
        LOG_S(INFO) << std::format("{} committed regions loaded.", committedRegions->size());
        EnumerateCandidatesForPythonTypes();
        LOG_S(INFO) << std::format("{} python type types found.", pythonTypes->size());
        EnumeratePythonBuildinTypeAddresses();
    }

    [[nodiscard]] DWORD getProcessId() const {
        return processId;
    }

    [[nodiscard]] HANDLE getProcessHandle() const {
        return hProcess;
    }

    [[nodiscard]] std::map<PVOID, MemoryRegion*>* getCommittedRegions() const {
        return committedRegions;
    }

    std::unordered_set<PVOID>* EnumerateCandidatesForPythonObjects(
            const function<bool(uint64_t*)>& ob_type_filter,
            const function<bool(string*)>& tp_name_filter,
            const function<std::map<PVOID, MemoryRegion*>*(std::map<PVOID, MemoryRegion*>*)>& region_filter=[](std::map<PVOID, MemoryRegion*>* regions){return regions;}
    );

    template<class T> inline T* readCachedMemory(PVOID address) const {
        return readBytes(address, sizeof(T), committedRegions);
    }

    template<class T> inline T* readCachedMemory(PVOID address, SIZE_T size) const {
        return readBytes(address, sizeof(T) * size, committedRegions);
    }

    inline PVOID readCachedMemory(PVOID address, SIZE_T size) const {
        return readBytes(address, size, committedRegions);
    }

    template<class T> auto readPythonObject(PVOID objectAddress) {
        auto pyObject = readCachedMemory<py27::PyObject>(objectAddress);
        if (pyObject == nullptr) {
            return nullptr;
        }
        if (pythonBuildinTypesMapping.contains(pyObject->ob_type)) {
            auto typeName = pythonBuildinTypesMapping.at(pyObject->ob_type);
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


//    static string ReadPythonStringValue(ulong stringObjectAddress, IMemoryReader memoryReader, int maxLength);

protected:
    DWORD processId = 0;
    uint8_t numThreads = 4;
    HANDLE hProcess = nullptr;
    std::map<PVOID, MemoryRegion*>* committedRegions = nullptr;
    std::map<PVOID, MemoryRegion*>* buildinTypeRegions = nullptr;
    std::unordered_set<PVOID>* pythonTypes = nullptr;
    std::map<PVOID, string> pythonBuildinTypesMapping = {};
    static constexpr std::array buildinTypeNames = {"str"sv, "float"sv, "dict"sv, "int"sv, "unicode"sv, "long"sv, "list"sv, "tuple"sv, "bool"sv, "set"sv};

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
        if (pythonBuildinTypesMapping.contains(ob_type)) {
            return &pythonBuildinTypesMapping.at(ob_type);
        }
        if (isPyTypeObject(nativeObjectAddress)) {
            auto pyTypeObject = (py27::PyTypeObject*)nativeObjectAddress;
            return readNullTerminatedAsciiString(pyTypeObject->tp_name, committedRegions, 255);
        }
        return nullptr;
    }

    std::unordered_set<PVOID>* EnumerateCandidatesForPythonTypes();
    std::unordered_set<PVOID> * EnumerateCandidatesForPythonTypesInMemoryRegion(MemoryRegion* region) const;

    inline std::unordered_set<PVOID> * EnumerateCandidatesForPythonObjectsInMemoryRegion(
            MemoryRegion* region,
            const function<bool(uint64_t*)>& ob_type_filter,
            const function<bool(string*)>& tp_name_filter
    ) const;

    void inline setBuildinTypeRegions(PVOID anyBuildTypeAddr) {
        if (this -> buildinTypeRegions != nullptr) {
            return;
        }
        auto buildinTypeRegionsFiltered = new std::map<PVOID, MemoryRegion*>;
        const uint64_t buildinTypeAddrMask = 0xFFFFFFFFFF000000;
        uint64_t buildinTypeAddrMin = 0;
        uint64_t buildinTypeAddrMax = 0x7FFFFFFFFF000000;

        buildinTypeAddrMin = (uint64_t )anyBuildTypeAddr & buildinTypeAddrMask;
        buildinTypeAddrMax = buildinTypeAddrMin + ~buildinTypeAddrMask;
        for (auto &[_, region] : *committedRegions) {
            if ((uint64_t )region->baseAddress >= buildinTypeAddrMax || (uint64_t )region->baseAddress + region->content.size() <= buildinTypeAddrMin) {
                continue;
            }
            buildinTypeRegionsFiltered->insert(std::pair<PVOID, MemoryRegion*>(region->baseAddress, region));
        }
        this -> buildinTypeRegions = buildinTypeRegionsFiltered;
        LOG_S(INFO) << std::format("buildin type regions located @ 0x{:X} - 0x{:X}", buildinTypeAddrMin, buildinTypeAddrMax);
    }

    void EnumeratePythonBuildinTypeAddresses() {
        bool allFound = false;
        int tries = 0;
        while (!allFound) {
            for (auto const &type: buildinTypeNames) {
                auto candidates = EnumerateCandidatesForPythonObjects(
                        [this](uint64_t* ob_type) {
                            return pythonTypes->contains(ob_type);
                        },
                        [&](string* tp_name) {
                            return tp_name != nullptr and *tp_name == type;
                        },
                        [*this](std::map<PVOID, MemoryRegion*>* regions) {
                            if (buildinTypeRegions != nullptr) {
                                return buildinTypeRegions;
                            }
                            return committedRegions;
                        }
                );
                if (candidates != nullptr && candidates->size() == 1) {
                    pythonBuildinTypesMapping[*candidates->begin()] = type;
                    setBuildinTypeRegions(*candidates->begin());
                    LOG_S(INFO) << std::format("buildin python type `{}` found @ 0x{:X}", type, (uint64_t)*candidates->begin());
                }
            }
            allFound = pythonBuildinTypesMapping.size() == buildinTypeNames.size();
            if (!allFound) {
                tries += 1;
                if (tries > 3) {
                    LOG_S(ERROR) << "Failed to find all buildin python types.";
                    exit(-1);
                }
            }
        }

    }

};

#endif //EVEONLINE_HELPER_PYTHONMEMORYREADER_H

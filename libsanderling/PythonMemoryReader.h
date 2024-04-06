//
// Created by allan on 2024/3/23.
//
#pragma once

#include "ProcessMemoryReader.h"

namespace eve {

    using namespace std::literals;
    using namespace boost::placeholders;
    using std::vector, std::map, std::shared_ptr, std::unique_ptr, std::make_shared, std::make_unique, std::string, std::unordered_set, std::function, std::pair, std::make_pair, std::move, std::format;

    typedef std::unordered_set<PVOID> USP;
    typedef std::unique_ptr<USP> PUSP;


    namespace py27 {
        struct PyTypeObject;

        struct PyObject {
            uint64_t ob_refcnt;
            PyTypeObject *ob_type;
        };

        struct PyVarObject {
            uint64_t ob_refcnt;
            PyTypeObject *ob_type;
            uint64_t ob_size;
        };

        struct PyTypeObject {
            PyVarObject ob_base;
            char *tp_name;
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
        explicit PythonMemoryReader(DWORD processId, uint8_t numThreads = 4) : ProcessMemoryReader(processId,
                                                                                                   numThreads) {
            EnumerateCandidatesForPythonTypes();
            LOG_S(INFO) << std::format("{} python type types found.", pythonTypes->size());
            EnumeratePythonBuiltinTypeAddresses();
        }

        ~PythonMemoryReader() = default;

        template<class PyObject> friend class ForeignPyObject;

        inline bool isPyTypeObject(PVOID nativeObjectAddress) const {
            auto *pyObjectPtr = (py27::PyObject *) nativeObjectAddress;
            return pythonTypes->contains(pyObjectPtr->ob_type);
        }

        inline PSTR getPyObjectTypeName(PVOID foreignObjectAddress) const {
            if (foreignObjectAddress == nullptr) {
                return nullptr;
            }
            auto pyObjectPtr = readMemory<py27::PyObject>(foreignObjectAddress);
            auto ob_type = pyObjectPtr->ob_type;
            if (pythonBuiltinTypesMapping.contains(ob_type)) {
                return make_unique<STR>(pythonBuiltinTypesMapping.at(ob_type));
            }
            if (isPyTypeObject(foreignObjectAddress)) {
                auto pyTypeObject = (py27::PyTypeObject *) foreignObjectAddress;
                return readCachedNullTerminatedAsciiString(pyTypeObject->tp_name, 255);
            }
            return nullptr;
        }

        inline PSTR getPythonTypeObjectName(PVOID nativeObjectAddress) const {
            if (nativeObjectAddress == nullptr) {
                return nullptr;
            }
            auto *pyObjectPtr = (py27::PyObject *) nativeObjectAddress;
            auto ob_type = pyObjectPtr->ob_type;
            if (pythonBuiltinTypesMapping.contains(ob_type)) {
                return make_unique<STR>(pythonBuiltinTypesMapping.at(ob_type));
            }
            if (isPyTypeObject(nativeObjectAddress)) {
                auto pyTypeObject = (py27::PyTypeObject *) nativeObjectAddress;
                return readCachedNullTerminatedAsciiString(pyTypeObject->tp_name, 255);
            }
            return nullptr;
        }

        inline PUSP EnumerateCandidatesForPythonObjects( // NOLINT(*-no-recursion)
                const function<bool(uint64_t *)> &ob_type_filter,
                const function<bool(PSTR const&)> &tp_name_filter
        ) {
            return std::move(EnumerateCandidatesForPythonObjects(ob_type_filter, tp_name_filter, committedRegions));
        }

        inline PUSP EnumerateCandidatesForPythonObjects( // NOLINT(*-no-recursion)
                const function<bool(uint64_t *)> &ob_type_filter,
                const function<bool(PSTR const&)> &tp_name_filter,
                CPMMR filteredRegions
        ) {
            if (filteredRegions == nullptr || filteredRegions->empty()) {
                return std::move(EnumerateCandidatesForPythonObjects(ob_type_filter, tp_name_filter));
            }

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
            auto regionList = new std::unordered_set<PVOID> *[filteredRegions->size()];
            for (auto [it, i] = std::tuple{filteredRegions->begin(), 0}; it != filteredRegions->end(); it++, i++) {
                auto &region = it->second;
                ioService.post([=, &region, &regionList, this] {
                    auto candidates = EnumerateCandidatesForPythonObjectsInMemoryRegion(region, ob_type_filter,
                                                                                        tp_name_filter);
                    regionList[i] = candidates;
                });
            }
            ioService.stop();
            for (auto &thread: threads) {
                thread->join();
            }
            auto allCandidates = std::make_unique<USP>();
            for (auto i = 0; i < filteredRegions->size(); i++) {
                auto regionCandidates = regionList[i];
                if (regionCandidates == nullptr || regionCandidates->empty()) {
                    continue;
                }
                allCandidates->insert_range(*regionCandidates);
            }
            return allCandidates;
        }

        template<class T>
        auto readPythonObject(PVOID objectAddress) {
//            auto pyObject = readCachedMemory<py27::PyObject>(objectAddress);
//            if (pyObject == nullptr) {
//                return nullptr;
//            }
//            if (pythonBuiltinTypesMapping.contains(pyObject->ob_type)) {
//                auto typeName = pythonBuiltinTypesMapping.at(pyObject->ob_type);
//            }
//            auto PyObjectType = readCachedMemory<py27::PyTypeObject>(pyObject->ob_type);
//            auto PyTypeName = getPythonTypeObjectName(PyObjectType);
//            if (PyTypeName == nullptr) {
//                return nullptr;
//            }

        }

        template<class KT, class VT>
        inline std::map<KT, VT> *readPythonDict(PVOID dictObjectAddress) {
            auto *dictObject = (py27::PyDictObject *) dictObjectAddress;
            auto dict = new std::map<KT, VT>();

            auto numberOfSlots = (SIZE_T) dictObject->ma_mask + 1;
            if (numberOfSlots < 0 || 10000 < numberOfSlots) {
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
                auto key = (KT) slot.me_key;
                auto value = (VT) slot.me_value;
                dict->insert(std::pair<KT, VT>(key, value));
            }
            for (uint64_t i = 0; i < dictObject->ma_used; i++) {
                auto entry = dictObject->ma_table[i];
                auto key = (KT) entry.me_key;
                auto value = (VT) entry.me_value;
                dict->insert(std::pair<KT, VT>(key, value));
            }
            return dict;
        }

    protected:
        PMMR builtinTypeRegions = nullptr;
        PUSP pythonTypes = nullptr;
        std::map<PVOID, string> pythonBuiltinTypesMapping = {};
        std::map<PVOID, string> pythonUserDefinedTypesMapping = {};
        std::shared_mutex pythonUserDefinedTypesMappingMutex;
        static constexpr std::array builtinTypeNames = {"str"sv, "float"sv, "dict"sv, "int"sv, "unicode"sv, "long"sv,
                                                        "list"sv, "tuple"sv, "bool"sv, "set"sv, "NoneType"sv};

    private:
        [[nodiscard]] inline std::unordered_set<PVOID> *
        EnumerateCandidatesForPythonTypesInMemoryRegion(CPMR region) const {
            if (region == nullptr || region->content.empty()) {
                // LOG_S(WARNING) << "No committed regions loaded.";
                return nullptr;
            }
            auto memoryRegionContentAsULongArray = (uint64_t *) region->content.data();
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

        inline std::unordered_set<PVOID> *EnumerateCandidatesForPythonObjectsInMemoryRegion(
                CPMR &region,
                const function<bool(uint64_t *)> &ob_type_filter,
                const function<bool(PSTR const&)> &tp_name_filter
        ) const {
            if (region == nullptr || region->content.empty()) {
                //LOG_S(WARNING) << "No committed regions loaded.";
                return nullptr;
            }
            auto memoryRegionContentAsULongArray = (uint64_t *) region->content.data();
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

        inline void EnumeratePythonBuiltinTypeAddresses() {
            bool allFound = false;
            int tries = 0;
            while (!allFound) {
                for (auto const &type: builtinTypeNames) {
                    auto candidates = EnumerateCandidatesForPythonObjects(
                            [this](uint64_t *ob_type) {
                                return pythonTypes->contains(ob_type);
                            },
                            [&type](PSTR const &tp_name) {
                                return tp_name != nullptr and *tp_name == type;
                            },
                            builtinTypeRegions
                    );
                    if (candidates != nullptr && candidates->size() == 1) {
                        pythonBuiltinTypesMapping[*candidates->begin()] = type;
                        setBuiltinTypeRegions(*candidates->begin());
                        LOG_S(INFO) << std::format("builtin python type `{}` found @ 0x{:X}", type,
                                                   (uint64_t) *candidates->begin());
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

        inline void EnumerateCandidatesForPythonTypes() {
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
            auto regionList = new std::unordered_set<PVOID> *[committedRegions->size()];
            for (auto [it, i] = std::tuple{committedRegions->begin(), 0}; it != committedRegions->end(); it++, i++) {
                auto &region = it->second;
                ioService.post([=, &region, this] {
                    auto candidates = EnumerateCandidatesForPythonTypesInMemoryRegion(region);
                    regionList[i] = candidates;
                });
            }
            ioService.stop();
            for (auto &thread: threads) {
                thread->join();
            }
            auto allCandidates = std::make_unique<USP>();
            for (auto i = 0; i < committedRegions->size(); i++) {
                auto regionCandidates = regionList[i];
                if (regionCandidates == nullptr || regionCandidates->empty()) {
                    continue;
                }
                allCandidates->insert_range(*regionCandidates);
            }
            pythonTypes = std::move(allCandidates);
        }

        inline void setBuiltinTypeRegions(PVOID anyBuiltinTypeAddr) {
            if (this->builtinTypeRegions != nullptr) {
                return;
            }
            auto builtinTypeRegionsFiltered = make_unique<MMR>();
            const uint64_t builtinTypeAddrMask = 0xFFFFFFFFFF000000;
            uint64_t builtinTypeAddrMin = 0;
            uint64_t builtinTypeAddrMax = 0x7FFFFFFFFF000000;

            builtinTypeAddrMin = (uint64_t) anyBuiltinTypeAddr & builtinTypeAddrMask;
            builtinTypeAddrMax = builtinTypeAddrMin + ~builtinTypeAddrMask;
            for (auto &[_, region]: *committedRegions) {
                if ((uint64_t) region->baseAddress >= builtinTypeAddrMax ||
                    (uint64_t) region->baseAddress + region->content.size() <= builtinTypeAddrMin) {
                    continue;
                }
                builtinTypeRegionsFiltered->insert(std::pair<PVOID, SPMR>(region->baseAddress, region));
            }
            this->builtinTypeRegions = std::move(builtinTypeRegionsFiltered);
            LOG_S(INFO)
            << std::format("builtin type regions located @ 0x{:X} - 0x{:X}", builtinTypeAddrMin, builtinTypeAddrMax);
        }
    };

//    template<class PyObject> class ForeignPyObject {
//    public:
//        ForeignPyObject(PyObject* foreignObjectAddress, const PythonMemoryReader& reader) : foreignObjectAddress(foreignObjectAddress), reader(reader){
//
//        }
//        inline bool isAlive() const {
//            // check
//        }
//
//    private:
//        string typeName = ""s;
//        PyObject* foreignObjectAddress;
//        const PythonMemoryReader& reader;
//        PyObject nativeObject;
//        inline bool pyObjectTraverse() {
//            if (!typeName.empty()) {
//                return false;
//            }
//            auto py_object = reader.readMemory<py27::PyObject>(foreignObjectAddress);
//            if (py_object == nullptr) {
//                return false;
//            }
//            if (py_object->ob_type == nullptr) {
//                return false;
//            }
//            auto ob_type = py_object->ob_type;
//            if (reader.pythonBuiltinTypesMapping.contains(ob_type)) {
//                typeName = reader.pythonBuiltinTypesMapping.at(ob_type);
//                if (typeName == "str") {
//                    return pyBuiltinStrTraverse();
//                } else if (typeName == "float") {
//                    return pyBuiltinFloatTraverse();
//                } else if (typeName == "int") {
//                    return pyBuiltinIntTraverse();
//                } else if (typeName == "dict") {
//                    return pyBuiltinDictTraverse();
//                } else if (typeName == "list") {
//                    return pyBuiltinListTraverse();
//                } else if (typeName == "tuple") {
//                    return pyBuiltinTupleTraverse();
//                } else if (typeName == "set") {
//                    return pyBuiltinSetTraverse();
//                } else if (typeName == "bool") {
//                    return pyBuiltinBoolTraverse();
//                } else if (typeName == "unicode") {
//                    return pyBuiltinUnicodeTraverse();
//                } else if (typeName == "long") {
//                    return pyBuiltinLongTraverse();
//                } else if (typeName == "NoneType") {
//                    return pyBuiltinNoneTypeTraverse();
//                }
//            }
//            else {
//                std::shared_lock lock(reader.pythonUserDefinedTypesMappingMutex);
//                auto type_exists = reader.pythonUserDefinedTypesMapping.contains(ob_type);
//                auto type_verified = type_exists && !reader.pythonUserDefinedTypesMapping.at(ob_type).empty();
//
//                if (reader.pythonUserDefinedTypesMapping.contains(ob_type)) {
//                    typeName = reader.pythonUserDefinedTypesMapping.at(ob_type);
//                    return pyUserDefinedObjectTraverse();
//                }
//                auto pyTypeObject = reader.readMemory<py27::PyTypeObject>(ob_type);
//                if (pyTypeObject == nullptr) {
//                    return false;
//                }
//                auto tp_name = reader.readCachedNullTerminatedAsciiString(pyTypeObject->tp_name, 255);
//                if (tp_name == nullptr) {
//                    return false;
//                }
//                typeName = *tp_name;
//                {
//                    std::unique_lock lock(reader.pythonUserDefinedTypesMappingMutex);
//                    reader.pythonUserDefinedTypesMapping[ob_type] = typeName;
//                }
//                return pyUserDefinedObjectTraverse();
//            }
//        }
//    };


//template<class PyType> class ForeignPyObject {
//public:
//    ForeignPyObject(PyType* &foreignObjectAddress, const PythonMemoryReader& reader) : foreignObjectAddress(foreignObjectAddress) {}
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
//private:
//    PyType* foreignObjectAddress;
//};

}
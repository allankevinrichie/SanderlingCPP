//
// Created by allan on 2024/3/23.
//

#ifndef EVEONLINE_HELPER_EVEONLINEREADER_H
#define EVEONLINE_HELPER_EVEONLINEREADER_H


#include <vector>
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
#include "PythonMemoryReader.h"

using namespace boost::placeholders;

class EVEOnlineReader : public PythonMemoryReader {
public:
    explicit EVEOnlineReader(DWORD processId, uint8_t numThreads = 4) : PythonMemoryReader(processId, numThreads) {
        EnumerateCandidatesForPythonUIRoot();
        if (pythonUIRootTypes != nullptr && pythonUIRootTypes->size() == 1) {
            eveTypesMapping[*pythonUIRootTypes->begin()] = "UIRoot";
            setEVEObjectRegions(*pythonUIRootTypes->begin());
            LOG_S(INFO) << std::format("eve UIRoot type found @ 0x{:X}", (uint64_t)*pythonUIRootTypes->begin());
        }
        EnumerateCandidatesForPythonUIRootObject();
    }



    inline void EnumerateCandidatesForPythonUIRoot() {
        pythonUIRootTypes = std::move(EnumerateCandidatesForPythonObjects(
                [this](uint64_t* ob_type) {
                    return pythonTypes->contains(ob_type);
                },
                [](std::string* tp_name) {
                    return tp_name != nullptr and *tp_name == "UIRoot";
                },
                eveObjectRegions
        ));
        LOG_S(INFO) << std::format("{} python UIRoot Types found.", pythonUIRootTypes->size());
    }

    inline void EnumerateCandidatesForPythonUIRootObject() {
        pythonUIRootObjects = std::move(EnumerateCandidatesForPythonObjects(
                [this](uint64_t* ob_type) {
                    return pythonUIRootTypes->contains(ob_type);
                },
                [](std::string* tp_name) {
                    return true;
                },
                eveObjectRegions
        ));
        LOG_S(INFO) << std::format("{} python UIRoot Objects found.", pythonUIRootObjects->size());
        for (auto addr : *pythonUIRootObjects) {
            LOG_S(INFO) << std::format("0x{:016X}", (uint64_t)addr);
        }
    }


private:
    PUSP pythonUIRootTypes = nullptr;
    PUSP pythonUIRootObjects = nullptr;
    PMMR eveObjectRegions = nullptr;
    std::map<PVOID, string> eveTypesMapping = {};

    const std::unordered_set<std::string> DictEntriesOfInterestKeys = {
            "_top", "_left", "_width", "_height", "_displayX", "_displayY",
            "_displayHeight", "_displayWidth",
            "_name", "_text", "_setText",
            "children",
            "texturePath", "_bgTexturePath",
            "_hint", "_display",

            //  HPGauges
            "lastShield", "lastArmor", "lastStructure",

            //  Found in "ShipHudSpriteGauge"
            "_lastValue",

            //  Found in "ModuleButton"
            "ramp_active",

            //  Found in the Transforms contained in "ShipModuleButtonRamps"
            "_rotation",

            //  Found under OverviewEntry in Sprite named "iconSprite"
            "_color",

            //  Found in "SE_TextlineCore"
            "_sr",

            //  Found in "_sr" Bunch
            "htmlstr",

            // 2023-01-03 Sample with PhotonUI: process-sample-ebdfff96e7.zip
            "_texturePath", "_opacity", "_bgColor", "isExpanded"
        };



    inline void setEVEObjectRegions(PVOID UIRootAddr) {
        if (this -> eveObjectRegions != nullptr) {
            return;
        }
        auto eveRegionsFiltered = make_unique<MMR>();
        const auto eveTypeAddrMask = 0xFFFFFFFC00000000;
        uint64_t eveObjectAddrMin = 0;
        uint64_t eveObjectAddrMax = 0x7FFFFFFFFF000000;

        eveObjectAddrMin = (uint64_t )UIRootAddr & eveTypeAddrMask;
        eveObjectAddrMax = eveObjectAddrMin + ~eveTypeAddrMask;
        for (auto &[_, region] : *committedRegions) {
            if ((uint64_t )region->baseAddress >= eveObjectAddrMax || (uint64_t )region->baseAddress + region->content.size() <= eveObjectAddrMin) {
                continue;
            }
            eveRegionsFiltered->insert(std::pair<PVOID, PMR>(region->baseAddress, region));
        }
        this -> eveObjectRegions = std::move(eveRegionsFiltered);
        LOG_S(INFO) << std::format("eve type regions located @ 0x{:X} - 0x{:X}", eveObjectAddrMin, eveObjectAddrMax);
    }

//    static std::map<std::string, std::function<uint64_t, LocalMemoryReadingTools, object>> specializedReadingFromPythonType =
//    ImmutableDictionary<string, Func<ulong, LocalMemoryReadingTools, object>>.Empty
//    .Add("str", new Func<ulong, LocalMemoryReadingTools, object>(ReadingFromPythonType_str))
//    .Add("unicode", new Func<ulong, LocalMemoryReadingTools, object>(ReadingFromPythonType_unicode))
//    .Add("int", new Func<ulong, LocalMemoryReadingTools, object>(ReadingFromPythonType_int))
//    .Add("bool", new Func<ulong, LocalMemoryReadingTools, object>(ReadingFromPythonType_bool))
//    .Add("float", new Func<ulong, LocalMemoryReadingTools, object>(ReadingFromPythonType_float))
//    .Add("PyColor", new Func<ulong, LocalMemoryReadingTools, object>(ReadingFromPythonType_PyColor))
//    .Add("Bunch", new Func<ulong, LocalMemoryReadingTools, object>(ReadingFromPythonType_Bunch));
};


#endif //EVEONLINE_HELPER_EVEONLINEREADER_H

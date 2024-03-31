// libsanderling.cpp: 定义应用程序的入口点。
//
#include "test.h"

using namespace loguru;

int main(int argc, char* argv[])
{
    loguru::g_preamble_header = false;
    loguru::g_internal_verbosity = Verbosity_MAX;
    loguru::g_stderr_verbosity = Verbosity_INFO;
    loguru::g_colorlogtostderr = false;
    loguru::init(argc, argv);
    DWORD processId = 29020;
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    auto reader = new eve::EVEOnlineReader(processId, 16);
//    auto uiRootTypes = reader->EnumerateCandidatesForPythonUIRoot();
//    auto uiRoot = reader->EnumerateCandidatesForPythonUIRootObject();
////    auto addresses = ProcessMemoryReader::getBaseAddresses(hProcess);
//    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
//    LOG_S(INFO) << std::format("{} regions found, {:.5f}s elaplsed.", uiRoot->size(), (double)std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() / 1000);
	return 0;
}

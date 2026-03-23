#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
// 每个诊断用例描述一组运行参数，用于隔离排查 cvtColor 崩溃路径。
struct DiagnosticCase
{
    std::string id;
    std::string description;
    std::string cpuDisable;
    bool disableOptimized = false;
    bool setSingleThread = false;
};

std::wstring utf8ToWide(const std::string &value)
{
    if (value.empty()) {
        return {};
    }

    const int size =
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        throw std::runtime_error("Failed to convert UTF-8 to wide string");
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string wideToUtf8(const std::wstring &value)
{
    if (value.empty()) {
        return {};
    }

    const int size =
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("Failed to convert wide string to UTF-8");
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

std::wstring quoted(const std::wstring &value)
{
    return L"\"" + value + L"\"";
}

bool runBgrToGray()
{
    cv::Mat image(960, 1280, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::rectangle(image, cv::Rect(300, 260, 180, 140), cv::Scalar(0, 0, 0), cv::FILLED);

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return gray.type() == CV_8UC1 && gray.channels() == 1 && gray.cols == image.cols && gray.rows == image.rows;
}

bool runBgraToGray()
{
    cv::Mat image(720, 1280, CV_8UC4, cv::Scalar(255, 255, 255, 255));
    cv::rectangle(image, cv::Rect(220, 180, 150, 120), cv::Scalar(0, 0, 0, 255), cv::FILLED);

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    return gray.type() == CV_8UC1 && gray.channels() == 1 && gray.cols == image.cols && gray.rows == image.rows;
}

bool runBgrToRgb()
{
    cv::Mat image(1, 1, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::Mat converted;
    cv::cvtColor(image, converted, cv::COLOR_BGR2RGB);

    if (converted.type() != CV_8UC3 || converted.channels() != 3) {
        return false;
    }

    const cv::Vec3b pixel = converted.at<cv::Vec3b>(0, 0);
    return pixel[0] == 30 && pixel[1] == 20 && pixel[2] == 10;
}

bool runCaseById(const std::string &caseId)
{
    if (caseId == "bgr2gray") {
        return runBgrToGray();
    }

    if (caseId == "bgra2gray") {
        return runBgraToGray();
    }

    if (caseId == "bgr2rgb") {
        return runBgrToRgb();
    }

    throw std::runtime_error("Unknown diagnostic case: " + caseId);
}

std::wstring currentExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = 0;

    while (true) {
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            throw std::runtime_error("GetModuleFileNameW failed");
        }

        if (size < buffer.size() - 1) {
            buffer.resize(size);
            return buffer;
        }

        buffer.resize(buffer.size() * 2);
    }
}

struct ProcessResult
{
    DWORD exitCode = 0;
    bool started = false;
    std::string output;
};

ProcessResult runChildProcess(
    const std::wstring &programPath,
    const DiagnosticCase &diagnosticCase)
{
    // 子进程启动前注入 CPU 特性开关，便于对比不同优化路径行为。
    const char *previousCpuDisable = std::getenv("OPENCV_CPU_DISABLE");
    const std::string previousCpuDisableValue = previousCpuDisable != nullptr ? previousCpuDisable : "";

    if (!diagnosticCase.cpuDisable.empty()) {
        _putenv_s("OPENCV_CPU_DISABLE", diagnosticCase.cpuDisable.c_str());
    } else {
        _putenv_s("OPENCV_CPU_DISABLE", "");
    }

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
        throw std::runtime_error("CreatePipe failed");
    }

    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        throw std::runtime_error("SetHandleInformation failed");
    }

    std::wstring commandLine = quoted(programPath) + L" --child " + utf8ToWide(diagnosticCase.id);
    if (diagnosticCase.disableOptimized) {
        commandLine += L" --disable-optimized";
    }
    if (diagnosticCase.setSingleThread) {
        commandLine += L" --single-thread";
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION processInfo{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    const std::wstring workingDirectory = std::filesystem::path(programPath).parent_path().wstring();
    const BOOL started = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        workingDirectory.c_str(),
        &startupInfo,
        &processInfo);

    CloseHandle(writePipe);

    ProcessResult result;
    result.started = started == TRUE;

    if (started) {
        // 通过独立进程执行单个用例，避免一次崩溃中断整批诊断。
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        GetExitCodeProcess(processInfo.hProcess, &result.exitCode);

        char buffer[4096];
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
            result.output.append(buffer, buffer + bytesRead);
        }

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }

    CloseHandle(readPipe);

    if (!previousCpuDisableValue.empty()) {
        _putenv_s("OPENCV_CPU_DISABLE", previousCpuDisableValue.c_str());
    } else {
        _putenv_s("OPENCV_CPU_DISABLE", "");
    }

    return result;
}

int runChild(int argc, char *argv[])
{
    if (argc < 3) {
        std::cerr << "child mode requires a case id\n";
        return 2;
    }

    const std::string caseId = argv[2];
    bool disableOptimized = false;
    bool setSingleThread = false;
    for (int i = 3; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--disable-optimized") {
            disableOptimized = true;
        } else if (argument == "--single-thread") {
            setSingleThread = true;
        }
    }

    if (disableOptimized) {
        cv::setUseOptimized(false);
    }

    if (setSingleThread) {
        cv::setNumThreads(1);
    }

    std::cout << "child case=" << caseId
              << " optimized=" << (cv::useOptimized() ? "true" : "false")
              << " threads=" << cv::getNumThreads()
              << " cpu_features=" << cv::getCPUFeaturesLine() << "\n";

    const bool ok = runCaseById(caseId);
    std::cout << "child result=" << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}

int runParent()
{
    std::cout << "OpenCV version=" << CV_VERSION << "\n";
    std::cout << "cpu_features=" << cv::getCPUFeaturesLine() << "\n";
    std::cout << "default_optimized=" << (cv::useOptimized() ? "true" : "false") << "\n";
    std::cout << "default_threads=" << cv::getNumThreads() << "\n";

    const char *cpuDisable = std::getenv("OPENCV_CPU_DISABLE");
    std::cout << "env.OPENCV_CPU_DISABLE=" << (cpuDisable != nullptr ? cpuDisable : "") << "\n\n";

    const std::vector<DiagnosticCase> cases = {
        {"bgr2gray", "BGR -> Gray (default)"},
        {"bgra2gray", "BGRA -> Gray (default)"},
        {"bgr2rgb", "BGR -> RGB (default)"},
        {"bgr2gray", "BGR -> Gray (disable AVX2)", "AVX2"},
        {"bgr2gray", "BGR -> Gray (disable optimized)", "", true},
        {"bgr2gray", "BGR -> Gray (single thread)", "", false, true},
    };

    const std::wstring programPath = currentExecutablePath();
    int failedCount = 0;

    for (const DiagnosticCase &diagnosticCase : cases) {
        std::cout << "=== " << diagnosticCase.description << " ===\n";
        // 父进程只负责调度并收集结果，具体 OpenCV 调用在子进程执行。
        const ProcessResult result = runChildProcess(programPath, diagnosticCase);
        if (!result.started) {
            std::cout << "parent status=START_FAILED\n\n";
            ++failedCount;
            continue;
        }

        std::cout << result.output;
        const bool crashed = result.exitCode >= 0xC0000000u;
        std::cout << "parent status=" << (crashed ? "CRASH" : "EXIT")
                  << " exitCode=" << result.exitCode << "\n\n";
        if (crashed || result.exitCode != 0) {
            ++failedCount;
        }
    }

    std::cout << "summary failed=" << failedCount << " total=" << cases.size() << "\n";
    return failedCount == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char *argv[])
{
    try {
        if (argc >= 2 && std::string(argv[1]) == "--child") {
            return runChild(argc, argv);
        }

        return runParent();
    } catch (const cv::Exception &exception) {
        std::cerr << "OpenCV exception: " << exception.what() << "\n";
        return 3;
    } catch (const std::exception &exception) {
        std::cerr << "std::exception: " << exception.what() << "\n";
        return 4;
    } catch (...) {
        std::cerr << "unknown exception\n";
        return 5;
    }
}

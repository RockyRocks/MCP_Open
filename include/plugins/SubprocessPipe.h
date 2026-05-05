#pragma once
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

class SubprocessPipe {
public:
    static std::unique_ptr<SubprocessPipe> Spawn(
        const std::string& command, const std::vector<std::string>& args);

    bool WriteLine(const std::string& line);
    bool ReadLine(std::string& line, int timeoutMs);
    bool IsRunning() const;
    void Kill();

    ~SubprocessPipe();

    SubprocessPipe(const SubprocessPipe&) = delete;
    SubprocessPipe& operator=(const SubprocessPipe&) = delete;

private:
    SubprocessPipe() = default;

#ifdef _WIN32
    HANDLE m_Process     = INVALID_HANDLE_VALUE;
    HANDLE m_ChildStdinW = INVALID_HANDLE_VALUE;
    HANDLE m_ChildStdoutR = INVALID_HANDLE_VALUE;
#else
    pid_t m_Pid        = -1;
    int   m_StdinFd    = -1;
    int   m_StdoutFd   = -1;
#endif
};

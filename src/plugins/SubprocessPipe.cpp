#include <plugins/SubprocessPipe.h>
#include <core/Logger.h>
#include <stdexcept>

#ifdef _WIN32
// Windows: CreateProcess with redirected stdin/stdout pipes

std::unique_ptr<SubprocessPipe> SubprocessPipe::Spawn(
    const std::string& command, const std::vector<std::string>& args)
{
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdinR  = INVALID_HANDLE_VALUE;
    HANDLE childStdinW  = INVALID_HANDLE_VALUE;
    HANDLE childStdoutR = INVALID_HANDLE_VALUE;
    HANDLE childStdoutW = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&childStdinR, &childStdinW, &sa, 0))
        throw std::runtime_error("SubprocessPipe: CreatePipe stdin failed");
    if (!CreatePipe(&childStdoutR, &childStdoutW, &sa, 0)) {
        CloseHandle(childStdinR);
        CloseHandle(childStdinW);
        throw std::runtime_error("SubprocessPipe: CreatePipe stdout failed");
    }

    SetHandleInformation(childStdinW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(childStdoutR, HANDLE_FLAG_INHERIT, 0);

    std::string cmdLine = "\"" + command + "\"";
    for (const auto& a : args)
        cmdLine += " \"" + a + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = childStdinR;
    si.hStdOutput = childStdoutW;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        nullptr,
        const_cast<char*>(cmdLine.c_str()),
        nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    CloseHandle(childStdinR);
    CloseHandle(childStdoutW);

    if (!ok) {
        CloseHandle(childStdinW);
        CloseHandle(childStdoutR);
        throw std::runtime_error(
            "SubprocessPipe: CreateProcess failed for: " + command);
    }

    CloseHandle(pi.hThread);

    auto pipe = std::unique_ptr<SubprocessPipe>(new SubprocessPipe());
    pipe->m_Process      = pi.hProcess;
    pipe->m_ChildStdinW  = childStdinW;
    pipe->m_ChildStdoutR = childStdoutR;
    return pipe;
}

bool SubprocessPipe::WriteLine(const std::string& line) {
    std::string data = line + "\n";
    DWORD written = 0;
    return WriteFile(m_ChildStdinW, data.c_str(),
                     static_cast<DWORD>(data.size()), &written, nullptr) != 0;
}

bool SubprocessPipe::ReadLine(std::string& line, int timeoutMs) {
    line.clear();

    DWORD deadline = GetTickCount() + static_cast<DWORD>(timeoutMs);
    char ch;
    DWORD bytesRead = 0;

    while (true) {
        DWORD remaining = deadline - GetTickCount();
        if (static_cast<int>(remaining) <= 0)
            return false;

        DWORD avail = 0;
        if (!PeekNamedPipe(m_ChildStdoutR, nullptr, 0, nullptr, &avail, nullptr))
            return false;

        if (avail == 0) {
            Sleep(1);
            continue;
        }

        if (!ReadFile(m_ChildStdoutR, &ch, 1, &bytesRead, nullptr) || bytesRead == 0)
            return false;

        if (ch == '\n')
            return true;
        if (ch != '\r')
            line += ch;
    }
}

bool SubprocessPipe::IsRunning() const {
    if (m_Process == INVALID_HANDLE_VALUE) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(m_Process, &code)) return false;
    return code == STILL_ACTIVE;
}

void SubprocessPipe::Kill() {
    if (m_Process != INVALID_HANDLE_VALUE && IsRunning())
        TerminateProcess(m_Process, 1);
}

SubprocessPipe::~SubprocessPipe() {
    Kill();
    if (m_ChildStdinW != INVALID_HANDLE_VALUE) CloseHandle(m_ChildStdinW);
    if (m_ChildStdoutR != INVALID_HANDLE_VALUE) CloseHandle(m_ChildStdoutR);
    if (m_Process != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(m_Process, 3000);
        CloseHandle(m_Process);
    }
}

#else
// POSIX: fork + execvp with pipe fds

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>

std::unique_ptr<SubprocessPipe> SubprocessPipe::Spawn(
    const std::string& command, const std::vector<std::string>& args)
{
    int stdinPipe[2];
    int stdoutPipe[2];

    if (pipe(stdinPipe) != 0)
        throw std::runtime_error("SubprocessPipe: pipe(stdin) failed");
    if (pipe(stdoutPipe) != 0) {
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        throw std::runtime_error("SubprocessPipe: pipe(stdout) failed");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdinPipe[0]); close(stdinPipe[1]);
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        throw std::runtime_error("SubprocessPipe: fork failed");
    }

    if (pid == 0) {
        // Child
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdinPipe[0]); close(stdinPipe[1]);
        close(stdoutPipe[0]); close(stdoutPipe[1]);

        std::vector<const char*> argv;
        argv.push_back(command.c_str());
        for (const auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);

        execvp(command.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    // Parent
    close(stdinPipe[0]);
    close(stdoutPipe[1]);

    auto p = std::unique_ptr<SubprocessPipe>(new SubprocessPipe());
    p->m_Pid      = pid;
    p->m_StdinFd   = stdinPipe[1];
    p->m_StdoutFd  = stdoutPipe[0];
    return p;
}

bool SubprocessPipe::WriteLine(const std::string& line) {
    std::string data = line + "\n";
    ssize_t total = 0;
    while (total < static_cast<ssize_t>(data.size())) {
        ssize_t n = write(m_StdinFd, data.c_str() + total,
                          data.size() - static_cast<size_t>(total));
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

bool SubprocessPipe::ReadLine(std::string& line, int timeoutMs) {
    line.clear();
    char ch;
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeoutMs);

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;

        int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());

        struct pollfd pfd{};
        pfd.fd = m_StdoutFd;
        pfd.events = POLLIN;

        int pr = poll(&pfd, 1, remaining);
        if (pr <= 0) return false;

        ssize_t n = read(m_StdoutFd, &ch, 1);
        if (n <= 0) return false;

        if (ch == '\n') return true;
        if (ch != '\r') line += ch;
    }
}

bool SubprocessPipe::IsRunning() const {
    if (m_Pid <= 0) return false;
    int status = 0;
    pid_t r = waitpid(m_Pid, &status, WNOHANG);
    return r == 0;
}

void SubprocessPipe::Kill() {
    if (m_Pid > 0 && IsRunning()) {
        kill(m_Pid, SIGTERM);
        usleep(100000);
        if (IsRunning()) kill(m_Pid, SIGKILL);
        int status;
        waitpid(m_Pid, &status, 0);
    }
}

SubprocessPipe::~SubprocessPipe() {
    Kill();
    if (m_StdinFd >= 0)  close(m_StdinFd);
    if (m_StdoutFd >= 0) close(m_StdoutFd);
}

#endif

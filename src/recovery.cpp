#include "recovery.h"
#include "pathutil.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static constexpr const wchar_t *RESTART_FLAG   = L"/restored";
static constexpr const wchar_t *RECOVERY_GLOB  = L"nanopad-recovery-*.recovery";
static constexpr const wchar_t *MAGIC          = L"NANOPAD-RECOVERY 1";
static constexpr const wchar_t *BODY_SEPARATOR = L"\n@@BODY@@\n";

// Reject absurdly large snapshots so a corrupt file can't make us allocate wildly.
static constexpr ULONGLONG MAX_RECOVERY_BYTES = 256ULL * 1024 * 1024;

void Recovery::RegisterForRestart()
{
    // Relaunch with RESTART_FLAG after a crash, hang, patch, or update/reboot.
    // Pass no exclusion flags: in particular we must NOT set RESTART_NO_REBOOT,
    // which would prevent Windows from reopening Nanopad on the next sign-in
    // after a restart -- the main case where users want their unsaved text back.
    // Relaunch on reboot also depends on the user's "Restart apps" sign-in
    // setting being enabled, and Windows imposes a grace period (~60s) before
    // reopening apps after login.
    RegisterApplicationRestart(RESTART_FLAG, 0);
}

bool Recovery::LaunchedByRestart(const wchar_t *cmdLine)
{
    return cmdLine && wcsstr(cmdLine, RESTART_FLAG) != nullptr;
}

void Recovery::OwnPath(wchar_t (&out)[MAX_PATH])
{
    wchar_t name[64];
    swprintf_s(name, _countof(name), L"nanopad-recovery-%lu.recovery", GetCurrentProcessId());
    PathUtil::GetPortableFilePath(name, out);
}

void Recovery::OwnTempPath(wchar_t (&out)[MAX_PATH])
{
    wchar_t name[64];
    swprintf_s(name, _countof(name), L"nanopad-recovery-%lu.recovery.tmp", GetCurrentProcessId());
    PathUtil::GetPortableFilePath(name, out);
}

void Recovery::DirPath(wchar_t (&out)[MAX_PATH])
{
    // Yields "<exe dir>\" (the empty filename leaves a trailing backslash).
    PathUtil::GetPortableFilePath(L"", out);
}

// True if pid belongs to a currently running Nanopad process (this exe). Such a
// snapshot is owned by a live instance and must not be treated as an orphan.
bool Recovery::IsLiveSibling(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if(!hProc)
        return false; // can't open -> process is gone (or not ours): treat as orphan

    wchar_t image[MAX_PATH] = {};
    DWORD len               = MAX_PATH;
    bool live               = false;
    if(QueryFullProcessImageNameW(hProc, 0, image, &len))
    {
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        live = (_wcsicmp(image, self) == 0);
    }
    CloseHandle(hProc);
    return live;
}

void Recovery::Save(const std::wstring &text, const FileInfo &info)
{
    std::wstring buf;
    buf.reserve(text.size() + 256);
    buf += MAGIC;
    buf += L"\n";
    buf += L"Path=";
    buf += info.filePath;
    buf += L"\n";
    buf += L"Encoding=";
    buf += std::to_wstring((int)info.encoding);
    buf += L"\n";
    buf += L"LineEnding=";
    buf += std::to_wstring((int)info.lineEnding);
    buf += BODY_SEPARATOR;
    buf += text;

    // Atomic write: fill a temp file, then rename it over the target so a crash
    // mid-write can never leave a half-written snapshot in place.
    wchar_t tmp[MAX_PATH];
    OwnTempPath(tmp);

    HANDLE hFile = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(hFile == INVALID_HANDLE_VALUE)
        return;

    const unsigned char bom[2] = {0xFF, 0xFE}; // UTF-16LE
    DWORD written;
    WriteFile(hFile, bom, sizeof(bom), &written, nullptr);
    WriteFile(hFile, buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(hFile);

    wchar_t path[MAX_PATH];
    OwnPath(path);
    MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

void Recovery::EnumerateOrphans(std::vector<std::wstring> &outPaths)
{
    outPaths.clear();

    wchar_t dir[MAX_PATH];
    DirPath(dir);

    wchar_t pattern[MAX_PATH];
    swprintf_s(pattern, _countof(pattern), L"%s%s", dir, RECOVERY_GLOB);

    DWORD myPid = GetCurrentProcessId();

    // Collect orphan candidates (snapshots whose owning process is gone), oldest
    // first, so repeated launches drain them in a stable order.
    struct Candidate
    {
        ULONGLONG writeTime;
        std::wstring path;
    };
    std::vector<Candidate> candidates;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if(hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            DWORD pid = 0;
            if(swscanf_s(fd.cFileName, L"nanopad-recovery-%lu.recovery", &pid) != 1)
                continue;
            if(pid == myPid || IsLiveSibling(pid))
                continue;

            ULONGLONG t =
                ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | (ULONGLONG)fd.ftLastWriteTime.dwLowDateTime;
            candidates.push_back({t, std::wstring(dir) + fd.cFileName});
        } while(FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.writeTime < b.writeTime; });

    for(const Candidate &c : candidates)
        outPaths.push_back(c.path);
}

bool Recovery::HasOrphans()
{
    std::vector<std::wstring> orphans;
    EnumerateOrphans(orphans);
    return !orphans.empty();
}

void Recovery::LaunchRecoveryInstance()
{
    wchar_t exe[MAX_PATH];
    if(!GetModuleFileNameW(nullptr, exe, MAX_PATH))
        return;

    std::wstring cmd = L"\"";
    cmd += exe;
    cmd += L"\" /recover";

    STARTUPINFOW si        = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if(CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

bool Recovery::ClaimOrphan(std::wstring &outText, FileInfo &outInfo)
{
    std::vector<std::wstring> orphans;
    EnumerateOrphans(orphans);

    wchar_t dir[MAX_PATH];
    DirPath(dir);
    DWORD myPid = GetCurrentProcessId();

    // Two instances may scan at the same time (e.g. Windows reopening apps after
    // a reboot). Claim by atomically renaming the orphan to a name this process
    // owns -- the first renamer wins, a loser's rename fails because the source
    // is already gone, and it moves on to the next candidate. This guarantees
    // each orphan is recovered exactly once.
    for(const std::wstring &orphan : orphans)
    {
        wchar_t claimed[MAX_PATH];
        swprintf_s(claimed, _countof(claimed), L"%snanopad-recovery-%lu.claimed", dir, myPid);

        if(!MoveFileExW(orphan.c_str(), claimed, 0))
            continue; // lost the race for this one, or it vanished -- try the next

        bool ok = LoadFile(claimed, outText, outInfo);

        // Consume the claimed snapshot whether or not it parsed -- a corrupt
        // snapshot must not block the next launch.
        DeleteFileW(claimed);
        DeleteFileW((orphan + L".tmp").c_str());
        return ok;
    }

    return false;
}

bool Recovery::LoadFile(const wchar_t *path, std::wstring &outText, FileInfo &outInfo)
{
    HANDLE hFile =
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size;
    if(!GetFileSizeEx(hFile, &size) || size.QuadPart < 2 || (ULONGLONG)size.QuadPart > MAX_RECOVERY_BYTES)
    {
        CloseHandle(hFile);
        return false;
    }

    DWORD bytes = (DWORD)size.QuadPart;
    auto raw    = std::make_unique<char[]>(bytes);
    DWORD read  = 0;
    BOOL ok     = ::ReadFile(hFile, raw.get(), bytes, &read, nullptr);
    CloseHandle(hFile);
    if(!ok || read < 2)
        return false;

    // Expect a UTF-16LE BOM.
    if((unsigned char)raw[0] != 0xFF || (unsigned char)raw[1] != 0xFE)
        return false;

    size_t wcount = (read - 2) / sizeof(wchar_t);
    std::wstring content(reinterpret_cast<const wchar_t *>(raw.get() + 2), wcount);

    if(content.compare(0, wcslen(MAGIC), MAGIC) != 0)
        return false;

    size_t bodyPos = content.find(BODY_SEPARATOR);
    if(bodyPos == std::wstring::npos)
        return false;

    std::wstring header = content.substr(0, bodyPos);
    outText             = content.substr(bodyPos + wcslen(BODY_SEPARATOR));

    outInfo            = FileInfo{};
    outInfo.encoding   = Encoding::UTF8;
    outInfo.lineEnding = LineEnding::CRLF;

    // Parse header key=value lines.
    size_t lineStart = 0;
    while(lineStart < header.size())
    {
        size_t nl       = header.find(L'\n', lineStart);
        size_t lineEnd  = (nl == std::wstring::npos) ? header.size() : nl;
        std::wstring ln = header.substr(lineStart, lineEnd - lineStart);
        lineStart       = (nl == std::wstring::npos) ? header.size() : nl + 1;

        size_t eq = ln.find(L'=');
        if(eq == std::wstring::npos)
            continue;
        std::wstring key = ln.substr(0, eq);
        std::wstring val = ln.substr(eq + 1);

        if(key == L"Path")
            outInfo.filePath = val;
        else if(key == L"Encoding")
            outInfo.encoding = (Encoding)_wtoi(val.c_str());
        else if(key == L"LineEnding")
            outInfo.lineEnding = (LineEnding)_wtoi(val.c_str());
    }

    return true;
}

void Recovery::Clear()
{
    wchar_t path[MAX_PATH];
    OwnPath(path);
    DeleteFileW(path);

    wchar_t tmp[MAX_PATH];
    OwnTempPath(tmp);
    DeleteFileW(tmp);
}

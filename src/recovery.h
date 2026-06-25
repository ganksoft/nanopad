#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "file_io.h"

// Crash / shutdown autosave and restore for unsaved buffers.
//
// Two independent safety nets cooperate here:
//   1. RegisterApplicationRestart asks Windows to relaunch Nanopad after a
//      Windows Update reboot, a crash, or a hang. Windows passes a known flag
//      back on the command line so we can tell it was a restart.
//   2. An on-disk snapshot (written on shutdown and on an idle timer) covers
//      raw power loss and hard crashes where no relaunch happens at all.
//
// Each instance owns its own snapshot file, named with its process id
// (nanopad-recovery-<pid>.recovery), so multiple running instances never
// clobber each other. On startup an instance scans for snapshots whose owning
// process is no longer a live Nanopad -- those are orphans left by a crash, and
// are offered for recovery. Snapshots live next to the exe, matching the
// portable storage convention used for nanopad.ini.
class Recovery
{
  public:
    // Call once at startup, before the main window is created. Registers the
    // app for automatic restart after an update / crash / hang.
    static void RegisterForRestart();

    // True if this process was relaunched by Windows' restart mechanism.
    static bool LaunchedByRestart(const wchar_t *cmdLine);

    // Write this process's unsaved buffer to its own recovery file. Cheap and
    // safe to call from WM_QUERYENDSESSION and from an idle timer. The write is
    // atomic (temp file then rename) so a crash mid-write never corrupts a good
    // snapshot.
    static void Save(const std::wstring &text, const FileInfo &info);

    // Look for a recovery snapshot left orphaned by a crashed instance (one
    // whose owning process is no longer a live Nanopad). If found, load it,
    // delete it from disk, and return true. outInfo.filePath is the path the
    // buffer belonged to (empty for an untitled document). Claiming is atomic
    // (the file is renamed first), so concurrent instances each take a distinct
    // orphan -- one is claimed per call; relaunching claims the next.
    static bool ClaimOrphan(std::wstring &outText, FileInfo &outInfo);

    // True if at least one orphaned snapshot (owned by a dead process) is still
    // waiting to be recovered.
    static bool HasOrphans();

    // Spawn another Nanopad instance with the /recover switch so it claims and
    // restores the next orphaned snapshot in its own window. Used to fan out
    // recovery across multiple windows without prompting.
    static void LaunchRecoveryInstance();

    // Delete this process's own recovery snapshot. Call after a clean save /
    // exit, or once the buffer is no longer dirty.
    static void Clear();

  private:
    static void OwnPath(wchar_t (&out)[MAX_PATH]);
    static void OwnTempPath(wchar_t (&out)[MAX_PATH]);
    static void DirPath(wchar_t (&out)[MAX_PATH]);
    static void EnumerateOrphans(std::vector<std::wstring> &outPaths);
    static bool LoadFile(const wchar_t *path, std::wstring &outText, FileInfo &outInfo);
    static bool IsLiveSibling(DWORD pid);
};

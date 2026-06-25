#pragma once
#include <windows.h>

// Helpers for locating portable data files that live next to the running exe.
namespace PathUtil
{
// Fills out with the full path to a file named fileName located in the same
// directory as the running executable. Symlinks are resolved (e.g. a WinGet
// Links shim -> the actual Packages dir) so the path points at the real
// install location. out must be a MAX_PATH-sized buffer.
void GetPortableFilePath(const wchar_t *fileName, wchar_t (&out)[MAX_PATH]);
} // namespace PathUtil

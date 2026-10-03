#pragma once
#include <windows.h>
#include <filesystem>
namespace chat {
// Decode raster data and insert a bounded thumbnail; never execute an attachment.
// Caller initializes GDI+ and OLE on the UI thread.
bool appendInlineImage(HWND transcript, const std::filesystem::path& path,
                       int maxWidth, int maxHeight, int dpi);
}

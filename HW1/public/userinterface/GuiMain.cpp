#include "GuiConnection.h"
#include "InlineImage.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <gdiplus.h>
#include <objidl.h>
#include <shellapi.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

using namespace chat;
namespace {
constexpr COLORREF Ink = RGB(242, 241, 245), Muted = RGB(163, 164, 177);
constexpr COLORREF Surface = RGB(28, 29, 35), Accent = RGB(239, 38, 58);

// The supplied asset is a 1536 x 1024 contact sheet, not a transparent sprite sheet.
// Sample only the icon tiles; never stretch the labels or composite sheet into the UI.
struct Artwork {
    IStream *stream = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    ~Artwork() {
        bitmap.reset();
        if (stream)
            stream->Release();
    }
    void load(int id) {
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
        if (!resource)
            return;
        DWORD size = SizeofResource(nullptr, resource);
        void *source = LockResource(LoadResource(nullptr, resource));
        if (!source || !size)
            return;
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!data)
            return;
        void *destination = GlobalLock(data);
        if (!destination) {
            GlobalFree(data);
            return;
        }
        CopyMemory(destination, source, size);
        GlobalUnlock(data);
        if (FAILED(CreateStreamOnHGlobal(data, TRUE, &stream))) {
            GlobalFree(data);
            return;
        }
        bitmap.reset(Gdiplus::Bitmap::FromStream(stream));
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
            bitmap.reset();
    }
};
enum Control {
    Nick = 101,
    Host,
    Port,
    Connect,
    Rooms,
    Refresh,
    Join,
    Leave,
    RoomName,
    Create,
    Transcript,
    Input,
    Send,
    File,
    History,
    Downloads
};
std::wstring wide(const std::string &text) {
    int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        length);
    return result;
}
std::string utf8(const std::wstring &text) {
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                     nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        length, nullptr, nullptr);
    return result;
}
std::wstring value(HWND control) {
    int count = GetWindowTextLengthW(control);
    std::wstring result(count + 1, L'\0');
    GetWindowTextW(control, result.data(), count + 1);
    result.resize(count);
    return result;
}
std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}
struct App {
    HWND window{}, controls[128]{};
    HFONT font{}, small{}, title{};
    HBRUSH brush = CreateSolidBrush(Surface);
    std::unique_ptr<GuiConnection> network;
    Artwork background, assets;
    std::filesystem::path downloads = executableDirectory() / L"downloads";
    bool connected = false, busy = false, composing = false;
    std::wstring nickname, currentRoom = L"尚未加入", status = L"離線 · 輸入暱稱後連線";
    float scale = 1;
    int width = 1200, height = 780, chatX = 272, chatW = 640;
    HWND hovered = nullptr;
    HWND get(int id) const {
        return controls[id - 100];
    }
    int px(int logical) const {
        return static_cast<int>(logical * scale);
    }
    ~App() {
        network.reset();
        DeleteObject(font);
        DeleteObject(small);
        DeleteObject(title);
        DeleteObject(brush);
    }
    void place(int id, int x, int y, int w, int h) {
        MoveWindow(get(id), px(x), px(y), px(w), px(h), TRUE);
    }
    HWND add(int id, const wchar_t *klass, const wchar_t *text, DWORD style) {
        HWND control = CreateWindowExW(
            0, klass, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!control)
            throw std::runtime_error("Cannot create GUI control");
        controls[id - 100] = control;
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    }
    void label(HDC dc, const std::wstring &text, int x, int y, int w, int h, HFONT face,
               COLORREF color) {
        SelectObject(dc, face);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        RECT rect{px(x), px(y), px(x + w), px(y + h)};
        DrawTextW(dc, text.c_str(), -1, &rect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    void tile(Gdiplus::Graphics &g, int sx, int sy, int sw, int sh,
              int x, int y, int w, int h) {
        if (!assets.bitmap)
            return;
        float fx = assets.bitmap->GetWidth() / 1536.0f;
        float fy = assets.bitmap->GetHeight() / 1024.0f;
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(assets.bitmap.get(), Gdiplus::Rect(px(x), px(y), px(w), px(h)),
                    sx * fx, sy * fy, sw * fx, sh * fy, Gdiplus::UnitPixel);
    }
    void card(Gdiplus::Graphics &g, int x, int y, int w, int h,
              Gdiplus::Color fill, Gdiplus::Color border, int radius = 12) {
        Gdiplus::GraphicsPath path;
        int d = px(radius * 2), l = px(x), t = px(y), r = px(x + w), b = px(y + h);
        path.AddArc(l, t, d, d, 180, 90);
        path.AddArc(r - d, t, d, d, 270, 90);
        path.AddArc(r - d, b - d, d, d, 0, 90);
        path.AddArc(l, b - d, d, d, 90, 90);
        path.CloseFigure();
        Gdiplus::SolidBrush brush(fill);
        Gdiplus::Pen pen(border, std::max(1.0f, scale));
        g.FillPath(&brush, &path);
        g.DrawPath(&pen, &path);
    }
    void append(const std::wstring &text, COLORREF color = Ink) {
        HWND log = get(Transcript);
        if (GetWindowTextLengthW(log) > 100000) {
            SendMessageW(log, EM_SETSEL, 0, 20000);
            SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        }
        SendMessageW(log, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        CHARFORMAT2W format{};
        format.cbSize = sizeof format;
        format.dwMask = CFM_COLOR;
        format.crTextColor = color;
        SendMessageW(log, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        std::wstring line = text + L"\r\n\r\n";
        SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
        SendMessageW(log, EM_SCROLLCARET, 0, 0);
    }
    void updateControls() {
        for (int id : {Nick, Host, Port})
            EnableWindow(get(id), !busy && !connected);
        for (int id : {Rooms, Refresh, Join, Leave, RoomName, Create})
            EnableWindow(get(id), connected);
        for (int id : {Input, Send, File, History})
            EnableWindow(get(id), connected && !currentRoom.empty());
        SetWindowTextW(get(Connect), connected ? L"中斷連線" : busy ? L"取消連線" : L"連線聊天室");
        InvalidateRect(get(Rooms), nullptr, FALSE);
        InvalidateRect(window, nullptr, FALSE);
    }
    bool submit(Packet packet) {
        if (network->send(std::move(packet)))
            return true;
        append(L"傳送佇列已滿或連線已關閉，請稍後重試。", RGB(255, 145, 145));
        return false;
    }
    void layout() {
        RECT rect;
        GetClientRect(window, &rect);
        width = static_cast<int>(rect.right / scale);
        height = static_cast<int>(rect.bottom / scale);
        chatW = std::max(490, width - chatX - std::max(308, width / 4 + 24));
        place(Nick, 40, 140, 200, 30);
        place(Host, 40, 204, 200, 30);
        place(Port, 40, 268, 70, 32);
        place(Connect, 120, 266, 120, 36);
        place(Downloads, chatX + chatW - 130, 30, 130, 36);
        place(Refresh, 176, 333, 64, 28);
        place(Rooms, 40, 376, 200, std::max(60, height - 604));
        place(Join, 40, height - 216, 96, 34);
        place(Leave, 144, height - 216, 96, 34);
        place(RoomName, 40, height - 140, 200, 30);
        place(Create, 40, height - 94, 200, 38);
        place(History, chatX + chatW - 110, 108, 94, 32);
        place(Transcript, chatX + 16, 164, chatW - 32, height - 342);
        place(Input, chatX + 22, height - 150, chatW - 44, 58);
        place(File, chatX + 16, height - 70, 112, 36);
        place(Send, chatX + chatW - 116, height - 70, 100, 36);
        // Leave padding around RichEdit text, including inline image previews.
        RECT inset{px(12), px(12), px(chatW - 60), px(height - 366)};
        SendMessageW(get(Transcript), EM_SETRECT, 0, reinterpret_cast<LPARAM>(&inset));
        InvalidateRect(window, nullptr, TRUE);
    }
    void paint() {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        RECT rect;
        GetClientRect(window, &rect);
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap =
            CreateCompatibleBitmap(dc, std::max(1L, rect.right), std::max(1L, rect.bottom));
        HGDIOBJ previous = SelectObject(memory, bitmap);
        {
            Gdiplus::Graphics graphics(memory);
            graphics.Clear(Gdiplus::Color(16, 17, 24));
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            if (background.bitmap) {
                float factor = std::max(float(rect.right) / background.bitmap->GetWidth(),
                                        float(rect.bottom) / background.bitmap->GetHeight());
                float w = background.bitmap->GetWidth() * factor, h = background.bitmap->GetHeight() * factor;
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(background.bitmap.get(), Gdiplus::RectF((rect.right - w) / 2,
                                                                    (rect.bottom - h) / 2, w, h));
            }
            Gdiplus::SolidBrush shade(Gdiplus::Color(45, 9, 10, 18));
            graphics.FillRectangle(&shade, 0, 0, rect.right, rect.bottom);
            Gdiplus::LinearGradientBrush veil(Gdiplus::Point(0, 0),
                Gdiplus::Point(rect.right, 0), Gdiplus::Color(225, 13, 14, 20),
                Gdiplus::Color(15, 13, 14, 20));
            graphics.FillRectangle(&veil, 0, 0, rect.right, rect.bottom);
            auto panel = Gdiplus::Color(244, 20, 21, 28);
            auto edge = Gdiplus::Color(255, 52, 53, 63);
            card(graphics, 24, 92, 232, height - 116, panel, edge);
            card(graphics, chatX, 92, chatW, height - 116, panel, edge);
            card(graphics, chatX + 12, height - 162, chatW - 24, 80,
                 Gdiplus::Color(255, 28, 29, 35), edge, 8);
            card(graphics, chatX + chatW + 16, height - 187,
                 std::max(264, width - chatX - chatW - 40), 163,
                 Gdiplus::Color(225, 20, 21, 28), edge);
            Gdiplus::Pen divider(edge, scale);
            graphics.DrawLine(&divider, px(chatX + 16), px(152), px(chatX + chatW - 16), px(152));
            graphics.DrawLine(&divider, px(40), px(320), px(240), px(320));
            tile(graphics, 134, 525, 80, 66, 40, 335, 28, 24);
            Gdiplus::SolidBrush red(Gdiplus::Color(239, 38, 58));
            graphics.FillRectangle(&red, px(24), px(24), px(4), px(42));
            graphics.FillRectangle(&red, px(chatX + 16), px(112), px(3), px(18));
            Gdiplus::SolidBrush indicator(connected ? Gdiplus::Color(106, 215, 168)
                : busy ? Gdiplus::Color(244, 182, 69) : Gdiplus::Color(137, 139, 153));
            graphics.FillEllipse(&indicator, px(chatX + chatW + 32), px(height - 156), px(8), px(8));
        }
        label(memory, L"夜航  /  NIGHTLINK", 40, 20, 600, 38, title, Ink);
        label(memory, L"留一盞燈，等一句訊息。", 42, 60, 500, 22, small, Muted);
        label(memory, L"暱稱 / NICKNAME", 40, 110, 200, 22, small, Muted);
        label(memory, L"伺服器 IPv4", 40, 178, 200, 22, small, Muted);
        label(memory, L"連接埠", 40, 242, 70, 22, small, Muted);
        label(memory, L"房間頻道", 76, 333, 100, 28, font, Ink);
        label(memory, L"建立新房間", 40, height - 170, 180, 24, small, Muted);
        label(memory,
              L"#  " +
                  (connected ? (currentRoom.empty() ? L"尚未加入房間" : currentRoom) : L"等待連線"),
              chatX + 30, 102, chatW - 150, 28, font, Ink);
        label(memory, connected ? L"與頻道裡的人聊聊吧。" : L"從左側設定連線，開始今晚的對話。",
              chatX + 30, 130, chatW - 150, 18, small, Muted);
        label(memory, L"Enter 傳送 · Shift + Enter 換行", chatX + 140, height - 67, chatW - 264, 28,
              small, Muted);
        label(memory,
              connected ? L"ONLINE / 已連線"
              : busy    ? L"CONNECTING / 連線中"
                        : L"OFFLINE / 離線",
              chatX + chatW + 50, height - 164, 220, 28, font,
              connected ? RGB(138, 218, 185) : Ink);
        label(memory, status, chatX + chatW + 32, height - 126, 232, 26, small, Ink);
        label(memory, L"NIGHTLINK / 今晚，也有人在。", chatX + chatW + 32, height - 71, 232, 22, small,
              Muted);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, previous);
        DeleteObject(bitmap);
        DeleteDC(memory);
        EndPaint(window, &ps);
    }
    void drawButton(DRAWITEMSTRUCT *item) {
        bool enabled = IsWindowEnabled(item->hwndItem) != FALSE;
        bool primary = item->CtlID == Connect || item->CtlID == Send;
        bool hot = enabled && hovered == item->hwndItem;
        bool pressed = enabled && (item->itemState & ODS_SELECTED);
        COLORREF color = !enabled ? RGB(33, 34, 41) : primary ? Accent : RGB(37, 38, 47);
        if (hot)
            color = primary ? RGB(255, 55, 73) : RGB(58, 37, 47);
        if (pressed)
            color = RGB(151, 27, 45);
        int w = static_cast<int>((item->rcItem.right - item->rcItem.left) / scale);
        int h = static_cast<int>((item->rcItem.bottom - item->rcItem.top) / scale);
        {
            Gdiplus::Graphics g(item->hDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.Clear(Gdiplus::Color(20, 21, 28));
            card(g, 1, 1, w - 2, h - 2,
                 Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)),
                 hot ? Gdiplus::Color(239, 38, 58) : Gdiplus::Color(62, 63, 72), 7);
            if (item->CtlID == Send) {
                // Each icon's source includes its intended background and state.
                int sx = !enabled ? 1286 : pressed ? 1140 : hot ? 1009 : 879;
                tile(g, sx, 194, 80, 58, 9, (h - 22) / 2, 30, 22);
            } else if (enabled && (item->CtlID == File || item->CtlID == Create || item->CtlID == Leave)) {
                int sx = item->CtlID == File ? 434 : item->CtlID == Create ? 252 : 742;
                int sy = item->CtlID == File ? 352 : 539;
                tile(g, sx, sy, 44, 44, 9, (h - 22) / 2, 22, 22);
            }
        }
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, enabled ? Ink : RGB(92, 93, 108));
        SelectObject(item->hDC, font);
        auto text = value(item->hwndItem);
        RECT rect = item->rcItem;
        if (item->CtlID == Send || (enabled && (item->CtlID == File || item->CtlID == Create || item->CtlID == Leave)))
            rect.left += px(28);
        DrawTextW(item->hDC, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS) {
            InflateRect(&rect, -3, -3);
            DrawFocusRect(item->hDC, &rect);
        }
    }
    void drawRoom(DRAWITEMSTRUCT *item) {
        if (item->itemID == static_cast<UINT>(-1))
            return;
        auto length = SendMessageW(get(Rooms), LB_GETTEXTLEN, item->itemID, 0);
        if (length == LB_ERR || length > 32)
            return;
        std::wstring room(static_cast<size_t>(length) + 1, L'\0');
        SendMessageW(get(Rooms), LB_GETTEXT, item->itemID, reinterpret_cast<LPARAM>(room.data()));
        room.resize(static_cast<size_t>(length));
        bool selected = item->itemState & ODS_SELECTED;
        HBRUSH fill = CreateSolidBrush(selected ? RGB(62, 32, 43) : Surface);
        FillRect(item->hDC, &item->rcItem, fill);
        DeleteObject(fill);
        RECT rect = item->rcItem;
        if (selected) {
            RECT marker = rect;
            marker.right = marker.left + px(3);
            fill = CreateSolidBrush(Accent);
            FillRect(item->hDC, &marker, fill);
            DeleteObject(fill);
        }
        rect.left += px(12);
        rect.right -= px(8);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, room == currentRoom ? RGB(255, 144, 160) : Ink);
        SelectObject(item->hDC, font);
        auto text = L"#  " + room;
        DrawTextW(item->hDC, text.c_str(), -1, &rect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (item->itemState & ODS_FOCUS)
            DrawFocusRect(item->hDC, &rect);
    }
    void connect() {
        if (busy || connected) {
            network->stop();
            busy = connected = false;
            currentRoom.clear();
            status = L"已中斷連線";
            SendMessageW(get(Rooms), LB_RESETCONTENT, 0, 0);
            append(L"已中斷連線。", Muted);
            updateControls();
            return;
        }
        nickname = value(get(Nick));
        auto name = utf8(nickname);
        if (name.empty() || name.size() > 32 ||
            name.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                std::string::npos) {
            append(L"暱稱限 1–32 個英文字母、數字、_ 或 -。", RGB(255, 145, 145));
            SetFocus(get(Nick));
            return;
        }
        auto text = value(get(Port));
        if (text.empty() || text.size() > 5 ||
            text.find_first_not_of(L"0123456789") != std::wstring::npos || std::stoi(text) < 1 ||
            std::stoi(text) > 65535) {
            append(L"連接埠必須介於 1 到 65535。", RGB(255, 145, 145));
            return;
        }
        busy = true;
        status = L"正在連線…";
        updateControls();
        try {
            network->start(utf8(value(get(Host))), static_cast<unsigned short>(std::stoi(text)),
                           name);
        } catch (const std::exception &e) {
            busy = false;
            append(wide(e.what()), RGB(255, 145, 145));
            updateControls();
        }
    }
    void sendText() {
        if (!connected || currentRoom.empty())
            return;
        auto text = utf8(value(get(Input)));
        if (text.empty())
            return;
        if (text.size() > 4096) {
            append(L"訊息超過 4096 bytes，請縮短後再傳送。", RGB(255, 145, 145));
            return;
        }
        if (submit({PacketType::SendMessage, {text}})) {
            SetWindowTextW(get(Input), L"");
            SetFocus(get(Input));
        }
    }
    void sendFile() {
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof dialog;
        dialog.hwndOwner = window;
        dialog.lpstrFilter = L"所有檔案\0*.*\0";
        dialog.lpstrFile = filename;
        dialog.nMaxFile = 32768;
        dialog.lpstrTitle = L"選擇要傳送的檔案（最大 1 MiB）";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog))
            return;
        try {
            std::filesystem::path path(filename);
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() < 0 || input.tellg() > 1024 * 1024)
                throw std::runtime_error("File missing or larger than 1 MiB");
            std::string content(static_cast<size_t>(input.tellg()), '\0');
            input.seekg(0);
            if (!input.read(content.data(), static_cast<std::streamsize>(content.size())))
                throw std::runtime_error("Cannot read file");
            if (submit({PacketType::SendFile, {path.filename().u8string(), std::move(content)}}))
                append(L"正在傳送：" + path.filename().wstring(), Muted);
        } catch (const std::exception &e) {
            append(L"檔案傳送失敗：" + wide(e.what()), RGB(255, 145, 145));
        }
    }
    void command(int id, int notification) {
        if (id == Connect) {
            connect();
            return;
        }
        if (id == Downloads) {
            try {
                std::filesystem::create_directories(downloads);
                ShellExecuteW(window, L"open", downloads.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } catch (const std::exception &e) {
                append(wide(e.what()), RGB(255, 145, 145));
            }
            return;
        }
        if (!connected)
            return;
        switch (id) {
        case Refresh:
            submit({PacketType::ListRooms, {}});
            break;
        case Rooms:
            if (notification != LBN_DBLCLK)
                break;
            [[fallthrough]];
        case Join: {
            auto selected = SendMessageW(get(Rooms), LB_GETCURSEL, 0, 0);
            if (selected == LB_ERR) {
                append(L"請先選擇左側的房間。", Muted);
                break;
            }
            auto length = SendMessageW(get(Rooms), LB_GETTEXTLEN, selected, 0);
            if (length == LB_ERR || length > 32) {
                append(L"無效的房間名稱。", Muted);
                break;
            }
            std::wstring room(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(get(Rooms), LB_GETTEXT, selected, reinterpret_cast<LPARAM>(room.data()));
            room.resize(static_cast<size_t>(length));
            submit({PacketType::JoinRoom, {utf8(room)}});
            break;
        }
        case Leave:
            submit({PacketType::LeaveRoom, {}});
            break;
        case Create: {
            auto room = utf8(value(get(RoomName)));
            if (room.empty()) {
                append(L"請輸入新房間名稱。", Muted);
                break;
            }
            submit({PacketType::CreateRoom, {room}});
            break;
        }
        case History:
            submit({PacketType::History, {}});
            break;
        case Send:
            sendText();
            break;
        case File:
            if (!currentRoom.empty())
                sendFile();
            break;
        }
    }
    void incoming() {
        for (auto &event : network->drain()) {
            if (event.kind == GuiEvent::Connected) {
                busy = false;
                connected = true;
                currentRoom = L"lobby";
                status = nickname + L" · lobby";
                append(L"已連線，歡迎 " + nickname + L"。已加入 lobby。", RGB(138, 218, 185));
                submit({PacketType::ListRooms, {}});
                SetFocus(get(Input));
            } else if (event.kind == GuiEvent::Failure) {
                append(L"提示：" + wide(event.text), RGB(255, 145, 145));
            } else if (event.kind == GuiEvent::Disconnected) {
                busy = connected = false;
                currentRoom.clear();
                status = L"連線已關閉 · 可重新連線";
                SendMessageW(get(Rooms), LB_RESETCONTENT, 0, 0);
                append(L"連線已關閉，請確認伺服器或重新連線。", Muted);
            } else {
                auto &p = event.packet;
                if (p.type == PacketType::Rooms && p.fields.size() == 1) {
                    SendMessageW(get(Rooms), LB_RESETCONTENT, 0, 0);
                    std::istringstream lines(p.fields[0]);
                    std::string room;
                    while (std::getline(lines, room)) {
                        auto name = wide(room);
                        SendMessageW(get(Rooms), LB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(name.c_str()));
                    }
                    SendMessageW(get(Rooms), LB_SELECTSTRING, static_cast<WPARAM>(-1),
                                 reinterpret_cast<LPARAM>(currentRoom.c_str()));
                } else if (p.type == PacketType::Message && p.fields.size() == 4) {
                    append(L"[" + wide(p.fields[0]) + L"]  " + wide(p.fields[1]) + L"\r\n" +
                               wide(p.fields[2]),
                           p.fields[1] == utf8(nickname) ? RGB(242, 169, 181) : Ink);
                } else if (p.type == PacketType::File && p.fields.size() == 4) {
                    append(L"[" + wide(p.fields[0]) + L"]  " + wide(p.fields[1]) + L" 傳送檔案：" +
                               wide(p.fields[2]) + L"\r\n已儲存：" + wide(p.fields[3]),
                           RGB(138, 218, 185));
                    if (!appendInlineImage(get(Transcript), std::filesystem::u8path(p.fields[3]),
                                           px(360), px(230), static_cast<int>(96 * scale)))
                        append(L"此檔案沒有圖片預覽，可從下載資料夾開啟。", Muted);
                } else if (!p.fields.empty()) {
                    const auto &text = p.fields[0];
                    if (p.type == PacketType::Ok && text.rfind("Joined ", 0) == 0) {
                        auto nextRoom = wide(text.substr(7));
                        if (nextRoom != currentRoom) {
                            SetWindowTextW(get(Transcript), L"");
                        }
                        currentRoom = nextRoom;
                        status = nickname + L" · " + currentRoom;
                    }
                    if (p.type == PacketType::Ok && text == "Left room") {
                        currentRoom.clear();
                        status = nickname + L" · 尚未加入房間";
                    }
                    if (p.type == PacketType::Ok && text.rfind("Room created", 0) == 0) {
                        submit({PacketType::ListRooms, {}});
                        SetWindowTextW(get(RoomName), L"");
                    }
                    std::wstring display = wide(text);
                    if (p.type == PacketType::Ok) {
                        if (text.rfind("Room created", 0) == 0)
                            display = L"房間已建立，選取後按「加入」或雙擊房間名稱。";
                        else if (text.rfind("Joined ", 0) == 0)
                            display = L"已加入 " + currentRoom;
                        else if (text == "Left room")
                            display = L"已離開房間。";
                        else if (text == "End of history")
                            display = L"歷史訊息讀取完畢。";
                    }
                    append((p.type == PacketType::Error ? L"錯誤：" : L"系統：") + display,
                           p.type == PacketType::Error ? RGB(255, 145, 145) : Muted);
                }
            }
        }
        updateControls();
    }
    void initialize();
};
LRESULT CALLBACK buttonProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                 UINT_PTR, DWORD_PTR data) {
    auto app = reinterpret_cast<App *>(data);
    if (message == WM_MOUSEMOVE && app->hovered != hwnd) {
        HWND previous = app->hovered;
        app->hovered = hwnd;
        if (previous)
            InvalidateRect(previous, nullptr, FALSE);
        InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
    } else if (message == WM_MOUSELEAVE) {
        if (app->hovered == hwnd)
            app->hovered = nullptr;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
LRESULT CALLBACK roomsProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR data) {
    LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    if (message == WM_PAINT && SendMessageW(hwnd, LB_GETCOUNT, 0, 0) == 0) {
        auto app = reinterpret_cast<App *>(data);
        HDC dc = GetDC(hwnd);
        HGDIOBJ previous = SelectObject(dc, app->small);
        RECT rect;
        GetClientRect(hwnd, &rect);
        InflateRect(&rect, -app->px(12), -app->px(14));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, Muted);
        DrawTextW(dc, app->connected ? L"按「更新」取得房間" : L"連線後顯示房間", -1, &rect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, previous);
        ReleaseDC(hwnd, dc);
    }
    return result;
}
LRESULT CALLBACK inputProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                DWORD_PTR data) {
    auto app = reinterpret_cast<App *>(data);
    if (message == WM_IME_STARTCOMPOSITION)
        app->composing = true;
    if (message == WM_IME_ENDCOMPOSITION)
        app->composing = false;
    if (message == WM_KEYDOWN && wParam == VK_RETURN && !app->composing &&
        !(GetKeyState(VK_SHIFT) & 0x8000)) {
        app->sendText();
        return 0;
    }
    if (message == WM_CHAR && wParam == L'\r' && !app->composing &&
        !(GetKeyState(VK_SHIFT) & 0x8000))
        return 0;
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
void App::initialize() {
    HDC dc = GetDC(window);
    scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
    ReleaseDC(window, dc);
    font = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, L"Microsoft JhengHei UI");
    small = CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, 0, L"Microsoft JhengHei UI");
    title = CreateFontW(-px(27), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, 0, L"Microsoft JhengHei UI");
    background.load(101);
    assets.load(102);
    add(Nick, L"EDIT", L"", ES_AUTOHSCROLL);
    add(Host, L"EDIT", L"", ES_AUTOHSCROLL);
    SendMessageW(get(Host), EM_SETCUEBANNER, FALSE,
                 reinterpret_cast<LPARAM>(L"Tailscale 或區網 IPv4"));
    add(Port, L"EDIT", L"9000", ES_NUMBER | ES_AUTOHSCROLL);
    for (int id : {Nick, Host, Port}) {
        SendMessageW(get(id), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(px(7), px(7)));
        SendMessageW(get(id), EM_SETLIMITTEXT, id == Port ? 5 : 32, 0);
    }
    add(Connect, L"BUTTON", L"連線聊天室", BS_OWNERDRAW);
    add(Downloads, L"BUTTON", L"下載資料夾", BS_OWNERDRAW);
    add(Rooms, L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT |
                               LBS_OWNERDRAWFIXED | LBS_HASSTRINGS);
    SendMessageW(get(Rooms), LB_SETITEMHEIGHT, 0, px(38));
    SetWindowSubclass(get(Rooms), roomsProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    add(Refresh, L"BUTTON", L"更新", BS_OWNERDRAW);
    add(Join, L"BUTTON", L"加入", BS_OWNERDRAW);
    add(Leave, L"BUTTON", L"離開", BS_OWNERDRAW);
    add(RoomName, L"EDIT", L"", ES_AUTOHSCROLL);
    SendMessageW(get(RoomName), EM_SETLIMITTEXT, 32, 0);
    add(Create, L"BUTTON", L"建立房間", BS_OWNERDRAW);
    add(Transcript, MSFTEDIT_CLASS, L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL);
    SendMessageW(get(Transcript), EM_SETBKGNDCOLOR, 0, Surface);
    SendMessageW(get(Transcript), EM_EXLIMITTEXT, 0, 150000);
    add(Input, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL);
    SendMessageW(get(Input), EM_SETLIMITTEXT, 4096, 0);
    SendMessageW(get(Input), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(px(8), px(8)));
    SetWindowSubclass(get(Input), inputProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    add(Send, L"BUTTON", L"傳送", BS_OWNERDRAW);
    add(File, L"BUTTON", L"附加檔案", BS_OWNERDRAW);
    add(History, L"BUTTON", L"歷史訊息", BS_OWNERDRAW);
    for (int id : {Connect, Downloads, Refresh, Join, Leave, Create, Send, File, History})
        SetWindowSubclass(get(id), buttonProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(get(Nick), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"你的暱稱"));
    SendMessageW(get(RoomName), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"輸入房間名稱"));
    network = std::make_unique<GuiConnection>(window, downloads);
    append(L"歡迎來到夜航。\r\n輸入暱稱，連上你的聊天室。", Muted);
    if (!background.bitmap)
        append(L"背景圖片載入失敗。", RGB(255, 145, 145));
    if (!assets.bitmap)
        append(L"介面圖示載入失敗，仍可使用文字按鈕。", RGB(255, 145, 145));
    layout();
    updateControls();
    SetFocus(get(Nick));
}
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = reinterpret_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        try {
            app->initialize();
        } catch (const std::exception &e) {
            MessageBoxW(window, wide(e.what()).c_str(), L"啟動失敗", MB_ICONERROR);
            return -1;
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(lParam);
        info->ptMinTrackSize = {app->px(1080), app->px(730)};
        return 0;
    }
    case WM_SIZE:
        if (app->network && wParam != SIZE_MINIMIZED)
            app->layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        app->paint();
        return 0;
    case WM_DRAWITEM:
        if (reinterpret_cast<DRAWITEMSTRUCT *>(lParam)->CtlID == Rooms)
            app->drawRoom(reinterpret_cast<DRAWITEMSTRUCT *>(lParam));
        else
            app->drawButton(reinterpret_cast<DRAWITEMSTRUCT *>(lParam));
        return TRUE;
    case WM_MEASUREITEM:
        reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->itemHeight = app->px(38);
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), Ink);
        SetBkColor(reinterpret_cast<HDC>(wParam), Surface);
        return reinterpret_cast<LRESULT>(app->brush);
    case WM_COMMAND:
        app->command(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case NetworkEvent:
        app->incoming();
        return 0;
    case WM_CLOSE:
        if (app->network)
            app->network->stop();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    SetProcessDPIAware();
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
    if (FAILED(OleInitialize(nullptr))) {
        WSACleanup();
        return 1;
    }
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        OleUninitialize();
        WSACleanup();
        return 1;
    }
    HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
    INITCOMMONCONTROLSEX common{sizeof common, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    int result = 1;
    {
        App app;
        WNDCLASSW klass{};
        klass.lpfnWndProc = windowProcedure;
        klass.hInstance = instance;
        klass.lpszClassName = L"NightlinkChat";
        klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        klass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassW(&klass);
        HDC dc = GetDC(nullptr);
        float scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
        ReleaseDC(nullptr, dc);
        HWND window = CreateWindowExW(
            WS_EX_CONTROLPARENT, klass.lpszClassName, L"夜航 NIGHTLINK — Winsock 聊天室",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
            static_cast<int>(1240 * scale), static_cast<int>(820 * scale), nullptr, nullptr,
            instance, &app);
        if (window) {
            ShowWindow(window, show);
            UpdateWindow(window);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                // Multiline input handles Enter itself (including IME input); Tab uses dialog
                // navigation.
                if (message.hwnd == app.get(Input) && message.message == WM_KEYDOWN &&
                    message.wParam == VK_RETURN) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                } else if (!IsDialogMessageW(window, &message)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            result = 0;
        }
    }
    if (richEdit)
        FreeLibrary(richEdit);
    Gdiplus::GdiplusShutdown(token);
    OleUninitialize();
    WSACleanup();
    return result;
}

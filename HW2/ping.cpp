#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

using namespace std;

using Clock = chrono::steady_clock; // Windows 使用高解析度效能計數器。
using Seconds = chrono::duration<double>;
static volatile LONG interrupted = 0;

// 收到 Ctrl+C 時通知主迴圈結束，保留最後的統計輸出。
BOOL WINAPI onConsoleEvent(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT)
    {
        InterlockedExchange(&interrupted, 1);
        return TRUE;
    }
    return FALSE;
}

// ICMP 標頭固定為 8 位元組，不允許編譯器插入填充。
#pragma pack(push, 1)
struct ICMPHeader
{
    uint8_t type, code;
    uint16_t checksum, id, sequence;
};
#pragma pack(pop)

// 以 16 位元為單位計算 Internet checksum，並處理奇數長度。
uint16_t checksum(const unsigned char *data, size_t length)
{
    uint32_t sum = 0;
    while (length >= 2)
    {
        sum += (static_cast<uint16_t>(data[0]) << 8) | data[1];
        data += 2;
        length -= 2;
    }
    if (length)
    {
        sum += static_cast<uint16_t>(*data) << 8;
    }
    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

void usage()
{
    cerr << "Usage: ping destination [-c count] [-i interval] [-W timeout] [-s size]\n"
            "  -c count     number of requests (default: unlimited)\n"
            "  -i interval  seconds between requests (default: 1)\n"
            "  -W timeout   seconds to wait for each reply (default: 2)\n"
            "  -s size      data bytes, 0-65507 (default: 56)\n";
}

bool number(const char *text, double &result)
{
    char *end = nullptr;
    result = strtod(text, &end);
    return *text && *end == '\0' && isfinite(result);
}

// 將命令列設定集中保存，count 為 0 表示持續傳送。
struct PingOptions
{
    long long count = 0;
    double interval = 1;
    double timeout = 2;
    int dataSize = 56;
    string host;
};

enum class ParseResult
{
    Success,
    Help,
    Error
};

// 將 argc、argv 交由此函式解析與檢查。
ParseResult parseArguments(int argc, char *argv[], PingOptions &options)
{
    for (int i = 1; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            usage();
            return ParseResult::Help;
        }
        if (arg == "-c" || arg == "-i" || arg == "-W" || arg == "-s")
        {
            double value;
            if (++i == argc || !number(argv[i], value))
            {
                usage();
                return ParseResult::Error;
            }
            // switch 使用選項字元，分別檢查並保存對應的數值。
            switch (arg[1])
            {
            case 'c':
                if (value < 1 || value > 2147483647 || floor(value) != value)
                {
                    usage();
                    return ParseResult::Error;
                }
                options.count = static_cast<long long>(value);
                break;
            case 's':
                if (value < 0 || value > 65507 || floor(value) != value)
                {
                    usage();
                    return ParseResult::Error;
                }
                options.dataSize = static_cast<int>(value);
                break;
            case 'i':
                if (value < 0.01 || value > 3600)
                {
                    usage();
                    return ParseResult::Error;
                }
                options.interval = value;
                break;
            case 'W':
                if (value < 0.001 || value > 3600)
                {
                    usage();
                    return ParseResult::Error;
                }
                options.timeout = value;
                break;
            }
        }
        else if (!arg.empty() && arg[0] != '-' && options.host.empty())
        {
            options.host = arg;
        }
        else
        {
            usage();
            return ParseResult::Error;
        }
    }
    if (options.host.empty())
    {
        usage();
        return ParseResult::Error;
    }

    return ParseResult::Success;
}

int main(int argc, char *argv[])
{
    PingOptions options;
    ParseResult result = parseArguments(argc, argv, options);
    if (result != ParseResult::Success)
    {
        return result == ParseResult::Help ? 0 : 2;
    }

    const auto &[count, interval, timeout, dataSize, host] = options;

    // 初始化 Winsock，並將目標名稱解析成 IPv4 位址。
    WSADATA wsa{};
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err)
    {
        cerr << "WSAStartup: " << err << '\n';
        return 2;
    }
    addrinfo hints{}, *addresses = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    err = getaddrinfo(host.c_str(), nullptr, &hints, &addresses);
    if (err || !addresses)
    {
        cerr << "ping: cannot resolve " << host << " (" << err << ")\n";
        WSACleanup();
        return 2;
    }
    sockaddr_in target = *reinterpret_cast<sockaddr_in *>(addresses->ai_addr);
    freeaddrinfo(addresses);
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &target.sin_addr, ip, sizeof(ip));
    // 透過 Raw Socket 直接收送 ICMP，通常需要系統管理員權限。
    SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock == INVALID_SOCKET)
    {
        cerr << "ping: raw socket: " << WSAGetLastError() << " (run as Administrator)\n";
        WSACleanup();
        return 2;
    }
    if (!SetConsoleCtrlHandler(onConsoleEvent, TRUE))
    {
        cerr << "ping: cannot install Ctrl+C handler\n";
        closesocket(sock);
        WSACleanup();
        return 2;
    }

    cout << "PING " << host << " (" << ip << ") " << dataSize << "(" << dataSize + 28
         << ") bytes of data.\n";
    cout << fixed << setprecision(3);
    vector<unsigned char> packet(sizeof(ICMPHeader) + dataSize, 'A');
    const uint16_t id = htons(static_cast<uint16_t>(GetCurrentProcessId()));
    long long sent = 0, received = 0;
    double sum = 0, sumSquares = 0, minimum = numeric_limits<double>::max(), maximum = 0;
    // 記錄尚未回覆的序號及送出時間，供逾時與 RTT 計算使用。
    map<uint16_t, Clock::time_point> pending;
    auto nextSend = Clock::now();
    bool fatal = false;
    while (!InterlockedCompareExchange(&interrupted, 0, 0))
    {
        auto now = Clock::now();
        // 清除已逾時的請求。
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (Seconds(now - it->second).count() >= timeout)
            {
                it = pending.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if ((!count || sent < count) && now >= nextSend)
        {
            uint16_t sequence = static_cast<uint16_t>(sent + 1);
            // 避免序號環繞後與仍在等待的封包混淆。
            if (pending.count(sequence))
            {
                cerr << "ping: sequence still pending\n";
                fatal = true;
                break;
            }
            // 建立 Echo Request，再將標頭與資料一起計算 checksum。
            ICMPHeader header{8, 0, 0, id, htons(sequence)};
            memcpy(packet.data(), &header, sizeof(header));
            header.checksum = htons(checksum(packet.data(), packet.size()));
            memcpy(packet.data(), &header, sizeof(header));
            auto start = Clock::now();
            int result = sendto(sock, reinterpret_cast<const char *>(packet.data()),
                                static_cast<int>(packet.size()), 0,
                                reinterpret_cast<sockaddr *>(&target), sizeof(target));
            if (result == SOCKET_ERROR || result != static_cast<int>(packet.size()))
            {
                cerr << "ping: sendto: " << WSAGetLastError() << '\n';
                fatal = true;
                break;
            }
            ++sent;
            pending.emplace(sequence, start);
            nextSend = start + chrono::duration_cast<Clock::duration>(Seconds(interval));
        }
        if (count && sent >= count && pending.empty())
        {
            break;
        }

        // select 最多等待到下一次傳送、封包逾時或需要檢查 Ctrl+C。
        now = Clock::now();
        double waitSeconds = 0.1; // 使 Ctrl+C 迅速結束等待。
        if (!count || sent < count)
        {
            waitSeconds = min(waitSeconds, max(0.0, Seconds(nextSend - now).count()));
        }
        for (const auto &entry : pending)
        {
            waitSeconds = min(waitSeconds, max(0.0, timeout - Seconds(now - entry.second).count()));
        }
        long micros = static_cast<long>(waitSeconds * 1000000);
        timeval wait{micros / 1000000, micros % 1000000};
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        int ready = select(0, &readSet, nullptr, nullptr, &wait);
        if (ready == SOCKET_ERROR)
        {
            cerr << "ping: select: " << WSAGetLastError() << '\n';
            fatal = true;
            break;
        }
        if (!ready)
        {
            continue;
        }
        unsigned char buffer[65536];
        sockaddr_in sender{};
        int senderSize = sizeof(sender);
        int bytes = recvfrom(sock, reinterpret_cast<char *>(buffer), sizeof(buffer), 0,
                             reinterpret_cast<sockaddr *>(&sender), &senderSize);
        auto end = Clock::now();
        if (bytes == SOCKET_ERROR)
        {
            cerr << "ping: recvfrom: " << WSAGetLastError() << '\n';
            fatal = true;
            break;
        }
        // Raw Socket 收到的資料包含 IPv4 標頭，先確認長度再解析 ICMP。
        if (bytes < 20 || (buffer[0] >> 4) != 4)
        {
            continue;
        }
        int ipLength = (buffer[0] & 15) * 4;
        int total = (static_cast<int>(buffer[2]) << 8) | buffer[3];
        if (ipLength < 20 || total > bytes ||
            total < ipLength + static_cast<int>(sizeof(ICMPHeader)))
        {
            continue;
        }
        ICMPHeader reply{};
        memcpy(&reply, buffer + ipLength, sizeof(reply));
        uint16_t sequence = ntohs(reply.sequence);
        auto it = pending.find(sequence);
        // 只接受來源、識別碼、序號與 checksum 都符合的 Echo Reply。
        if (sender.sin_addr.s_addr != target.sin_addr.s_addr || reply.type != 0 ||
            reply.code != 0 || reply.id != id || it == pending.end() ||
            checksum(buffer + ipLength, total - ipLength) != 0 ||
            Seconds(end - it->second).count() > timeout)
        {
            continue;
        }
        double ms = Seconds(end - it->second).count() * 1000.0;
        pending.erase(it);
        ++received;
        sum += ms;
        sumSquares += ms * ms;
        minimum = min(minimum, ms);
        maximum = max(maximum, ms);
        cout << total - ipLength << " bytes from " << ip << ": icmp_seq=" << sequence
             << " ttl=" << static_cast<int>(buffer[8]) << " time=" << ms << " ms\n"
             << flush;
    }
    // 顯示封包遺失率與 RTT 最小值、平均值、最大值和標準差。
    cout << "\n--- " << host << " ping statistics ---\n"
         << sent << " packets transmitted, " << received << " received, "
         << (sent ? 100.0 * (sent - received) / sent : 0.0) << "% packet loss\n";
    if (received)
    {
        double average = sum / received;
        double deviation = sqrt(max(0.0, sumSquares / received - average * average));
        cout << "rtt min/avg/max/mdev = " << minimum << '/' << average << '/' << maximum << '/'
             << deviation << " ms\n";
    }
    SetConsoleCtrlHandler(onConsoleEvent, FALSE);
    closesocket(sock);
    WSACleanup();
    return fatal ? 2 : (received ? 0 : 1);
}

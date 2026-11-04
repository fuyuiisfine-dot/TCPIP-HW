# HW2：ICMP Ping

Windows Winsock Raw Socket 實作 IPv4 ICMP Echo Request / Echo Reply，介面參考 Unix ping。目標可為 IPv4 位址或網域名稱。預設持續傳送，以 Ctrl+C 停止並顯示統計。

```powershell
.\build\ping.exe 127.0.0.1
.\build\ping.exe example.com -c 4 -i 1 -W 2 -s 56
```

| 參數 | 功能 | 預設值 |
| --- | --- | --- |
| `-c` | 傳送次數 | 持續傳送 |
| `-i` | 傳送間隔（秒，0.01–3600） | 1 |
| `-W` | 每個封包等待回覆的時間（秒，0.001–3600） | 2 |
| `-s` | ICMP 資料大小（位元組，0–65507） | 56 |
| `-h`、`--help` | 顯示用法 | — |

RTT 使用 C++ steady clock 的高解析度計時，輸出毫秒至小數點後三位；實際精度仍受作業系統排程與網路影響。統計顯示封包遺失率，以及 RTT 的最小值、平均值、最大值及母體標準差（mdev）。逾時封包計入遺失；程式不支援 IPv6，也不會顯示路由器傳回的 ICMP 錯誤訊息。

在 VS Code 開啟整個 `TCP` 資料夾，從「執行與偵錯」選擇 **Ping: HW2**，按 F5 後輸入目標與封包數量。Windows Raw Socket 需要系統管理員權限；若建立 socket 失敗，請以系統管理員身分啟動 VS Code。

也可在本資料夾使用 MinGW 手動編譯：

```powershell
g++ -std=c++17 ping.cpp -o ping.exe -lws2_32
.\ping.exe 127.0.0.1 -c 4
```

結束碼：收到回覆為 0；沒有收到回覆為 1；參數、解析或系統錯誤為 2。

# ES-WS-04 RS485 Modbus RTU trên ATMC/ESP32

Firmware đọc góc hướng gió ES-WS-04 và in JSON qua USB Serial để máy tính hiển thị.

## Thông số theo datasheet

- Nguồn: 10-30 VDC, công suất tối đa 0,2 W.
- Dây đỏ: dương nguồn; đen: âm nguồn.
- Dây vàng: RS485-A; xanh: RS485-B.
- Modbus RTU mặc định: slave 1, 9600 baud, 8N1.
- Hàm đọc: `0x03`; thanh ghi góc: `0x0000`.
- Thiết bị thật trả giá trị theo 0,1 độ (`3512` = `351,2°`), phù hợp dải 0-359,9° ở đầu datasheet. Ví dụ cuối datasheet ghi số nguyên không có hệ số là không khớp với thiết bị đã kiểm thử.
- Request mặc định: `01 03 00 00 00 01 84 0A`.

## Chân của bo ATMC

- RS485 RX: GPIO16.
- RS485 TX: GPIO17.
- DE/RE: `-1`, do mạch RS485 tích hợp tự đổi chiều.

Các chân này lấy từ firmware ES-WS-02 trước đây đi cùng bo ATMC. Nếu bo khác phiên bản,
sửa các macro ở đầu `es_ws_04_rs485.ino`.

## Nạp và xem

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\atmc_es_ws04_flash.ps1 -Port COM11
```

Sau khi kiểm tra log, nhấn `Ctrl+C` để nhả COM11, chạy `es-ws04-monitor.cmd`, chọn COM11.
Thiết bị đã kiểm thử phản hồi ở 4800 baud; firmware tự thử 9600, 4800 rồi 2400.

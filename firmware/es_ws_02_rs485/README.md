# ES-WS-02 — tốc độ gió qua ATMC RS485

Nguồn: `esp32-NBIoT V2/ES-WS-02 Product Manual.pdf`, trang 3, 5–7.

- Nguồn cảm biến 10–30 VDC: nâu dương, đen GND; vàng RS485 A/+, xanh dương B/−.
- ATMC dùng UART2 RX16/TX17 và bộ chuyển RS485 tự đổi chiều, như thiết lập ES-WS-04 trước đó.
- Modbus RTU, mặc định slave 1, 4800 8N1. USB Serial luôn 115200.
- Đọc function 03, bắt đầu 0000, hai thanh ghi theo ví dụ mục 5.3.
  Chỉ giải mã thanh ghi 0: giá trị unsigned chia 10 = m/s.
- Ví dụ tài liệu: `01 03 04 00 24 00 03 FA 39` → 3,6 m/s.
  Thanh ghi 1 chưa được bảng mô tả nên không gán ý nghĩa cho nó.
- Kiểm CRC, địa chỉ và độ dài; lỗi trả `speed_mps:null`. Không ghi cấu hình cảm biến.
- Nếu mất phản hồi 3 lần, thử baud tiếp theo; địa chỉ vẫn là 1.

Từ gốc repo:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bas_flash.ps1 -Sketch es_ws_02_rs485 -CompileOnly
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bas_flash.ps1 -Sketch es_ws_02_rs485 -Port COM11
```

Hoặc chạy `atmc-es-ws02.cmd`. Đóng monitor trước khi nạp; sau nạp dừng log
Serial bằng Ctrl+C để nhả COM. Nạp sketch này thay firmware ES-WS-04 trên bo.

Mở `es-ws04-monitor.cmd` (tên launcher cũ được giữ), hoặc tải lại
http://127.0.0.1:8092/ nếu máy chủ đã chạy. Chọn COM11. Trang tự nhận
`[ESWS02]` để hiển thị m/s và km/h; vẫn hỗ trợ `[ESWS04]` cho hướng gió.
Khi lỗi hoặc quá 3,5 giây không có số đo hợp lệ, số đo được xóa.
Đây là dữ liệu qua USB vào máy tính; không gửi lên broker hoặc Internet.

## Kiểm chứng ngày 02/10/2026

- Biên dịch ESP32 core 3.3.12: 270548 byte flash, 22204 byte RAM tĩnh.
- Nạp ATMC ESP32-D0WD qua COM11, esptool xác minh hash thành công.
- Đọc 21 bản tin thực tế trong 20 giây: 0,0–0,6 m/s, 4800 baud, slave 1,
  tất cả `status:ok`, bộ đếm `errors:0`. COM đã được đóng sau kiểm tra.
- Log cục bộ: `runtime/esws02-20261002-111315.log` (Git bỏ qua).
- Trang 8092 trả HTTP 200 và chứa giao diện ES-WS-02 mới. Kiểm thử JavaScript
  đạt: số đo, đổi km/h, giá trị 0, dữ liệu lỗi, mất dữ liệu và tương thích ES-WS-04.
- Chưa xác nhận trực quan trong trình duyệt vì phiên này không có trình duyệt
  được kết nối công cụ điều khiển. Người dùng tải lại trang, chọn COM11.

Kiểm thử giao diện: `node windows/es_ws04_monitor/monitor.test.cjs`.
Đọc Serial độc lập (cần pyserial):
`python windows/check_wind_serial.py --port COM11 --seconds 20`.

# ATMC đọc đồng thời tốc độ và hướng gió

Sketch: `wind_dual_rs485`. Trang hiển thị chung: http://127.0.0.1:8092/,
khởi động bằng `es-ws04-monitor.cmd` ở gốc repo.

| Cảm biến | Địa chỉ Modbus | Giá trị | Mặc định thử baud |
|---|---|---|---|
| ES-WS-02 | 2 | Thanh ghi 0 / 10, m/s; đọc 2 thanh ghi theo tài liệu | 4800 |
| ES-WS-04 | 1 | Thanh ghi 0 / 10, độ; đọc 1 thanh ghi theo bản đã kiểm thử | 4800 |

ATMC dùng UART2 RX16/TX17 qua RS485 tự đổi chiều; USB Serial 115200.
Firmware hỏi lần lượt hai địa chỉ khoảng mỗi giây. Mỗi cảm biến có bộ đếm lỗi
và dò baud riêng; một cảm biến mất phản hồi không ngăn việc đọc cảm biến kia.
Trang hiển thị m/s, km/h, góc và la bàn trong hai ô riêng; dữ liệu lỗi hoặc
quá 3,5 giây không có bản tin mới được xóa ở đúng ô tương ứng.

## Cấu hình lần đầu

Cả hai cảm biến ban đầu đều địa chỉ 1. Không nối chung trước khi đổi địa chỉ.

1. Ngắt nguồn trước khi thay dây. Chỉ nối ES-WS-02 vào ATMC, cấp nguồn lại.
2. Ngắt COM trên trình duyệt/Arduino monitor; nạp bằng `atmc-wind-dual.cmd`.
3. Lần đầu firmware dừng chờ cấu hình. Với Python + pyserial, chạy từ gốc repo:

   ```powershell
   python windows/wind_dual_control.py configure-speed --port COM11 --speed-only-connected
   ```

   Lệnh STOP trước, đọc xác minh thanh ghi 0x07D0, đổi địa chỉ 1 → 2,
   rồi đọc lại địa chỉ và số đo. Chỉ tiếp tục nếu có `SPEED_ADDRESS_2_OK`.
   Tài liệu ES-WS-02 có ví dụ đổi địa chỉ không khớp bảng thanh ghi;
   firmware chỉ ghi 0x07D0 sau khi đọc được đúng địa chỉ cũ, không thử ghi
   thanh ghi khác nếu thất bại. Lệnh cấu hình chỉ hỗ trợ baud mặc định 4800.
4. Ngắt nguồn, nối thêm ES-WS-04 theo bảng bên dưới, cấp nguồn lại.
5. Chạy `python windows/wind_dual_control.py start --port COM11 --seconds 20`.
   Kiểm tra cả `[ESWS02]` và `[ESWS04]` đều có `status:ok`, địa chỉ 2 và 1.
   Trạng thái START được lưu trên ESP32 và tự tiếp tục sau khi cấp nguồn lại.
6. Công cụ tự nhả COM khi hết thời gian. Tải lại trang 8092 bằng Ctrl+F5,
   chọn COM11; hai ô cập nhật độc lập.

Có thể gửi `STOP`, `SET_SPEED_ADDRESS_2`, `START` bằng Serial Monitor 115200,
kết thúc dòng Newline. Chỉ gửi lệnh đổi địa chỉ khi ES-WS-02 đứng riêng trên bus.
Lệnh STOP cũng được lưu qua khởi động lại.

## Nối chung RS485

| Đầu nối | ES-WS-02 | ES-WS-04 |
|---|---|---|
| Nguồn cảm biến +10–30 VDC | Nâu | Đỏ |
| Âm nguồn/GND chung | Đen | Đen |
| ATMC RS485 A/+ | Vàng | Vàng |
| ATMC RS485 B/− | Xanh dương | Xanh |

Dùng nguồn đủ cấp cả hai cảm biến và đối chiếu nhãn dây thực tế.
Giữ nguyên mạch RS485 tích hợp ATMC; không nối dây A/B trực tiếp vào GPIO.

## Kiểm tra phần mềm

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/bas_flash.ps1 -Sketch wind_dual_rs485 -CompileOnly
node windows/es_ws04_monitor/monitor.test.cjs
```

Log kiểm chứng phần cứng được lưu trong `runtime/wind-dual-*.log` (Git bỏ qua).
Firmware không tự nhận dạng model cảm biến: địa chỉ 2 được gán tốc độ,
địa chỉ 1 được gán hướng, nên phải nối và cấu hình đúng theo bảng.

# ATMC / ESP32 + TF03: web qua Wi-Fi

Sketch cho ESP32 thường, Arduino core esp32 3.3.12. Không chạy NB-IoT,
không điều khiển relay. Trang web nằm ngay trên ESP32, không cần Internet,
MQTT hoặc thư viện bên ngoài core ESP32. Bản MQTT trước vẫn ở `tf03_wifi_mqtts`.

## Nạp

Mở `atmc-tf03-web.cmd` trong thư mục `esp32-NBIoT`, nhập COM của **ESP32 trên bo ATMC**.
Hoặc từ thư mục đó:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\bas_flash.ps1 -Sketch atmc_tf03_web -CompileOnly
powershell -ExecutionPolicy Bypass -File .\windows\bas_flash.ps1 -Sketch atmc_tf03_web -Port COM16
```

Thay COM16 bằng cổng thực tế. Nạp sẽ thay chương trình đang chạy trên ESP32.
Không chọn một module ESP32 khác nếu hệ thống có nhiều module.

## Mở web

1. Sau khi nạp, xem Serial 115200 (script tự mở).
2. Bo phát Wi-Fi `ATMC-TF03-xxxx`. Mật khẩu ngẫu nhiên được lưu riêng trên bo
   và in tại dòng `Setup password / admin password` mỗi lần khởi động.
3. Kết nối điện thoại/máy tính vào Wi-Fi đó, mở `http://192.168.4.1`.
   Chấp nhận giữ kết nối dù mạng này không có Internet.
4. Mở **Cấu hình Wi-Fi và cảm biến**. Đăng nhập `admin`, mật khẩu như bước 2.
5. Nhập Wi-Fi 2,4 GHz của bạn rồi lưu. Bo khởi động lại; Serial in
   `Open website: http://<IP>/`. Mở IP này từ thiết bị cùng mạng Wi-Fi.
   Wi-Fi riêng của bo vẫn có để cấu hình lại nếu nhập sai mật khẩu mạng.

File `config.h` cục bộ không được sao chép từ V2. Có thể tạo từ
`config.example.h`; khi chưa có, sketch dùng cấu hình mẫu. Cấu hình lưu trên web được ưu tiên;
không hiển thị mật khẩu mạng trong API. Mật khẩu quản trị/Wi-Fi cấu hình
khác với mật khẩu Wi-Fi router. Trang đọc số đo không yêu cầu đăng nhập;
trang cấu hình và các lệnh lưu yêu cầu đăng nhập và token biểu mẫu.
Trang này dành cho mạng nội bộ, chưa triển khai truy cập Internet/cloud.

## Chân TF03

Mặc định GPIO nhận là **-1**, tức chưa mở chân nhận. Trong trang cấu hình,
điền GPIO ESP32 thực sự nối tới **TX của TF03**, theo sơ đồ bo ATMC.
Không suy đoán SDA/SCL trên đầu nối là GPIO21/22. Cần biết đường mạch
SDA/SCL của chính model ATMC; nếu có mạch chuyển mức hoặc chip trung gian,
cần xác nhận cổng đó có thể dùng UART. Không tự động quét chân trên bo điều khiển.
Code chỉ nhận UART, không phát TX. Baud mặc định 115200, 8N1.
TF03 phải xuất khung nhị phân chuẩn 9 byte, đơn vị cm.

Trang cấu hình cho phép đổi GPIO, baud, mã ngoài tầm và lưu qua lần tắt nguồn.
Một số GPIO có thể dành cho ngoại vi ATMC; danh sách hợp lệ ESP32 không thay thế sơ đồ bo.
TF03-UART vẫn truyền số đo đến ESP32 bằng dây; phần từ ESP32 đến trình duyệt dùng Wi-Fi.

## Dữ liệu

Trang cập nhật mỗi 500 ms. Task riêng đọc cảm biến liên tục khi web đang xử lý.
API `GET /api/data` trả JSON, ví dụ minh họa:

```json
{"distance":1.25,"unit":"m","status":"ok","strength":350,"age_ms":4,"frames":100,"wifi_connected":true,"ip":"192.168.1.20","rx_gpio":21}
```

GPIO21 và địa chỉ IP trên chỉ là ví dụ, không phải sơ đồ ATMC đã xác minh.
Khung sai checksum bị bỏ. Dữ liệu quá 2 giây, cường độ <40, khoảng cách 0
hoặc >= mã ngoài tầm đều trả `distance: null`. Mã ngoài tầm mặc định 18000 cm;
đặt đúng model/cấu hình cảm biến. Hai byte dự phòng không giải mã thành nhiệt độ.
Các trạng thái: `pin_not_configured`, `uart_error`, `no_data`, `stale`, `invalid`, `ok`.
Mất kết nối web sẽ xóa số đo hiện tại trên giao diện, không giữ số cũ như đang đo.

Kiểm thử parser tại thời điểm biên dịch: khung đúng, khung thiếu byte,
checksum sai, nhiễu/đầu khung giả và khả năng đồng bộ lại (`parser_checks.h`).
Kiểm tra phần cứng cần: số đo thật, rút cảm biến -> `stale`, đổi Wi-Fi và khởi động lại.

Nguồn giao thức: https://en.benewake.com/uploadfiles/2025/12/20251216145317597.pdf
Wi-Fi ESP32: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html

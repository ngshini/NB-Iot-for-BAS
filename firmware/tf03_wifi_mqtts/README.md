# TF03 -> ESP32 ATMC -> Wi-Fi -> MQTT TLS -> BAS

## Chạy nhanh

1. Sửa `config.h`: Wi-Fi 2,4 GHz, broker/topic của web và thông tin đăng nhập nếu server yêu cầu.
2. Kiểm tra `tf03_settings.h`: `TF03_RX_PIN` phải là GPIO ESP32 nối với TX của TF03.
3. Nhấp `atmc-tf03-wifi.cmd`, nhập đúng cổng COM của ESP32 trên board ATMC.
4. Giữ cửa sổ Serial mở. Khi thấy `Publish OK`, mở `bas-monitor.cmd` và bấm **Theo dõi broker**.

Launcher `atmc-tf03-web.cmd` cũ cũng đã được chuyển sang sketch này. Không chạy trực tiếp
`bas_flash.ps1` mà không truyền `-Sketch`, vì mặc định script đó phục vụ firmware BAS khác.

## Tích hợp BAS (cập nhật)

Nạp `tf03_wifi_mqtts` vào COM16. Mở `bas-monitor.cmd`, bấm **Theo dõi broker**,
topic `bas/BAS_TEST_001/telemetry`. Ô **ATMC · Laser TF03 qua Wi-Fi** nhận bản tin
trực tiếp từ broker; không cần kết nối Serial để xem khoảng cách.
Firmware tự kết nối Wi-Fi và gửi mỗi giây, không cần `MODE WIFI`.
GPIO21 và GPIO22 lấy theo sketch ATMC PlatformIO hiện có; firmware chỉ luân phiên
nghe RX khi không có khung TF03, không cấu hình chân TX. Ánh xạ này vẫn cần
được xác nhận bằng khung thực nhận trên thiết bị. Không tự dò các GPIO khác.
PlatformIO `src/main.cpp` và `src/wifi.cpp` dùng chung nguồn firmware này.
Bản đọc Serial cũ lưu tại `main.serial-only.cpp.bak` trong dự án PlatformIO.
Nếu mất bản tin quá 5 giây BAS xóa số đo hiện tại. Payload báo `no_data`, `stale`
hoặc `invalid` không hiển thị như khoảng cách hợp lệ.

Firmware riêng cho ESP32 thường (core 3.3.12), dùng PubSubClient 2.8.
Không điều khiển relay hoặc khởi chạy modem NB-IoT.

## Cấu hình

- `config.h`: cấu hình Wi-Fi, broker, topic và xác thực (không đưa vào git).
  Nếu chưa có, sao chép `config.example.h`. Cấu hình cục bộ đã được sao chép
  từ `bas_mqtts` khi tạo firmware này. Wi-Fi đã lưu trong namespace `bas`
  trên ESP32 được ưu tiên hơn thông tin Wi-Fi trong file.
- `tf03_settings.h`: đặt `TF03_RX_PIN` bằng GPIO ESP32 thực sự nối tới TX cảm biến.
  Hiện dùng GPIO21, thử GPIO22 nếu không có khung, theo code ATMC trong dự án.
  Không mặc định SDA/SCL của bo ATM là GPIO21/22. Cần sơ đồ bo hoặc kiểm tra
  đường mạch khi đã ngắt nguồn. RX cảm biến không cần cho chế độ chỉ nhận.
- Mặc định UART 115200 8N1, cảm biến đang xuất khung nhị phân 9 byte chuẩn,
  khoảng cách tính bằng cm. Cần khớp cấu hình thực tế nếu cảm biến từng đổi baud/đơn vị.
- Chu kỳ gửi 1000 ms; dữ liệu quá 2000 ms bị đánh dấu `stale`.
  `TF03_OVER_RANGE_CM` mặc định 18000; chỉnh theo model/cấu hình TF03 thực tế.

## Biên dịch và nạp

Từ thư mục `esp32-NBIoT`:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\bas_flash.ps1 -Sketch tf03_wifi_mqtts -CompileOnly
powershell -ExecutionPolicy Bypass -File .\windows\bas_flash.ps1 -Sketch tf03_wifi_mqtts -Port COM16
```

Chỉ nạp sau khi xác định đúng ESP32/cổng COM và GPIO nhận cảm biến.
Sketch khởi chạy Wi-Fi tự động, không cần lệnh `MODE WIFI`.
Serial 115200 in trạng thái cảm biến mỗi 2 giây, cùng log kết nối/publish MQTT.
Sketch này không triển khai các lệnh tương tác Serial của `bas_mqtts`.

Payload ví dụ (minh họa, không phải kết quả đo thực tế):

```json
{"distance":1.25,"unit":"m","sensor":"TF03","status":"ok","strength":350,"age_ms":5}
```

`distance` tính bằng mét, 2 số thập phân. Trạng thái `pin_not_configured`,
`no_data`, `stale`, `invalid` luôn gửi `distance: null`.
Chỉ nhận khung đúng header và checksum. Loại số đo cường độ <40, khoảng cách 0
hoặc >= mã ngoài tầm đã cấu hình. Hai byte dự phòng không được coi là nhiệt độ.

Theo dõi topic `MQTT_TOPIC` của config bằng MQTT client để xác minh nhận ở broker.
Log `Publish OK` chỉ xác nhận ghi socket với QoS 0, không chứng minh subscriber nhận được.
Chứng chỉ CA hiện sao chép từ dự án BAS; khi đổi broker cần cập nhật `ca_cert.h`.

Nguồn giao thức: https://en.benewake.com/uploadfiles/2025/12/20251216145317597.pdf

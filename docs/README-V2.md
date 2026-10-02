# ATMC/ESP32 + laser TF03 → Wi‑Fi → MQTT → Web BAS

Tài liệu này hướng dẫn từ đầu cách nối cảm biến laser TF03 với bo ATMC dùng ESP32, nạp firmware, gửi số đo qua Wi‑Fi lên MQTT broker và xem dữ liệu trên trang BAS Serial Monitor. Người chưa từng dùng Arduino/ESP32 có thể làm lần lượt theo các bước bên dưới.

## 1. Kết quả của dự án

Luồng dữ liệu chính:

```text
TF03 --UART--> ATMC/ESP32 --Wi-Fi 2,4 GHz--> MQTT broker
                                                |
                                                +--> Web BAS trên Chrome/Edge

ATMC/ESP32 --USB COM--> Máy tính (chỉ để nạp firmware và xem log Serial)
```

Hai đường hiển thị hoàn toàn khác nhau:

| Đường dữ liệu | Mục đích | Có cần COM11 không? |
|---|---|---|
| USB Serial | Xem log TF03, Wi‑Fi, MQTT; nạp firmware | Có |
| Wi‑Fi → MQTT → Web | Truyền số đo lên server/broker và hiển thị trên web | Không |

Firmware đang dùng:

- Sketch: `firmware/tf03_wifi_mqtts`
- Cảm biến: Benewake TF03, giao tiếp UART nhị phân 9 byte
- UART cảm biến: 115200 baud, 8N1
- Chân nhận đã chạy thực tế: ESP32 GPIO21
- Chân dự phòng khi không có dữ liệu: GPIO22
- Chu kỳ gửi: 1 giây
- MQTT TLS: `broker.emqx.io:8883`
- Topic: `bas/BAS_TEST_001/telemetry`
- Web theo dõi broker qua WSS: `wss://broker.emqx.io:8084/mqtt`

Ngày 26/09/2026, hệ thống đã được kiểm tra trên phần cứng thật: TF03 trả khoảng cách 0,25–0,26 m, ESP32 kết nối Wi‑Fi, kết nối MQTT TLS và subscriber độc lập nhận được bản tin mỗi giây.

## 2. Chuẩn bị

### Phần cứng

- Bo ATMC có ESP32.
- Cảm biến laser TF03 UART và cáp 4 dây.
- Cáp USB có truyền dữ liệu để nối bo với Windows.
- Nguồn phù hợp cho bo ATMC và TF03.
- Máy tính Windows có Wi‑Fi 2,4 GHz hoặc điện thoại/router phát Wi‑Fi 2,4 GHz.

TF03 là cảm biến laser LiDAR/ToF, không phải cảm biến siêu âm.

### Phần mềm trên Windows

1. Cài Arduino IDE 2.x.
2. Trong Arduino IDE, mở **Boards Manager**, cài `esp32 by Espressif Systems` phiên bản `3.3.12`.
3. Mở **Library Manager**, cài `PubSubClient` phiên bản `2.8`.
4. Cài Python 3 và chọn tùy chọn thêm Python vào `PATH`.
5. Dùng Google Chrome hoặc Microsoft Edge. Web Serial không chạy đầy đủ trên Firefox.

Script của dự án tìm Arduino CLI tại:

```text
C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
```

Nếu Arduino IDE được cài ở vị trí khác, sửa biến `$cli` trong `windows/bas_flash.ps1`.

## 3. Nối phần cứng

### 3.1. Tắt nguồn trước khi nối dây

Rút USB và nguồn ngoài trước khi thay đổi dây. Không xác định dây chỉ bằng màu vì màu cáp có thể khác giữa các lô TF03. Hãy dùng nhãn `VCC`, `GND`, `TX`, `RX` trên cảm biến, cáp hoặc sơ đồ bo.

### 3.2. Nối TF03 với ATMC/ESP32

Kết nối đang được firmware sử dụng:

| Chân TF03 | Nối tới ATMC/ESP32 | Chức năng |
|---|---|---|
| VCC | Nguồn cảm biến trên đầu nối ATMC | Cấp nguồn cho TF03 |
| GND | GND của ATMC/ESP32 | Bắt buộc chung mass |
| TX | ESP32 GPIO21 | TF03 gửi khung đo cho ESP32 |
| RX | Để trống trong dự án này | Chỉ cần khi gửi lệnh cấu hình vào TF03 |

Lưu ý:

- Phải nối `TX của TF03 → RX của ESP32`. Không nối TX với TX.
- Firmware hiện chỉ nhận dữ liệu, không phát lệnh sang TF03.
- Không tự cấp VCC từ chân 3,3 V nếu chưa xác nhận điện áp của đúng model TF03 và mạch ATMC. Ưu tiên đầu cấp nguồn cảm biến chuyên dụng trên bo.
- Dữ liệu thực tế đã xác nhận đi vào GPIO21. Nếu phiên bản bo ATMC khác, kiểm tra sơ đồ mạch và sửa `TF03_RX_PIN`.
- GPIO1/GPIO3 là UART USB mặc định; không dùng chúng cho TF03 trong dự án này.

### 3.3. Nối bo với máy tính

1. Cắm cáp USB từ ATMC/ESP32 vào Windows.
2. Mở **Device Manager → Ports (COM & LPT)**.
3. Ghi lại cổng của bo, ví dụ `COM11`.

Cổng COM có thể đổi khi đổi cổng USB. Luôn kiểm tra lại thay vì mặc định mọi máy đều dùng COM11.

## 4. Chuẩn bị Wi‑Fi 2,4 GHz

ESP32-D0WD trong bo chỉ dùng Wi‑Fi 2,4 GHz. Mạng 5 GHz dù cùng tên SSID cũng không thể dùng trực tiếp.

### Cách A: dùng router 2,4 GHz

Xác nhận router đang phát 2,4 GHz và biết đúng SSID/mật khẩu của băng tần này. Nếu router dùng chung tên cho 2,4 và 5 GHz nhưng ESP32 báo lỗi bắt tay WPA2, hãy tạo SSID riêng cho 2,4 GHz.

### Cách B: dùng Mobile Hotspot của Windows

Cách này đã chạy thành công trong lần kiểm thử:

1. Mở **Settings → Network & Internet → Mobile hotspot**.
2. Chọn chia sẻ kết nối Internet từ Wi‑Fi.
3. Chọn **Edit**, đặt tên mạng và mật khẩu riêng.
4. Trong phần băng tần, chọn **2.4 GHz**.
5. Bật **Mobile hotspot**.
6. Giữ Mobile Hotspot bật trong suốt thời gian bo gửi dữ liệu.

Sau khi Windows khởi động lại, có thể phải bật Mobile Hotspot lại. Không ghi mật khẩu thật vào README hoặc đưa `config.h` lên kho mã nguồn công khai.

## 5. Cấu hình firmware

Mở thư mục:

```text
firmware\tf03_wifi_mqtts
```

### 5.1. Tạo file cấu hình riêng

Nếu chưa có `config.h`, mở PowerShell tại thư mục gốc dự án và chạy:

```powershell
Copy-Item .\firmware\tf03_wifi_mqtts\config.example.h `
          .\firmware\tf03_wifi_mqtts\config.h
```

`config.h` đã được `.gitignore` bỏ qua để tránh công khai mật khẩu.

### 5.2. Điền Wi‑Fi

Mở `firmware/tf03_wifi_mqtts/config.h` và sửa:

```cpp
#define WIFI_SSID "TEN_WIFI_2_4_GHZ"
#define WIFI_PASS "MAT_KHAU_WIFI"
```

Không thêm khoảng trắng ngoài ý muốn và giữ nguyên dấu ngoặc kép.

### 5.3. Kiểm tra MQTT

Cấu hình mặc định dùng broker thử nghiệm công cộng:

```cpp
#define DEVICE_ID "BAS_TEST_001"
#define MQTT_TOPIC "bas/" DEVICE_ID "/telemetry"
#define MQTT_HOST "broker.emqx.io"
#define MQTT_PORT 8883
#define MQTT_USER ""
#define MQTT_PASS ""
```

Web và firmware phải dùng chính xác cùng topic. Nếu triển khai thật, nên dùng broker riêng, topic riêng và tài khoản MQTT thay cho broker công cộng.

### 5.4. Kiểm tra chân TF03

Mở `firmware/tf03_wifi_mqtts/tf03_settings.h`:

```cpp
#define TF03_RX_PIN 21
#define TF03_ALT_RX_PIN 22
#define TF03_BAUD 115200
#define TF03_PUBLISH_MS 1000UL
```

Với phần cứng đã kiểm thử, giữ `TF03_RX_PIN 21`. Firmware chỉ luân phiên thử GPIO22 khi không nhận được khung trên GPIO21.

## 6. Biên dịch thử trước khi nạp

Mở PowerShell tại thư mục gốc dự án:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\windows\atmc_tf03_flash.ps1 -CompileOnly
```

Biên dịch thành công sẽ hiển thị dung lượng sketch và không có dòng `Compile failed`.

## 7. Nạp firmware vào ATMC/ESP32

### Cách dễ nhất

1. Đóng Arduino Serial Monitor.
2. Trên trang BAS, nhấn **Ngắt** nếu Web Serial đang giữ COM.
3. Nhấp đúp `atmc-tf03-wifi.cmd`.
4. Nhập cổng COM, ví dụ `COM11`, rồi Enter.
5. Chờ đến khi esptool báo `Hash of data verified` và `Hard resetting via RTS pin`.

### Chạy bằng PowerShell

Thay `COM11` bằng cổng thực tế:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\windows\atmc_tf03_flash.ps1 -Port COM11
```

Script sẽ thực hiện ba việc: biên dịch, nạp firmware và mở log Serial 115200. Nhấn `Ctrl+C` chỉ dừng cửa sổ theo dõi trên máy tính; firmware trên bo vẫn tiếp tục chạy.

## 8. Nhận biết bo đã chạy đúng

Sau khi bo khởi động, log đúng có dạng:

```text
[TF03] RX GPIO21, 115200 baud
[SYS] TF03 -> Wi-Fi -> MQTT; distance in metres, no simulated values
[WF] Wi-Fi: joining "..."
[WF] IP: 192.168.x.x
[WF] clock OK: ...
[WF] MQTT connected to broker.emqx.io:8883
[WF] Publish #1 to bas/BAS_TEST_001/telemetry ...
[WF] Publish OK
```

Một bản tin TF03 thật:

```json
{"distance":0.26,"unit":"m","sensor":"TF03","status":"ok","strength":311,"age_ms":3}
```

Ý nghĩa:

| Trường | Ý nghĩa |
|---|---|
| `distance` | Khoảng cách theo mét |
| `unit` | Đơn vị `m` |
| `sensor` | Nguồn dữ liệu là TF03 |
| `status` | `ok` khi số đo hợp lệ |
| `strength` | Cường độ tín hiệu laser phản xạ |
| `age_ms` | Tuổi của mẫu đo khi đóng gói, tính bằng mili giây |

Di chuyển một vật trước cảm biến. Nếu `distance` thay đổi tương ứng và `status` vẫn là `ok`, TF03 đang hoạt động.

## 9. Mở web BAS

1. Nhấp đúp `bas-monitor.cmd`.
2. Script mở web tại `http://127.0.0.1:8090/` bằng Chrome hoặc Edge.
3. Trong phần **Đối chiếu broker**, giữ topic `bas/BAS_TEST_001/telemetry`.
4. Nhấn **Theo dõi broker**.
5. Xem dữ liệu trong **ATMC · Laser TF03 qua Wi‑Fi** và **Bản tin broker nhận**.

Không cần chọn COM để nhận dữ liệu qua Wi‑Fi. Nút **Chọn COM và kết nối** chỉ mở log Serial để đối chiếu.

Nếu muốn đồng thời xem Serial:

1. Nhấn **Chọn COM và kết nối**.
2. Chọn `USB Serial (COM11)` hoặc cổng đúng của bo.
3. Chọn baud `115200`.
4. Nhấn kết nối.

Khi Web Serial đang kết nối, không thể nạp firmware vì COM bị khóa. Hãy nhấn **Ngắt** trước khi nạp.

## 10. Xác minh broker độc lập

`Publish OK` với MQTT QoS 0 chỉ xác nhận ESP32 đã ghi bản tin vào socket TLS. Để chứng minh broker đã nhận, chạy subscriber độc lập:

```powershell
py .\windows\bas_mqtt_check.py --seconds 15
```

Kết quả đúng:

```text
TLS OK: TLSv1.2 ...
MQTT 3.1.1 connected ...
Subscribed to bas/BAS_TEST_001/telemetry
15:01:07 ... {"distance":0.26,"unit":"m","sensor":"TF03",...}
```

## 11. Trình tự chạy lại hằng ngày

1. Kiểm tra dây TF03 và cấp nguồn cho ATMC.
2. Bật router 2,4 GHz hoặc Mobile Hotspot đã cấu hình trong firmware.
3. Cấp nguồn/USB cho bo; bo tự đọc TF03 và tự kết nối MQTT.
4. Nhấp đúp `bas-monitor.cmd`.
5. Nhấn **Theo dõi broker**.
6. Quan sát khoảng cách trên web. Chỉ mở COM nếu cần xem log.

Không cần nạp lại firmware mỗi lần bật máy, trừ khi đổi Wi‑Fi, chân GPIO, broker hoặc code.

## 12. Xử lý lỗi thường gặp

### `COM11 is busy`, `Access is denied` hoặc `WinError 10048`

- Nhấn **Ngắt** trên BAS Serial Monitor.
- Đóng Arduino Serial Monitor, PuTTY hoặc chương trình đang dùng COM.
- Chỉ một chương trình được mở COM tại một thời điểm.
- `WinError 10048` của web server còn có thể do cổng 8090 đã có một server khác sử dụng.

Nếu cổng 8090 bận, chạy web bằng cổng khác:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\windows\bas_monitor.ps1 -Port 8091
```

Sau đó mở `http://127.0.0.1:8091/`.

### Wi‑Fi báo `reason 15: 4-way handshake failed`

- Kiểm tra lại chính xác SSID và mật khẩu.
- Đảm bảo đó là mạng 2,4 GHz.
- Tạo tên riêng cho mạng 2,4 GHz hoặc dùng Mobile Hotspot 2,4 GHz.
- Sửa `config.h`, ngắt COM và nạp lại firmware.

### TF03 báo `no_data`

- Kiểm tra nguồn TF03 và GND chung.
- Kiểm tra `TX TF03 → GPIO21`.
- Kiểm tra cảm biến đang xuất UART nhị phân 9 byte ở 115200 baud.
- Nếu bo khác phiên bản, xác định GPIO thật rồi sửa `TF03_RX_PIN`.
- Firmware sẽ thử GPIO22, nhưng không tự dò tất cả GPIO.

### `status: "stale"`

ESP32 đã từng nhận dữ liệu nhưng không có khung mới trong hơn 2 giây. Kiểm tra dây TX, nguồn cảm biến và đầu nối lỏng.

### `status: "invalid"`

Khoảng cách bằng 0, ngoài tầm cấu hình hoặc `strength` quá thấp. Hướng TF03 vào bề mặt phản xạ rõ và thử lại.

### Web chỉ thấy `source: "esp32_random"`

Đó là firmware/dữ liệu mô phỏng cũ, không phải TF03. Nạp lại đúng sketch `tf03_wifi_mqtts` bằng `atmc-tf03-wifi.cmd`.

### Web theo dõi broker nhưng không có dữ liệu

- Tìm `MQTT connected` và `Publish OK` trong log Serial.
- Kiểm tra web và firmware dùng cùng topic.
- Nhấn lại **Theo dõi broker**.
- Chạy `bas_mqtt_check.py --seconds 15` để phân biệt lỗi web với lỗi broker.

### NTP lỗi ở lần đầu

Firmware tự thử lại. Nếu lần sau xuất hiện `clock OK`, MQTT TLS sẽ tiếp tục bình thường. Nếu lỗi kéo dài, kiểm tra Windows/router có chia sẻ Internet và UDP 123 hay không.

## 13. Cấu trúc các file chính

| Đường dẫn | Chức năng |
|---|---|
| `firmware/tf03_wifi_mqtts/tf03_wifi_mqtts.ino` | Đọc và kiểm tra khung TF03, tạo JSON |
| `firmware/tf03_wifi_mqtts/wifi.cpp` | Wi‑Fi, NTP, MQTT TLS và publish |
| `firmware/tf03_wifi_mqtts/tf03_parser.h` | Bộ phân tích khung nhị phân TF03 |
| `firmware/tf03_wifi_mqtts/tf03_settings.h` | GPIO, baud, chu kỳ gửi và giới hạn đo |
| `firmware/tf03_wifi_mqtts/config.h` | Cấu hình riêng có mật khẩu; không đưa lên git |
| `firmware/tf03_wifi_mqtts/config.example.h` | Mẫu cấu hình an toàn |
| `windows/atmc_tf03_flash.ps1` | Gọi đúng sketch TF03 để biên dịch/nạp/log |
| `windows/bas_flash.ps1` | Công cụ Arduino CLI dùng chung |
| `windows/bas_monitor/index.html` | Giao diện Serial và MQTT trên web |
| `windows/bas_monitor.ps1` | Chạy web local tại cổng 8090 |
| `windows/bas_mqtt_check.py` | Subscriber TLS xác minh broker nhận dữ liệu |
| `atmc-tf03-wifi.cmd` | Trình nạp dễ dùng trên Windows |
| `bas-monitor.cmd` | Khởi động giao diện BAS |
| `runtime/` | Log Serial theo thời gian; không đưa lên git |

Các thư mục `bas_sim7022_mqtts`, `bas_mqtts` và `legacy` phục vụ thử nghiệm NB‑IoT/SIM7022 trước đây. Quy trình TF03 qua Wi‑Fi trong tài liệu này dùng riêng `tf03_wifi_mqtts`.

## 14. Danh sách kiểm tra hoàn thành

- [ ] TF03 có nguồn và chung GND với ESP32.
- [ ] TX của TF03 nối GPIO21.
- [ ] Windows nhận đúng cổng COM của ESP32.
- [ ] `config.h` chứa đúng Wi‑Fi 2,4 GHz.
- [ ] Biên dịch không lỗi.
- [ ] Nạp xong và flash được xác minh.
- [ ] Serial có `status:"ok"` và khoảng cách thay đổi.
- [ ] Serial có địa chỉ IP.
- [ ] Serial có `MQTT connected`.
- [ ] Serial có `Publish OK` mỗi giây.
- [ ] Web đang theo dõi đúng topic.
- [ ] `bas_mqtt_check.py` nhận được JSON TF03.

## 15. Cảm biến hướng gió ES-WS-04 qua RS485

Firmware riêng cho ES-WS-04 nằm trong `firmware/es_ws_04_rs485`. Việc nạp firmware
này sẽ thay firmware TF03 đang có trên ESP32.

Đấu dây theo datasheet:

| Dây ES-WS-04 | Kết nối ATMC |
|---|---|
| Đỏ | Dương nguồn 10-30 VDC |
| Đen | Âm nguồn/GND |
| Vàng | RS485-A |
| Xanh | RS485-B |

Bo ATMC dùng UART2 `RX=GPIO16`, `TX=GPIO17`; mạch RS485 tích hợp tự đổi chiều.
Cảm biến mặc định có địa chỉ Modbus `1`, hàm `0x03`, thanh ghi góc `0x0000`.
Thiết bị đã kiểm thử phản hồi ở `4800 8N1` và trả góc theo đơn vị 0,1 độ.

Nạp qua COM11:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\windows\atmc_es_ws04_flash.ps1 -Port COM11
```

Sau khi xem log, nhấn `Ctrl+C` để nhả COM11. Mở màn hình la bàn bằng
`es-ws04-monitor.cmd`, nhấn **Chọn COM và kết nối**, chọn COM11. USB Serial dùng
115200 baud; firmware tự thử baud cảm biến 9600, 4800 và 2400.

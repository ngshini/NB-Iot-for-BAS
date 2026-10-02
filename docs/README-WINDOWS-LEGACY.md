# Hệ thống giám sát hướng gió ES-WS-04 qua NB-IoT

Tài liệu này hướng dẫn hoàn chỉnh cách lắp ráp, cấu hình, nạp chương trình và vận hành dự án:

```text
Cảm biến hướng gió ES-WS-04
        │ RS485 Modbus RTU
        ▼
Board ATMC (ESP32)
        │ UART + lệnh AT
        ▼
Module SIM7022 + SIM Viettel NB-IoT
        │ MQTT qua TLS
        ▼
test.mosquitto.org:8883
        │ MQTT qua TLS
        ▼
Python bridge trên Windows
        │ HTTP cục bộ
        ▼
Giao diện BAS Monitor trên trình duyệt
```

Đây là cảm biến **hướng gió RS485**, không phải cảm biến siêu âm. Giá trị cảm biến là góc từ `0.0` đến `359.9` độ và được đổi thành các hướng `N`, `NNE`, `NE`, ..., `NNW`.

## 1. Trạng thái cấu hình đã kiểm tra

Cấu hình hiện tại đã được nạp và kiểm tra trên board kết nối ở COM11:

- ES-WS-04: địa chỉ Modbus `1`, tốc độ `4800 baud`, `8N1`.
- Board đọc cảm biến mỗi `100 ms`.
- SIM7022 giao tiếp với ESP32 ở `115200 baud`.
- SIM Viettel đăng ký NB-IoT thành công với mã mạng `45204`.
- APN đang dùng từ cấu hình lưu trong modem: `v-internet`.
- MQTT TLS: `test.mosquitto.org:8883`.
- Topic: `bas/BAS_WIND_001/telemetry`.
- Board gửi một bản tin mỗi `1000 ms`.
- MQTT client ID có hậu tố MAC riêng của ESP32 để tránh hai thiết bị đá kết nối của nhau.
- BAS Monitor chạy tại `http://127.0.0.1:8090/`.

Trong lần kiểm tra cuối, khi COM11 đã đóng, broker nhận 12 bản tin mới trong 12 giây. Điều này xác nhận dữ liệu đi qua SIM7022/NB-IoT, không đi qua cáp Serial.

## 2. Thiết bị cần chuẩn bị

### Phần cứng

1. Board ATMC sử dụng ESP32.
2. Cảm biến hướng gió RS485 Modbus RTU ES-WS-04, loại bốn dây.
3. Module NB-IoT SIMCom SIM7022.
4. SIM Viettel đã kích hoạt NB-IoT và có dịch vụ dữ liệu.
5. Anten NB-IoT phù hợp, đã gắn chắc vào SIM7022.
6. Nguồn đúng điện áp cho cảm biến và module SIM7022.
7. Cáp USB dữ liệu để nối board ATMC với máy Windows.
8. Dây nối UART và dây RS485.

### Phần mềm trên Windows

1. Windows 10 hoặc Windows 11.
2. Arduino IDE 2.x.
3. ESP32 core của Espressif Systems. Dự án đã kiểm tra với phiên bản `3.3.12`.
4. Thư viện Arduino `PubSubClient`. Dự án đã kiểm tra với phiên bản `2.8`.
5. Python 3 có lệnh `py.exe` hoặc `python.exe` trong `PATH`.
6. Google Chrome hoặc Microsoft Edge.
7. Driver USB-UART của board, ví dụ CP210x nếu board sử dụng chip CP210x.

Python bridge trong dự án chỉ dùng thư viện chuẩn của Python, không cần cài `paho-mqtt`.

## 3. Quy tắc an toàn khi đấu dây

1. Tắt nguồn trước khi cắm hoặc đổi dây.
2. Kiểm tra điện áp ghi trên cảm biến, module và board trước khi cấp nguồn.
3. Không cấp nguồn cho cảm biến hoặc module SIM7022 từ chân `3.3V` nếu thiết bị yêu cầu điện áp hoặc dòng lớn hơn.
4. SIM7022 cần nguồn ổn định; sụt áp khi phát sóng có thể làm modem khởi động lại hoặc mất kết nối.
5. ESP32 và SIM7022 phải nối chung GND.
6. Gắn anten trước khi cho modem hoạt động.
7. Không nối trực tiếp dây RS485 A/B vào GPIO ESP32. Hãy nối vào cổng A/B của bộ chuyển đổi RS485 tích hợp trên board ATMC.

## 4. Kết nối cảm biến ES-WS-04 với board ATMC

Theo bộ dây đang sử dụng trong dự án:

| Dây ES-WS-04 | Chức năng | Kết nối trên board ATMC |
|---|---|---|
| Đỏ | Dương nguồn cảm biến | Cực dương nguồn đúng điện áp của cảm biến |
| Đen | Âm nguồn/GND | Cực âm nguồn/GND |
| Vàng | RS485 A | Chân `A` của cổng RS485 |
| Xanh | RS485 B | Chân `B` của cổng RS485 |

Màu dây có thể khác giữa các lô sản phẩm. Luôn ưu tiên nhãn trên cảm biến và tài liệu của đúng thiết bị. Nếu cảm biến có nguồn nhưng liên tục báo `timeout` hoặc `bad_frame`, tắt nguồn rồi kiểm tra lại A/B; chỉ thử đổi A và B sau khi đã xác nhận nguồn và địa chỉ Modbus.

Cổng RS485 tích hợp của board được firmware sử dụng qua:

```cpp
#define RS485_RX_PIN 16
#define RS485_TX_PIN 17
#define WIND_SENSOR_ADDRESS 1
#define WIND_SENSOR_BAUD 4800
```

Người lắp đặt chỉ cần nối dây cảm biến vào cọc `A`, `B` và nguồn của board; không nối dây vàng/xanh trực tiếp vào GPIO16/17.

## 5. Kết nối SIM7022 với board ATMC

UART phải nối chéo TX và RX:

| SIM7022 | Board ATMC/ESP32 | Ý nghĩa |
|---|---|---|
| `TXD` | GPIO21, `RX` của ESP32 | SIM7022 gửi, ESP32 nhận |
| `RXD` | GPIO22, `TX` của ESP32 | ESP32 gửi, SIM7022 nhận |
| `GND` | `GND` | Chung mass |
| Nguồn module | Nguồn đúng theo carrier SIM7022 | Cấp nguồn ổn định |
| Cổng anten | Anten NB-IoT | Bắt buộc gắn chắc |

Cấu hình firmware:

```cpp
#define MODEM_RX_PIN 21
#define MODEM_TX_PIN 22
#define MODEM_BAUD 115200
```

GPIO16/17 đã dành cho RS485, vì vậy UART của SIM7022 sử dụng GPIO21/22. Nếu board có jumper chọn UART, đặt jumper SIM7022 về đúng hai GPIO này.

Sau khi lắp SIM:

1. Kiểm tra SIM đã kích hoạt NB-IoT.
2. Nếu SIM có mã PIN, cần tắt yêu cầu PIN hoặc bổ sung xử lý PIN trước khi chạy dự án.
3. Chờ đèn mạng của modem đạt trạng thái đăng ký mạng theo tài liệu module.

## 6. Kết nối board với máy tính

1. Dùng cáp USB có truyền dữ liệu, không dùng cáp chỉ sạc.
2. Mở Device Manager → **Ports (COM & LPT)**.
3. Ghi lại cổng của board. Máy đã kiểm tra sử dụng `COM11`.
4. Có thể liệt kê cổng bằng PowerShell:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

Cổng COM chỉ dùng để nạp firmware và xem log kỹ thuật. Dữ liệu trên BAS Monitor được nhận từ MQTT broker; web không cần giữ COM11 mở.

## 7. Cấu trúc các file chính

| File | Chức năng |
|---|---|
| `firmware/bas_mqtts/wind_sensor.cpp` | Gửi yêu cầu Modbus, kiểm tra CRC, đọc góc và đổi góc thành hướng gió |
| `firmware/bas_mqtts/bas_mqtts.ino` | Khởi động ESP32, tạo JSON và chạy các task cảm biến/NB-IoT/Wi-Fi |
| `firmware/bas_mqtts/common.h` | Khai báo cấu trúc dữ liệu và hàm dùng chung |
| `firmware/bas_mqtts/nbiot.cpp` | Điều khiển SIM7022 bằng lệnh AT, đăng ký mạng, TLS, MQTT và publish |
| `firmware/bas_mqtts/wifi.cpp` | Kênh Wi-Fi tùy chọn; mặc định đang tắt |
| `firmware/bas_mqtts/config.h` | Chân GPIO, chu kỳ đọc/gửi, broker, topic, APN và chế độ chạy |
| `firmware/bas_mqtts/ca_cert.h` | Chứng chỉ CA cho Mosquitto TLS |
| `windows/bas_monitor_server.py` | Subscribe MQTT TLS và cung cấp API cục bộ cho giao diện |
| `windows/bas_monitor/index.html` | Giao diện BAS Monitor |
| `windows/bas_monitor.ps1` | Khởi động Python bridge và mở trình duyệt |
| `windows/bas_flash.ps1` | Biên dịch, nạp firmware và đọc log Serial |
| `bas-monitor.cmd` | Lệnh chạy nhanh BAS Monitor |

## 8. Cấu hình firmware

File cấu hình đang dùng là:

```text
firmware/bas_mqtts/config.h
```

Nếu bắt đầu từ một bản sao dự án chưa có `config.h`, chạy:

```powershell
Copy-Item .\firmware\bas_mqtts\config.example.h .\firmware\bas_mqtts\config.h
```

Sau đó kiểm tra các giá trị chính:

```cpp
#define AUTO_START true
#define DEFAULT_CHANNELS CH_NB
#define PUBLISH_INTERVAL_MS 1000UL

#define DEVICE_ID "BAS_WIND_001"
#define MQTT_TOPIC "bas/" DEVICE_ID "/telemetry"

#define NB_MQTT_TLS 1
#define NB_MQTT_HOST "test.mosquitto.org"
#define NB_MQTT_PORT 8883
#define NB_APN ""

#define WIND_SENSOR_ADDRESS 1
#define WIND_SENSOR_BAUD 4800
#define WIND_READ_INTERVAL_MS 100UL
```

Ý nghĩa:

- `AUTO_START true`: board tự chạy sau khi cấp nguồn.
- `DEFAULT_CHANNELS CH_NB`: mặc định chỉ gửi bằng NB-IoT.
- `WIND_READ_INTERVAL_MS 100UL`: đọc cảm biến khoảng 10 lần/giây.
- `PUBLISH_INTERVAL_MS 1000UL`: gửi MQTT khoảng một lần/giây.
- `NB_APN ""`: sử dụng PDP/APN đã lưu trong SIM7022. Modem thử nghiệm đang dùng `v-internet`.
- `WIFI_SSID` và `WIFI_PASS` có thể để `CHANGE_ME` vì kênh Wi-Fi đang tắt.

Không đặt publish ở 100 ms cho SIM7022. Một lần publish cần nhiều lệnh AT (`CMQTTTOPIC`, `CMQTTPAYLOAD`, `CMQTTPUB`); tốc độ quá cao có thể làm socket đóng và sinh lỗi 26.

## 9. Cài Arduino IDE và thư viện

1. Cài Arduino IDE 2.x vào vị trí mặc định.
2. Mở Arduino IDE → Boards Manager.
3. Tìm **esp32 by Espressif Systems** và cài phiên bản `3.3.12` hoặc phiên bản tương thích đã kiểm tra.
4. Mở Library Manager.
5. Tìm và cài `PubSubClient` phiên bản `2.8`.
6. Khởi động lại Arduino IDE sau khi cài nếu CLI chưa nhận board hoặc thư viện.

Script nạp firmware đang gọi Arduino CLI tại:

```text
C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
```

Nếu Arduino IDE được cài ở vị trí khác, sửa biến `$cli` trong `windows/bas_flash.ps1`.

## 10. Biên dịch firmware

Mở PowerShell tại thư mục gốc `NB-Iot-for-BAS`:

```powershell
cd "C:\Users\dell\Desktop\NBIoT V3\NB-Iot-for-BAS"
```

Biên dịch mà chưa nạp:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bas_flash.ps1 `
  -Sketch bas_mqtts -CompileOnly
```

Kết quả thành công sẽ có thông tin dung lượng chương trình và không có dòng `Compile failed`.

## 11. Nạp firmware vào board

Trước khi nạp:

1. Đóng Arduino Serial Monitor.
2. Trên BAS Monitor, bấm **Ngắt** nếu COM11 đang kết nối.
3. Đóng mọi chương trình đang giữ COM11.
4. Giữ nguyên nguồn SIM7022 và dây cảm biến.

Nạp qua COM11 và ghi log trong 90 giây:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bas_flash.ps1 `
  -Sketch bas_mqtts -Port COM11 -Seconds 90
```

Quá trình đúng sẽ xuất hiện:

```text
Connected to ESP32 on COM11
Hash of data verified
Hard resetting via RTS pin
```

Nếu chỉ muốn nạp và tự dừng phần log, dùng một số giây nhỏ hơn. Có thể nhấn `Ctrl+C` sau khi nạp xong.

Sau khi đóng Serial, chờ khoảng 30–60 giây để SIM7022 hoàn tất TLS/MQTT. Việc mở cổng Serial trên một số board ESP32 có thể làm board reset; vì vậy không giữ COM mở trong phép thử NB-IoT cuối.

## 12. Đọc log để kiểm tra từng tầng

Log Serial dùng tốc độ `115200 baud`.

### Cảm biến hoạt động

```text
[SENSOR] ES-WS-04 angle=201.1 deg direction=SSW responses=... errors=0
```

`errors=0` cho biết các khung Modbus đang hợp lệ.

### SIM sẵn sàng

```text
[NB] < +CPIN: READY
```

### Đăng ký NB-IoT thành công

```text
[NB] < +CEREG: 0,1
[NB] registered (home)
```

`0,5` cũng là trạng thái đã đăng ký, nhưng ở chế độ roaming.

### PDP và địa chỉ IP hoạt động

```text
[NB] < +CGATT: 1
[NB] < +CGACT: 0,1
[NB] IP: ...
```

### MQTT kết nối thành công

```text
[NB] MQTT connected to test.mosquitto.org:8883 over TLS
```

### Gửi bản tin

```text
[NB] Publish #... to bas/BAS_WIND_001/telemetry (... bytes): {...}
[NB] Publish OK
```

Với QoS 0, `Publish OK` xác nhận modem đã gửi. Bằng chứng chắc chắn broker nhận là bản tin xuất hiện trong phần **BẢN TIN BROKER NHẬN** của BAS Monitor.

## 13. Chạy BAS Monitor

Không mở trực tiếp `index.html` bằng địa chỉ `file:///...`. Hãy chạy server của dự án:

```powershell
.\bas-monitor.cmd
```

Hoặc:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bas_monitor.ps1
```

Script sẽ:

1. Dừng tiến trình cũ đang giữ cổng `8090`.
2. Khởi động `windows/bas_monitor_server.py`.
3. Kết nối TLS tới `test.mosquitto.org:8883`.
4. Subscribe topic `bas/BAS_WIND_001/telemetry`.
5. Mở Chrome hoặc Edge tại `http://127.0.0.1:8090/`.

Trên giao diện:

1. Kiểm tra trạng thái cầu MQTT là **Đang theo dõi qua MQTT TLS cục bộ**.
2. Xem khung **ATMC - CẢM BIẾN HƯỚNG GIÓ QUA NB-IoT**.
3. Xem góc, hướng gió, trạng thái và tuổi dữ liệu.
4. Xem phần **BẢN TIN BROKER NHẬN** để xác nhận broker đã nhận JSON.
5. Không cần chọn COM11 để xem dữ liệu NB-IoT.

Nếu vừa cập nhật giao diện, nhấn `Ctrl+F5` để trình duyệt tải lại toàn bộ file mới.

## 14. Phân biệt dữ liệu Serial và dữ liệu NB-IoT

### Kênh Serial

```text
ESP32 → cáp USB → COM11 → trình duyệt/PowerShell
```

Serial chỉ dùng cho log kỹ thuật và chẩn đoán. Nếu chọn COM11, khung **LOG SERIAL** có thể hiển thị dữ liệu cảm biến ngay cả khi MQTT chưa hoạt động.

### Kênh NB-IoT

```text
ESP32 → UART → SIM7022 → mạng Viettel NB-IoT
→ MQTT broker → Python bridge → BAS Monitor
```

Cách kiểm tra độc lập:

1. Để cáp USB cấp nguồn cho board nhưng đóng mọi kết nối COM11.
2. Chạy `bas-monitor.cmd`.
3. Không bấm chọn COM.
4. Quan sát phần **BẢN TIN BROKER NHẬN**.
5. Nếu bản tin mới tiếp tục xuất hiện mỗi giây, dữ liệu chắc chắn đến qua NB-IoT/MQTT.

## 15. Dạng dữ liệu gửi lên MQTT

Ví dụ payload:

```json
{
  "sensor": "ES-WS-04",
  "angle": 201.1,
  "windDirection": "SSW",
  "status": "ok",
  "raw": 2011,
  "unit": "deg",
  "age_ms": 20
}
```

| Trường | Ý nghĩa |
|---|---|
| `sensor` | Tên cảm biến |
| `angle` | Góc hướng gió theo độ |
| `windDirection` | Hướng gió 16 phương |
| `status` | `ok` khi dữ liệu hợp lệ |
| `raw` | Giá trị Modbus theo đơn vị 0,1 độ |
| `unit` | Đơn vị `deg` |
| `age_ms` | Tuổi mẫu cảm biến khi tạo bản tin |

## 16. Các lệnh Serial hỗ trợ

Khi thực sự cần chẩn đoán, mở COM11 ở `115200 baud` và có thể gửi:

| Lệnh | Tác dụng |
|---|---|
| `STATUS` | In cấu hình và trạng thái hiện tại |
| `PUBLISH` | Yêu cầu gửi ngay một bản tin |
| `MODE NB` | Chỉ bật kênh NB-IoT |
| `MODE WIFI` | Chỉ bật kênh Wi-Fi |
| `MODE BOTH` | Bật đồng thời hai kênh |
| `STOP` | Dừng các kênh gửi |
| `PAYLOAD FULL` | Dùng payload đầy đủ |
| `PAYLOAD MIN` | Dùng payload rút gọn |

Sau khi chẩn đoán, đóng COM và chờ modem kết nối lại trước khi đánh giá đường NB-IoT.

## 17. Xử lý lỗi thường gặp

### Không thấy COM11

- Thử cáp USB dữ liệu khác.
- Đổi cổng USB.
- Cài driver USB-UART đúng loại.
- Kiểm tra Device Manager.
- Chạy `[System.IO.Ports.SerialPort]::GetPortNames()`.

### Upload báo cổng đang được sử dụng

Thông báo thường gặp:

```text
Access is denied
The port COM11 is busy
```

Đóng BAS Serial, Arduino Serial Monitor, PuTTY và các terminal khác rồi nạp lại.

### Cảm biến báo `timeout`

Kiểm tra theo thứ tự:

1. Nguồn cảm biến.
2. Dây GND.
3. Vàng vào A và xanh vào B theo bộ cảm biến hiện tại.
4. Địa chỉ Modbus là `1`.
5. Baud là `4800`.
6. Sau khi tắt nguồn, thử đổi A/B nếu tài liệu của đúng lô cảm biến quy định ngược lại.

### Cảm biến báo `bad_crc`

- Kiểm tra A/B có tiếp xúc kém hay không.
- Rút ngắn dây khi thử nghiệm.
- Tách dây tín hiệu khỏi dây nguồn gây nhiễu.
- Kiểm tra nguồn cảm biến ổn định.
- Kiểm tra không có hai thiết bị cùng địa chỉ trên bus.

### `CPIN` không phải `READY`

- Kiểm tra chiều lắp SIM.
- Tắt yêu cầu PIN bằng điện thoại hoặc bổ sung xử lý PIN.
- Kiểm tra tiếp xúc khe SIM.

### `CEREG` chưa phải 1 hoặc 5

- Kiểm tra anten.
- Đưa thiết bị đến nơi có phủ sóng NB-IoT.
- Kiểm tra SIM Viettel đã kích hoạt dịch vụ.
- Chờ thêm vài phút sau khi cấp nguồn lần đầu.

### Không có IP hoặc `CGATT: 0`

- Kiểm tra APN trong modem.
- Với cấu hình đã thử nghiệm, APN là `v-internet`.
- Kiểm tra gói dữ liệu và trạng thái SIM.
- Khởi động lại nguồn SIM7022.

### MQTT lỗi 26 hoặc `socket is closed by server`

Lỗi có thể xuất hiện tạm thời ngay sau khi COM làm ESP32 reset trong khi SIM7022 còn giữ phiên MQTT cũ.

1. Đóng COM11.
2. Không reset liên tục.
3. Chờ 30–60 giây để firmware dọn phiên cũ và kết nối lại.
4. Kiểm tra bản tin trực tiếp ở mục broker.
5. Giữ chu kỳ publish ở `1000 ms` hoặc chậm hơn.

Firmware hiện tạo client ID riêng từ MAC, ví dụ `bas-BAS_WIND_001-E342A8`, để tránh trùng client ID.

### BAS Monitor báo lỗi WebSocket hoặc không có dữ liệu

Phiên bản hiện tại không phụ thuộc WebSocket 8081 của trình duyệt. Python bridge kết nối MQTT TLS trực tiếp rồi cung cấp `/api/broker` trên cổng 8090.

- Chạy lại `bas-monitor.cmd` thay vì mở `index.html` trực tiếp.
- Kiểm tra Python 3.
- Kiểm tra máy Windows có Internet.
- Nhấn `Ctrl+F5`.
- Kiểm tra cửa sổ PowerShell có báo `MQTT TLS bridge` hay không.

### Lỗi cổng 8090 đã được sử dụng

`windows/bas_monitor.ps1` sẽ tự dừng tiến trình cũ đang lắng nghe cổng 8090. Nếu vẫn lỗi, đóng cửa sổ BAS Monitor cũ rồi chạy lại `bas-monitor.cmd`.

## 18. Quy trình chạy lại nhanh

Sau khi phần mềm đã được cài và firmware đã nạp:

1. Tắt nguồn toàn bộ.
2. Kiểm tra dây đỏ/đen/A/B của ES-WS-04.
3. Kiểm tra TX/RX chéo, GND, nguồn và anten của SIM7022.
4. Lắp SIM Viettel.
5. Cấp nguồn cho cảm biến, SIM7022 và board ATMC.
6. Chờ 30–60 giây.
7. Đóng mọi chương trình đang dùng COM11.
8. Chạy `bas-monitor.cmd`.
9. Mở `http://127.0.0.1:8090/` nếu trình duyệt không tự mở.
10. Kiểm tra trạng thái MQTT bridge.
11. Quan sát góc và hướng gió cập nhật khoảng mỗi giây.
12. Xác nhận bản tin xuất hiện trong **BẢN TIN BROKER NHẬN**.

## 19. Tiêu chí nghiệm thu

Dự án được xem là hoạt động đúng khi đạt đủ các điều kiện:

- [ ] Cảm biến có nguồn và lắp đúng A/B.
- [ ] Log cảm biến có `status=ok` hoặc `errors=0`.
- [ ] SIM báo `CPIN: READY`.
- [ ] SIM báo `CEREG: 0,1` hoặc `0,5`.
- [ ] SIM có `CGATT: 1`, PDP active và địa chỉ IP.
- [ ] MQTT báo kết nối thành công.
- [ ] BAS Monitor báo cầu MQTT đang kết nối.
- [ ] Góc và hướng gió thay đổi trên giao diện.
- [ ] Bản tin broker tiếp tục tăng khi COM11 đã đóng.

## 20. Giới hạn và lưu ý triển khai thật

- `test.mosquitto.org` là broker công cộng dùng để thử nghiệm. Không gửi thông tin bí mật lên topic công cộng.
- Khi triển khai chính thức, nên dùng broker riêng, tài khoản riêng, ACL, topic riêng và quản lý chứng chỉ TLS.
- QoS hiện là `0`; giao diện broker là nơi xác nhận bản tin thực sự đã đến broker.
- Máy Windows cần Internet để nhận dữ liệu từ broker và hiển thị trên BAS Monitor.
- Board vẫn tự gửi NB-IoT khi máy tính không mở COM; BAS Monitor chỉ là bên nhận và hiển thị.
- Đọc cảm biến 100 ms và gửi MQTT 1 giây là hai chu kỳ độc lập. Mỗi bản tin gửi giá trị cảm biến mới nhất.

## 21. Tài liệu bổ sung

- `docs/NB_TLS_TEST.md`: cấu hình và kiểm thử TLS của SIM7022.
- `docs/SIM7022_Series_AT_Command_Manual_V1.05.pdf`: lệnh AT SIM7022.
- `docs/SIM7022_Series_SSL_Application_Note_V1.00.pdf`: hướng dẫn SSL/TLS SIM7022.
- `docs/INTEGRATION-V2.md`: lịch sử tích hợp các phần của dự án.
- `firmware/es_ws_04_rs485/README.md`: chương trình thử riêng ES-WS-04.


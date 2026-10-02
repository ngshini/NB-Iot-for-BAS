# BAS ESP32 — cảm biến thật qua NB-IoT / Wi-Fi

Tài liệu cập nhật ngày 02/10/2026 cho chương trình chính `firmware/bas_mqtts/`. Hướng dẫn Windows cũ được giữ tại `docs/README-WINDOWS-LEGACY.md`; không dùng cấu hình COM hoặc topic cũ để kết luận trạng thái bản hiện tại.

## 1. Source này làm gì?

ESP32 đọc cảm biến hướng gió ES-WS-04 qua RS485 Modbus RTU, đóng gói dữ liệu thật thành JSON và gửi MQTT. Có hai nhánh truyền: SIM7022 qua NB-IoT và ESP32 qua Wi-Fi. Trang public nhận dữ liệu để quan sát, không cần cắm USB vào máy người xem.

Luồng NB-IoT đang sử dụng:

```text
ES-WS-04 → RS485 → ESP32 → JSON
                            ↓ UART / SIM7022
                    test.mosquitto.org:8883 (MQTT TLS)
                            ↓ MQTT TLS cổng 8886
                    Relay nhận trên Ubuntu
                            ↓ SSE qua HTTPS /iot-test/events
                    Trang public /iot-test/
```

Nhánh Wi-Fi hiện dùng `broker.emqx.io:8883`, không phải broker Mosquitto của nhánh NB-IoT. Muốn hai nhánh xuất hiện cùng một luồng nhận, phải thống nhất broker và cấu hình trang nhận tương ứng.

## 2. Những gì đã hoạt động và giới hạn

| Hạng mục | Trạng thái kiểm tra |
| --- | --- |
| Đọc ES-WS-04 thật | Đã quan sát góc thay đổi khi xoay cảm biến; kiểm tra CRC và phạm vi giá trị |
| SIM7022 đăng ký mạng, có IP | Đã kiểm tra trên thiết bị thực tế |
| NB-IoT MQTT TLS và publish | Đã nhận bản tin thật bằng subscriber độc lập và trang public |
| Relay MQTT → SSE | Đã chạy trên Ubuntu, có kết nối lại khi mất kết nối |
| Tốc độ cập nhật | Bản hiện tại đọc mỗi 100 ms, đặt lịch gửi mỗi 1 giây; mẫu 15 bản tin đo khoảng cách trung bình khoảng 1006 ms |
| Wi-Fi MQTT | Có code; chưa xác nhận end-to-end trên mạng Wi-Fi hiện tại |
| Cảm biến khác | Chưa có driver tích hợp vào chương trình chính; có tên trường trên web không đồng nghĩa thiết bị đã đọc được |
| Lưu vào cơ sở dữ liệu BAS | Luồng test độc lập không tự ghi vào database BAS |

Kết quả trên là kiểm chứng tại thời điểm test, không bảo đảm broker công cộng hoặc mạng di động luôn ổn định. Mosquitto test là broker công cộng, không dùng dữ liệu nhạy cảm hoặc làm hệ thống production cần xác thực thiết bị.

## 3. Các file cần biết

| File/thư mục | Vai trò |
| --- | --- |
| `firmware/bas_mqtts/bas_mqtts.ino` | Khởi tạo các task; `buildPayload()` đóng gói dữ liệu cảm biến |
| `firmware/bas_mqtts/wind_sensor.cpp` | Đọc RS485, kiểm tra Modbus/CRC, góc và độ mới của mẫu |
| `firmware/bas_mqtts/common.h` | Kiểu dữ liệu và giao diện chia sẻ giữa các task |
| `firmware/bas_mqtts/nbiot.cpp` | Điều khiển SIM7022 bằng AT, TLS/MQTT, gửi và thử kết nối lại |
| `firmware/bas_mqtts/wifi.cpp` | Wi-Fi, TLS và MQTT bằng PubSubClient |
| `firmware/bas_mqtts/config.example.h` | Cấu hình mặc định đang dùng khi chưa có `config.h` |
| `firmware/bas_mqtts/nb_tls_profile.h` | Chọn broker và CA tương ứng cho NB-IoT |
| `firmware/tests/` | Kiểm tra cấu hình và profile TLS |
| `server-test/bridge.cjs` | Relay nhận topic cố định và chuyển bản tin nguyên gốc sang SSE |
| `server-test/receiver.mjs` | Phần nhận dữ liệu trên trình duyệt, có nhánh SSE cho domain public |
| `server-test/README.md` | Ghi chú triển khai relay |
| `TEST-HUONG-GIO-THAT.md` | Hướng dẫn kiểm tra cảm biến thật |
| `windows/` | Công cụ monitor và hướng dẫn Windows; một số cấu hình/nhãn còn thuộc bản cũ |

Các sketch khác trong thư mục không phải chương trình chính này; có thể có cấu hình hoặc dữ liệu thử khác. Không nạp nhầm sketch khi test cảm biến thật.

## 4. Cấu hình và kết nối hiện tại

| Thông số | Giá trị |
| --- | --- |
| ESP32 RS485 RX / TX | GPIO 16 / GPIO 17 |
| ES-WS-04 | Slave 1, 4800 baud, Modbus RTU |
| Lệnh đọc | Function 03, thanh ghi 0 |
| Giá trị góc | Raw 0–3599, chia 10 để ra độ |
| Bù góc | `WIND_ANGLE_OFFSET_DEG=0` |
| ESP32 UART modem RX / TX | GPIO 21 / GPIO 22, 115200 baud |
| Chu kỳ đọc | `WIND_READ_INTERVAL_MS=100` |
| Timeout đọc | `WIND_RESPONSE_TIMEOUT_MS=350` |
| Mẫu quá cũ | `WIND_STALE_MS=3000` |
| Chu kỳ publish | `PUBLISH_INTERVAL_MS=1000` |
| Device ID | `BAS_TEST_001` |
| Topic | `bas/BAS_TEST_001/telemetry` |
| NB-IoT profile | Profile 2: Mosquitto, TLS cổng 8883 và CA tương ứng |
| Wi-Fi mặc định | EMQX, TLS cổng 8883 |

RS485 A/B phải đi qua bộ chuyển đổi RS485 của board, không nối trực tiếp A/B vào GPIO. Kiểm tra nguồn, mass và sơ đồ board trước khi đổi dây; không suy ra chân module chịu được 5 V chỉ vì nguồn đầu vào board là 5 V. Driver hiện tại giả định bộ RS485 tự điều khiển chiều truyền/nhận; phần cứng cần DE/RE riêng phải bổ sung điều khiển.

Nếu tạo `firmware/bas_mqtts/config.h`, cấu hình riêng sẽ thay cho `config.example.h`. Không đưa mật khẩu Wi-Fi hoặc MQTT vào kho source chia sẻ. Với NB-IoT, profile trong `nb_tls_profile.h` quyết định hostname/CA; chỉ sửa `MQTT_HOST` chưa chắc đổi được broker NB-IoT.

`AUTO_START=false`: sau khởi động firmware chờ lệnh chọn kênh. Nếu bật tự chạy, cần kiểm tra lựa chọn kênh đã lưu từ lần trước, không giả định luôn chạy NB-IoT.

## 5. JSON thiết bị đang gửi

Ví dụ một mẫu hợp lệ:

```json
{
  "sensor": "ES-WS-04",
  "angle": 217.8,
  "windDirection": "SW",
  "status": "ok",
  "raw": 2178,
  "unit": "deg",
  "age_ms": 66
}
```

`angle` là góc sau bù hướng; `windDirection` là tên hướng quy đổi; `raw` là góc theo phần mười độ sau xử lý. `age_ms` là tuổi mẫu tại lúc tạo payload, không phải độ trễ mạng.

Nếu không có mẫu hợp lệ hoặc mẫu quá cũ, các giá trị đo được gửi `null` và `status` mô tả lỗi; không dùng số 0 thay lỗi vì 0° là hướng hợp lệ. Mẫu hợp lệ gần nhất có thể tiếp tục được dùng trong thời gian chưa vượt ngưỡng stale.

Các lệnh `PAYLOAD MIN/FULL` và một số nhãn giao diện là phần tương thích cũ. `buildPayload()` hiện gửi dữ liệu hướng gió thật, không chuyển sang tạo số đo giả khi đổi nhãn payload. Đọc mỗi 100 ms không có nghĩa web nhận mỗi 100 ms: lịch gửi là 1 giây, còn phụ thuộc mạng, TLS và broker.

## 6. Build, nạp và vận hành trên macOS

Cần Arduino ESP32 core và thư viện PubSubClient. Có thể dùng Arduino IDE hoặc CLI đi kèm IDE. Kiểm tra đúng board và cổng trước khi nạp; đóng các monitor đang giữ cổng USB.

```sh
cd /Users/shini/Documents/project/BAS-ESP32-NB-IoT-WiFi-source-20261002
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$CLI" board list
"$CLI" compile --fqbn esp32:esp32:esp32:UploadSpeed=115200 firmware/bas_mqtts
"$CLI" upload --fqbn esp32:esp32:esp32:UploadSpeed=115200 --port /dev/cu.usbserial-130 firmware/bas_mqtts
"$CLI" monitor --port /dev/cu.usbserial-130 --config baudrate=115200,dtr=off,rts=off
```

`/dev/cu.usbserial-130` là cổng đã dùng trong lần test, có thể đổi khi cắm thiết bị khác. Thay bằng cổng thực tế từ `board list`.

Trong Serial monitor gửi lệnh với kết thúc dòng:

```text
STATUS
MODE NB
PUBLISH
STOP
MODE WIFI
MODE BOTH
```

`MODE NB` bắt đầu nhánh SIM; `PUBLISH` yêu cầu gửi; `STOP` dừng truyền. Wi-Fi cần SSID/mật khẩu đúng trước khi chạy. Hai nhánh dùng client ID riêng; không cho nhiều thiết bị dùng chung MQTT client ID vì broker có thể ngắt phiên cũ.

Để xem public, mở https://server.aitrg.io.vn/iot-test/?broker=mosquitto#test/wind-direction và bật nhận MQTT nếu trang chưa theo dõi. Xoay cảm biến, kiểm tra góc và thời điểm cập nhật thay đổi.

## 7. Trang local 8090 khác trang public thế nào?

`http://127.0.0.1:8090/` là monitor kỹ thuật trên máy của bạn: kết nối USB Serial, xem log, chọn kênh, reset và gửi lệnh. Trang public dùng MQTT/SSE để xem dữ liệu, không điều khiển USB từ xa.

Một số cấu hình monitor cũ còn topic `BAS_WIND_001`, trong khi firmware này dùng `BAS_TEST_001`. Phải khớp broker/topic trước khi dùng số đếm của monitor để kiểm tra. Các bảng trạng thái cũ không tự chứng minh firmware mới đang chạy đúng.

Luồng public test giữ lịch sử trong phiên trình duyệt, không phải kho dữ liệu lâu dài. Tải lại trang có thể mất lịch sử; chưa có chức năng tự lưu toàn bộ bản tin vào database.

## 8. Thêm một loại cảm biến mới

Không chỉ thêm tên trường vào cấu hình: cần driver đọc thật, payload đúng và web hiểu trường đó nếu muốn có ô hiển thị riêng.

### Bước 1 — xác định phần cứng và giao thức

Ghi rõ model, điện áp, giao thức (RS485 Modbus/analog/I²C/xung…), địa chỉ, baud, thanh ghi, hệ số quy đổi và đơn vị theo tài liệu cảm biến. Không dùng trùng GPIO 16/17 hoặc 21/22 cho chức năng khác.

Nếu thêm cảm biến Modbus chung bus, đặt địa chỉ slave khác và dùng một bộ điều phối đọc bus. Không cho hai task gửi đồng thời trên cùng UART RS485 vì sẽ trộn phản hồi.

### Bước 2 — viết driver và lưu mẫu

Thêm module riêng, chẳng hạn `wind_speed_sensor.cpp`, khai báo mẫu và hàm truy cập trong `common.h`, khởi tạo hoặc lên lịch đọc trong sketch chính. Driver cần kiểm tra CRC/địa chỉ, timeout, phạm vi, đơn vị và tuổi mẫu. Dùng khóa phù hợp khi chia sẻ dữ liệu giữa task đọc và task MQTT.

Đầu tiên xác nhận log thay đổi theo tác động thật lên cảm biến; chưa cần MQTT. Khi tháo cảm biến hoặc mất phản hồi phải báo lỗi/stale, không phát sinh số giả.

### Bước 3 — bổ sung vào `buildPayload()`

Ví dụ cấu trúc cho hai cảm biến hướng và tốc độ gió (chỉ minh họa schema, không phải driver đã có):

```json
{
  "sensor": "BAS-WIND-COMBINED",
  "windDirection": 217.8,
  "windSpeed": 3.2,
  "status": "ok",
  "windDirectionStatus": "ok",
  "windSpeedStatus": "ok",
  "age_ms": 66
}
```

Điền bằng mẫu thật; gửi `null` cho trường lỗi, không gửi `NaN` hoặc số kèm chữ đơn vị. Kiểm tra kích thước buffer JSON và giới hạn gói MQTT Wi-Fi khi thêm nhiều trường.

Web hiện chặn số đo khi `status` tổng khác `ok`. Nếu một cảm biến lỗi nhưng các cảm biến khác vẫn tốt, cần thống nhất xử lý trạng thái từng cảm biến trên web; các trường `...Status` trong ví dụ chưa tự tạo cảnh báo riêng. Không gắn nhãn ES-WS-04 cho cảm biến khác.

### Bước 4 — giữ hoặc thay topic có chủ đích

Thêm trường trên cùng thiết bị và giữ `bas/BAS_TEST_001/telemetry`: relay có thể chuyển nguyên JSON mới mà không cần thêm driver trên server.

Thêm thiết bị hoặc đổi topic: cần đổi device/client ID firmware, danh sách topic cho phép trong `server-test/bridge.cjs`, giới hạn topic trong receiver và lựa chọn trên web, rồi triển khai lại phần server tương ứng. Relay hiện chỉ nhận topic test cố định; không tự nhận mọi topic mới.

### Bước 5 — kiểm tra mapping giao diện

| Trường JSON | Web đang hiểu |
| --- | --- |
| `sensor: ES-WS-04` + `angle` | Góc hướng gió theo độ |
| `windDirection` | Góc dạng số hoặc tên hướng như N, NE, SW |
| `windSpeed` / `speed_mps` | Tốc độ gió m/s |
| `distance` / `bowDistance` | Khoảng cách 1 |
| `sternDistance` | Khoảng cách 2 |
| `waterLevel`, `bowSpeed`, `sternSpeed`, `waterFlow`, `vesselSpeed`, `vesselHeading` | Có mapping số; cần kiểm tra đúng đơn vị của ô đích |
| Trường chưa có mapping | Có thể xem trong JSON/thông tin thô, chưa tự có biểu đồ hay ô riêng |

Khoảng cách hiện hỗ trợ đơn vị m/cm/mm và quy đổi về cm. Không dùng chung `unit: deg` cho payload vừa có góc vừa có khoảng cách; cần schema đơn vị từng trường và sửa normalizer tương ứng. `windForce` không tự được hiểu là tốc độ gió.

Source giao diện đầy đủ hiện ở `/Users/shini/Documents/project/esp32-NBIoT/windows/bas_receive_test/`, đặc biệt `src/telemetry.mjs` và phần giao diện. Thư mục firmware này có các file relay/receiver nhưng không thay thế toàn bộ dự án web. Nếu thêm loại số đo mới cần ô hiển thị riêng, phải sửa và build/deploy web đó.

### Bước 6 — kiểm thử đủ luồng

1. Build và nạp thành công đúng board.
2. Tác động cảm biến thật, thấy số đọc local thay đổi đúng.
3. Kiểm tra JSON: giá trị, đơn vị, trạng thái, độ mới.
4. Subscriber độc lập nhận đúng broker/topic.
5. Trang public hiển thị đúng góc/đơn vị/thời điểm.
6. Ngắt cảm biến: web không tiếp tục báo số cũ là dữ liệu mới hợp lệ.
7. Mất mạng và kết nối lại: thiết bị phục hồi; đo khoảng cách bản tin thực tế.

Chỉ có log `publish OK` chưa đủ chứng minh web đã nhận đúng. Chỉ thấy JSON trên web cũng chưa đủ chứng minh số đo đúng với cảm biến.

## 9. Phần server đã triển khai

Server Ubuntu dùng thư mục `/home/croz/apps/bas-beta/`; các file triển khai test đặt dưới `deployments/iot-20261002/`. Container relay tên `bas-iot-relay` trên network `bas_default`. Frontend phục vụ `/iot-test/` và proxy SSE `/iot-test/events` tới relay cổng nội bộ 18093, tắt buffering để chuyển bản tin liên tục.

Relay chỉ subscribe, không điều khiển thiết bị, không ghi database và không mở cổng MQTT inbound trên router. Device và relay chủ động kết nối ra broker công cộng; public HTTPS dùng đường server/Cloudflare đã có.

Relay hiện được chạy riêng, chưa tích hợp đầy đủ vào Compose. Rebuild frontend hoặc tạo lại network có thể làm mất route SSE hoặc kết nối relay nếu không mang cấu hình này theo. `server-test/Dockerfile.frontend-relay` phụ thuộc image backup trên server, không phải Dockerfile độc lập dùng trên mọi máy.

Image backup trước relay: `bas-frontend:before-relay-20261002`; image frontend triển khai: `bas-frontend:latest`. Xem hướng dẫn ở `server-test/README.md` trước khi cập nhật server. Thay firmware không tự cập nhật web/server; không cần thay `.env.production` của BAS chỉ để thêm một trường JSON trong luồng test này.

## 10. Kiểm tra source không cần nạp

Chạy từ thư mục dự án trên macOS có clang++ và Node.js:

```sh
clang++ -std=c++17 firmware/tests/wind_config_test.cpp -o /tmp/bas-wind-config-test
/tmp/bas-wind-config-test
clang++ -std=c++17 firmware/tests/nb_tls_test.cpp -o /tmp/bas-nb-tls-test
/tmp/bas-nb-tls-test
node --test server-test/bridge.test.cjs
```

Các test này kiểm tra cấu hình/profile và việc giữ payload của relay; không thay thế test phần cứng, chứng chỉ/broker trực tiếp hoặc cảm biến mới.

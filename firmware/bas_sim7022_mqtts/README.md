# ESP32 + SIM7022 → BAS qua MQTT TLS

Firmware gửi telemetry JSON tới `broker.emqx.io:8883` (MQTT over TLS) bằng **stack MQTT(S) bên trong SIM7022**. ESP32 chỉ gửi lệnh AT qua UART: không Wi-Fi, không PPP, không thư viện MQTT. Sketch cũ `firmware/sim7022_mqtt` (MQTT thường qua ngrok) được giữ nguyên.

Bộ lệnh lấy từ tài liệu chính hãng SIMCom, bản sao trong `docs/`:

- *SIM7022 Series AT Command Manual V1.05*: chương 7 (SSL) và chương 8 (MQTT(S)). Nhóm `AT+CMQTT*` có từ V1.04 (04/2022).
- *SIM7022 Series SSL Application Note V1.00*.

Bản *SIM7022 MQTT(S) Application Note V1.03* [trên simcom.com](https://www.simcom.com/product/SIM7022.html) chưa tải được từ máy này. Nên đối chiếu thêm nếu lấy được. AT Manual V1.00 cũ **không có MQTT TLS** (chỉ có `AT$QCMT*` qua TCP thường). Vì vậy firmware kiểm tra khả năng của modem ngay khi khởi động, xem mục 6.

## Kết quả trên phần cứng thật (24/09/2026)

| Mục | Kết quả đo |
|---|---|
| Board ESP32 | ESP32 DevKit, chip ESP32-D0WD-V3 rev 3.1, 4 MB flash, CP210x (COM6) |
| Board modem | Combros "NB-IoT for Raspberry Pi", module SIM7022 |
| UART | ESP32 GPIO17 (TX) → RXD, GPIO16 (RX) ← TXD, 115200 baud. **Không dùng RX0/TX0 (GPIO3/1)**: hai chân này đã nối vào chip USB |
| `ATI` / `AT+CGMR` | `SIM7022 R2110` / **`2110B07SIM7022`** |
| Lệnh MQTT(S)/SSL | Có: `AT+CSSLCFG=?`, `AT+CCERTDOWN=?`, `AT+CMQTTSTART=?` đều trả `OK` |
| Mạng | `+CEREG: 0,1`, `+COPS: 0,2,"45204",9` (Viettel, NB-IoT) |
| APN / IP | `v-internet` (có sẵn trong modem, `NB_APN` để trống), IP 11.175.60.192 |
| Giờ | `+CCLK: "2026/09/24,10:16:26+28"`: năm 4 chữ số, đã hợp lệ sau `AT+CNTP` |
| CA | `digicert_g2.pem` nạp tự động, `AT+CSSLCFG?` → `0,3,1,0,120,"digicert_g2.pem","","",1` |
| **`AT+CMQTTCONNECT` tới broker.emqx.io:8883** | **`+CMQTTCONNECT: 0,32` (handshake fail) sau khoảng 22 s, lặp lại mọi lần** |

Để tìm nguyên nhân, tôi thử thêm ba broker khác. Mọi phép thử đều giữ xác minh chứng chỉ đầy đủ (`authmode 1`, `ignorelocaltime 0`) và dùng CA đúng của từng broker:

| Broker | Tên trong chứng chỉ | Chuỗi chứng chỉ | Kết quả |
|---|---|---|---|
| test.mosquitto.org:8883 | tên chính xác | 1 chứng chỉ, 0,9 KB | `0,0` (16 s) |
| broker.hivemq.com:8883 | tên chính xác | 3 chứng chỉ, khoảng 4 KB, root ký chéo | `0,0` (45 s) |
| broker.emqx.io:8883 | `*.emqx.io` | 3 chứng chỉ, 3,9 KB | **32** |
| mqtt.flespi.io:8883 | `*.flespi.io` | 3 chứng chỉ, CA GlobalSign | **32** |

**Kết luận:** firmware modem `2110B07` không hoàn tất bắt tay TLS với server dùng chứng chỉ wildcard. Nạp thêm DigiCert Global Root CA (xử lý khả năng chuỗi ký chéo) cũng không thay đổi kết quả. Modem không có tùy chọn tắt riêng phần kiểm tra tên, và yêu cầu dự án cấm tắt xác minh chứng chỉ.

Hướng xử lý:

- **Tốt nhất:** broker BAS dùng chứng chỉ cấp cho đúng tên máy chủ, không wildcard.
- Hỏi SIMCom/nhà phân phối về firmware mới hơn `2110B07`.
- Để ESP32 tự làm TLS (mbedTLS) trên kết nối TCP của modem.

Đổi broker thì chỉ cần sửa `MQTT_HOST`/`MQTT_PORT` và thay CA trong `ca_cert.h`/`CA_FILE_NAME`.

## 1. Thư viện và phiên bản

| Thành phần | Phiên bản | Ghi chú |
|---|---|---|
| Arduino IDE | 2.x (arduino-cli 1.5.1 đi kèm) | |
| Core **esp32 by Espressif Systems** | 3.3.12 | Board `ESP32 Dev Module` (`esp32:esp32:esp32`) |
| Thư viện ngoài | **Không có** | JSON được tạo bằng `snprintf` vào buffer cố định |

Kết quả biên dịch (24/09/2026, không cảnh báo): khoảng 299 KB flash (22%), 24 KB RAM tĩnh (7%).

## 2. Sơ đồ chân ESP32 DevKit ↔ SIM7022

Giống dự án cũ:

| ESP32 DevKit | SIM7022 board | Ghi chú |
|---|---|---|
| GPIO17 (TX) | RXD | ESP32 gửi lệnh AT |
| GPIO16 (RX) | TXD | ESP32 nhận phản hồi |
| GND | GND | Bắt buộc chung mass |
| (không nối) | PWRKEY / RESET | Bật modem thủ công theo hướng dẫn board |

UART 115200 8N1 (`MODEM_BAUD`).

- Mức logic UART của SIM7022 phải tương thích 3,3 V. Board không có mạch chuyển mức thì không nối thẳng vào ESP32.
- Nguồn modem lấy từ nguồn riêng đủ dòng, không lấy từ GPIO.
- Lắp anten NB-IoT trước khi chạy.

## 3. Cấu hình (APN nằm ở đâu)

Mọi tham số nằm trong [config.example.h](config.example.h). Muốn sửa thì chép thành `config.h` trong cùng thư mục. `config.h` được ưu tiên khi tồn tại và đã được loại khỏi git.

| Tham số | Giá trị mặc định |
|---|---|
| `NB_APN` | `""`: giữ profile PDP đang có trong modem. Điền APN Viettel cấp cho SIM nếu cần |
| `MODEM_RX_PIN` / `MODEM_TX_PIN` / `MODEM_BAUD` | 16 / 17 / 115200 |
| `DEVICE_ID` | `BAS_TEST_001` |
| Client ID | `bas-BAS_TEST_001` (sinh từ `DEVICE_ID`, nên mỗi thiết bị một ID) |
| Topic | `bas/BAS_TEST_001/telemetry` (sinh từ `DEVICE_ID`) |
| Broker | `broker.emqx.io:8883`, TLS, keepalive 60 s, clean session 1, QoS 0, retain 0 |
| `MQTT_USER` / `MQTT_PASS` | Để trống. Khi chuyển sang broker riêng: log chỉ in `***`, không in mật khẩu |
| `PAYLOAD_MINIMAL` | `1` gửi `{"distance":120.5}`; `0` gửi đủ 10 trường |
| `PUBLISH_INTERVAL_MS` | 60 000 |
| `CA_FILE_NAME` / `CA_FORCE_RELOAD` | `digicert_g2.pem` / `0` |
| `NTP_SERVER` / `NTP_TZ_QUARTERS` | `pool.ntp.org` / `28` (UTC+7, đơn vị 15 phút) |
| `AT_BRIDGE_ONLY` | `true`: ESP32 thành cầu USB ↔ UART để gõ lệnh AT tay |

Payload đầy đủ (175 byte, không có `portId`/`sensorId`):

```json
{"distance":120.5,"sternDistance":118.2,"bowSpeed":2.1,"sternSpeed":1.8,"angle":1.2,"waterLevel":4.2,"waterFlow":0.3,"waterDirection":"NE","windForce":15,"windDirection":"NE"}
```

Giá trị lấy từ `readTelemetry()`, hiện là số thử nghiệm của BAS. Khi có cảm biến thật, chỉ thay hàm này; tên trường giữ nguyên. Số được in với 1 chữ số thập phân. Giá trị NaN/Inf thành `null`. Hướng chỉ nhận 16 hướng la bàn (`N`, `NNE`, … `NNW`), còn lại thành `null`.

## 4. Chứng chỉ CA

Chuỗi chứng chỉ của broker (kiểm tra ngày 24/09/2026):

```
*.emqx.io (hết hạn 16/01/2027) → RapidSSL TLS RSA CA G1 → DigiCert Global Root G2
```

CA dùng là **DigiCert Global Root G2** (hết hạn 15/01/2038), SHA-256:
`CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F`.
Đã kiểm tra bằng OpenSSL: chỉ với CA này, TLS 1.2 xác minh được cả chuỗi và hostname `broker.emqx.io` (`Verify return code: 0`).

**Firmware tự nạp CA, không cần thao tác tay:**

1. Gửi `AT+CCERTLIST`. Nếu chưa có `digicert_g2.pem`, gửi `AT+CCERTDOWN="digicert_g2.pem",1294`, chờ `>`, gửi đúng 1294 byte PEM từ [ca_cert.h](ca_cert.h), chờ `OK`.
2. Gửi `AT+CCERTLIST` lần nữa để xác nhận.

File được lưu trong bộ nhớ modem, nên các lần sau không nạp lại.

- **Đổi CA** (ví dụ khi chuyển sang broker riêng): thay nội dung `ca_cert.h` và `DigiCertGlobalRootG2.pem`. Hoặc đổi `CA_FILE_NAME`, hoặc đặt `CA_FORCE_RELOAD 1` để firmware xóa rồi nạp lại một lần sau mỗi lần khởi động.
- **Nạp tay** (không bắt buộc): bật `AT_BRIDGE_ONLY`, dùng terminal gửi được file nhị phân chính xác số byte (Tera Term, PuTTY…). Gửi `AT+CCERTDOWN="digicert_g2.pem",<số byte file>`, chờ `>`, gửi nội dung file [DigiCertGlobalRootG2.pem](DigiCertGlobalRootG2.pem). Serial Monitor của Arduino IDE không phù hợp vì khó gửi nhiều dòng với đúng số byte.

Không pin chứng chỉ leaf `*.emqx.io`, vì leaf được thay định kỳ.

## 5. Chuỗi lệnh AT firmware gửi

| Bước | Lệnh | Kết quả mong đợi |
|---|---|---|
| 1 UART | `modem.begin(115200, RX16, TX17)` | |
| 2 Modem | `AT` (tối đa 5 lần), `ATE0`, `AT+CMEE=2` | `OK` |
| 3 Phiên bản | `ATI`, `AT+CGMR` | In ra log |
| 3 Kiểm tra khả năng | `AT+CSSLCFG=?`, `AT+CCERTDOWN=?`, `AT+CMQTTSTART=?` | `OK`. Nếu `ERROR`: dừng, xem mục 6 |
| 4 SIM | `AT+CPIN?` | `+CPIN: READY`. Không tự nhập PIN |
| 5 Đăng ký | `AT+CFUN?` (bật `=1` nếu cần), `AT+CPSMS=0`, `AT+CEDRXS=0`, lặp `AT+CEREG?` + `AT+CSQ` tối đa 180 s, `AT+COPS?` | `+CEREG: n,1` hoặc `n,5` |
| 6 APN | `AT+CGDCONT?`. Nếu `NB_APN` khác APN hiện tại: `AT+CFUN=0`, `AT+CGDCONT=1,"IP","<APN>"`, `AT+CFUN=1`, chờ đăng ký lại | |
| 7 IP | Lặp `AT+CGPADDR` (kèm `AT+CGATT?`) tối đa 60 s | `+CGPADDR: <cid>,<ip>` |
| 8 Thời gian | `AT+CTZU=1`, `AT+CCLK?`. Nếu năm < 2025: `AT+CNTP="pool.ntp.org",28`, `AT+CNTP`, `AT+CCLK?` | `+CNTP: 0`, năm hợp lệ |
| 9a CA | `AT+CCERTLIST` (và `AT+CCERTDOWN` nếu thiếu) | Có `digicert_g2.pem` |
| 9b SSL | `AT+CSSLCFG="sslversion",0,3`, `"authmode",0,1`, `"ignorelocaltime",0,0`, `"negotiatetime",0,120`, `"cacert",0,"digicert_g2.pem"`, `"enableSNI",0,1`, `AT+CSSLCFG?` | `OK` |
| 9c MQTT | Dọn phiên cũ: `AT+CMQTTDISC=0,60`, `AT+CMQTTREL=0`, `AT+CMQTTSTOP` (lỗi ở bước này là bình thường) | |
| | `AT+CMQTTSTART` | `+CMQTTSTART: 0` |
| | `AT+CMQTTACCQ=0,"bas-BAS_TEST_001",1` (1 = SSL/TLS) | `OK` |
| | `AT+CMQTTSSLCFG=0,0` | `OK` |
| | `AT+CMQTTCONNECT=0,"tcp://broker.emqx.io:8883",60,1` | `+CMQTTCONNECT: 0,0` |
| 10 Publish | `AT+CMQTTTOPIC=0,26` → `>` → topic; `AT+CMQTTPAYLOAD=0,<len>` → `>` → JSON; `AT+CMQTTPUB=0,0,60,0` | `+CMQTTPUB: 0,0` |

Giải thích một số lựa chọn:

- `authmode 1` bật xác minh server bằng CA. `ignorelocaltime 0` bật kiểm tra thời hạn chứng chỉ, nên phải đồng bộ giờ ở bước 8. Không có chế độ nào bỏ qua kiểm tra chứng chỉ.
- `<server_addr>` phải bắt đầu bằng `tcp://` kể cả khi dùng TLS (theo AT Manual 8.2.8). TLS được chọn bằng `server_type=1` trong `AT+CMQTTACCQ`.
- JSON gửi qua chế độ nhập dữ liệu `>` với độ dài chính xác, nên dấu `"` trong JSON không cần escape.
- **Phiên bản MQTT:** tài liệu SIM7022 không có tham số chọn phiên bản cho `AT+CMQTT*`. Firmware không ép được 3.1.1; đây là mặc định của modem. Nếu broker từ chối sẽ nhận lỗi 27.

Backoff khi lỗi mạng hoặc MQTT: chờ 5 → 10 → 20 → 30 → 60 s, sau đó giữ ở 60 s. Kết nối thành công thì về lại 5 s. Firmware không tự restart ESP32. Nhận `+CMQTTCONNLOST` hoặc publish lỗi thì chạy lại từ bước 2.

## 6. Mã lỗi quan trọng

**MQTT `<err>`** (AT Manual 8.3.1). Firmware in dạng `MQTT error N = <mô tả>` kèm gợi ý:

| Mã | Ý nghĩa | Kiểm tra |
|---|---|---|
| 0 | Thành công | |
| 3 / 4 / 26 | Không mở được socket / socket bị server đóng | Cổng 8883 có qua được APN không, sóng yếu, broker |
| 11 | Chưa kết nối | Tự kết nối lại |
| 14 / 19 / 21 | Client bận / đang dùng / chưa release | Phiên cũ còn trong modem; lần sau được dọn |
| 17 | Timeout | Sóng yếu, mạng chậm |
| 20 | Chưa acquire client | Thứ tự lệnh |
| 22 | Độ dài ngoài giới hạn | Topic/payload quá 1024 byte |
| 25 | Lỗi DNS | APN không có Internet/DNS |
| 27–31 | Broker từ chối CONNECT (phiên bản, client ID, user/pass, quyền) | Cấu hình broker |
| **32** | **TLS handshake fail** | CA sai, **đồng hồ modem sai**, phiên bản TLS, mạng rớt khi đang bắt tay |
| **33** | **Chưa đặt chứng chỉ** | `AT+CCERTLIST`, `AT+CSSLCFG?` |

`+CMQTTCONNLOST: 0,<cause>`: 1 = socket bị đóng, 2 = socket reset, 3 = mạng đóng.

**Lỗi chung:**

| Log | Ý nghĩa |
|---|---|
| `[AT] FAILED` | Modem không trả lời: nguồn, PWRKEY, TX/RX chéo, baud, mức điện áp |
| `+CME ERROR: SIM not inserted` / `SIM PIN required` | Lỗi SIM (`AT+CMEE=2` cho ra mô tả dạng chữ) |
| `+CEREG: 0,2` kéo dài | Đang tìm mạng: anten, vùng phủ NB-IoT |
| `+CEREG: 0,3` | Mạng từ chối đăng ký: SIM chưa kích hoạt NB-IoT |
| `[CNTP] FAILED` | Không đồng bộ được giờ; TLS sẽ thất bại vì đang bật kiểm tra thời hạn |
| `HALTED: modem firmware has no AT+CSSLCFG/...` | Firmware modem cũ (giống tài liệu V1.00) không có MQTT TLS. Gửi giá trị `AT+CGMR` cho SIMCom hoặc nhà phân phối để nâng cấp. Firmware ESP32 dừng hẳn, không thử lại liên tục |

## 7. Log Serial mong đợi khi thành công

Bản dưới đây dựng theo định dạng phản hồi trong tài liệu, **chưa phải log thu từ modem thật**. Chi tiết như `ATI`, `CGMR`, IP, giờ sẽ khác.

```text
ESP32 + SIM7022 NB-IoT -> MQTT over TLS (BAS test)
[1] Modem UART: ESP32 RX=GPIO16, TX=GPIO17, 115200 baud
  broker broker.emqx.io:8883 TLS, MQTT 3.1.1, client id bas-BAS_TEST_001
  topic bas/BAS_TEST_001/telemetry, keepalive 60 s, QoS 0, retain 0, clean session 1, credentials none
=== Connecting ===
[2] Modem check (AT)
> AT
< OK
...
[3] Modem firmware (AT+CGMR)
> AT+CGMR
< +CGMR: <phiên bản modem>
< OK
  firmware: +CGMR: <phiên bản modem>
> AT+CSSLCFG=?
...
[4] SIM (AT+CPIN?)
< +CPIN: READY
[5] NB-IoT registration (AT+CEREG?)
< +CEREG: 0,1
  registered (home)
[6] APN / PDP context (AT+CGDCONT)
[7] IP address (AT+CGPADDR)
  IP: 10.x.x.x
[8] Network time (NITZ, then NTP)
< +CCLK: "26/09/24,09:15:02+28"
  clock OK: +CCLK: "26/09/24,09:15:02+28"
[9a] Root CA in modem (AT+CCERTLIST)
  CA digicert_g2.pem present
[9b] SSL context 0: TLS1.2, verify server, check time, SNI
[9c] MQTT over TLS
> AT+CMQTTSTART
< OK
< +CMQTTSTART: 0
> AT+CMQTTACCQ=0,"bas-BAS_TEST_001",1
< OK
> AT+CMQTTSSLCFG=0,0
< OK
  TLS handshake + MQTT CONNECT (can take a few minutes on NB-IoT)
> AT+CMQTTCONNECT=0,"tcp://broker.emqx.io:8883",60,1
< OK
< +CMQTTCONNECT: 0,0
[9] MQTT connected to broker.emqx.io:8883 over TLS as bas-BAS_TEST_001
[10] Publish #1 to bas/BAS_TEST_001/telemetry (18 bytes): {"distance":120.5}
> AT+CMQTTTOPIC=0,26
> AT+CMQTTPAYLOAD=0,18
> AT+CMQTTPUB=0,0,60,0
< OK
< +CMQTTPUB: 0,0
[11] Publish OK (QoS 0: the modem sent it; the broker sends no PUBACK, confirm with a subscriber)
```

**QoS 0 không có PUBACK.** `+CMQTTPUB: 0,0` chỉ cho biết modem đã gửi gói. Chỉ khi subscriber nhận được bản tin mới xác nhận được broker đã nhận.

## Giao diện theo dõi Serial

Nhấp đôi `bas-monitor.cmd` ở thư mục dự án. Lệnh này chạy web server cục bộ `http://127.0.0.1:8090/` và mở trang bằng Chrome hoặc Edge. Trên trang:

1. Bấm **Chọn COM và kết nối**, chọn COM của ESP32 (CP210x, trên máy này là COM6), baud 115200.
2. Bấm **Theo dõi broker** để subscribe topic qua `wss://broker.emqx.io:8084/mqtt`.

Trang hiển thị:

- tiến trình các bước `[2]` đến `[10]`;
- CGMR, CSQ (đổi sang dBm), CEREG, IP, giờ modem, lỗi gần nhất và thời gian đếm ngược đến lần thử lại;
- bảng ghép mỗi lần ESP32 publish với bản tin broker nhận được, cùng độ trễ;
- kiểm tra JSON hợp lệ và báo trường lạ.

Ngoài ra còn có nút **Reset ESP32** (tạo xung RTS), ô gửi lệnh AT khi firmware bật `AT_BRIDGE_ONLY`, ô lọc log và nút **Lưu log**. Mở `http://127.0.0.1:8090/#demo` để xem trước giao diện bằng dữ liệu mẫu.

Trang và script `bas_flash.ps1` không dùng COM cùng lúc được: đóng một bên trước khi dùng bên kia.

## 8. Hướng dẫn test từng bước

Chỉ làm bước tiếp theo khi bước trước đạt.

1. **Kiểm tra phía máy tính** (không cần phần cứng): `py windows\bas_mqtt_check.py --self-test`. Script dùng cùng file CA và TLS 1.2 như modem, publish lên một topic ngẫu nhiên rồi nhận lại. Phải thấy `SELF-TEST PASSED`. Đã đạt trên máy này ngày 24/09/2026.
2. **Biên dịch và nạp:** Arduino IDE, board **ESP32 Dev Module**, mở `bas_sim7022_mqtts.ino`, Upload. Mở Serial Monitor 115200.
3. **Đọc thông tin modem:** đặt `AT_BRIDGE_ONLY true`, nạp lại, chọn CR+LF và gửi `AT`, `AT+CGMR`, `AT+CSSLCFG=?`, `AT+CMQTTSTART=?`. Nếu hai lệnh cuối trả `ERROR` thì modem không hỗ trợ MQTT TLS; dừng lại và gửi kết quả `AT+CGMR` cho nhà cung cấp. Xong thì đặt lại `false`.
4. **Mạng:** chạy firmware, xác nhận log qua được bước `[4]` đến `[7]`: SIM READY, đăng ký mạng, có IP. Nếu kẹt, xem mục 6.
5. **Thời gian:** bước `[8]` phải in `clock OK` với năm đúng.
6. **Kết nối TLS và MQTT:** bước `[9]` phải có `+CMQTTCONNECT: 0,0`. Lỗi 32 hoặc 33 thì xem mục 4 và 6.
7. **Xác nhận broker nhận bản tin:** trên máy tính chạy `py windows\bas_mqtt_check.py` (subscribe `bas/BAS_TEST_001/telemetry`). Mỗi lần firmware in `[11] Publish OK`, script phải in một dòng `{"distance":120.5}` kèm `valid JSON`. Có thể dùng MQTTX với cùng tham số, nhưng **client ID phải khác** `bas-BAS_TEST_001` để không đá thiết bị ra khỏi broker.
8. **Payload đầy đủ:** đặt `PAYLOAD_MINIMAL 0`, nạp lại, xác nhận script in đủ 10 trường, `valid JSON` và không có trường lạ.
9. **Reconnect:** tháo anten hoặc che sóng khoảng 2 phút. Log phải có lỗi hoặc `CONNLOST`, `Retry in 5 s`, `10 s`, `20 s`, `30 s`, `60 s`, và tự kết nối lại khi có sóng. ESP32 không được reset.
10. Báo kết quả cho backend BAS để họ xác nhận đã nhận dữ liệu theo `BAS_TEST_001`.

`broker.emqx.io` là broker công cộng: ai cũng có thể subscribe topic này. Chỉ dùng dữ liệu thử nghiệm.

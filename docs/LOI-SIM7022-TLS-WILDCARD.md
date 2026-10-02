# Lỗi: SIM7022 không kết nối được broker.emqx.io qua MQTT TLS (`+CMQTTCONNECT: 0,32`)

> Ghi nhận lịch sử từ V2. Nhận định wildcard bên dưới chưa được xác nhận là
> nguyên nhân; xem [kiểm thử TLS V3](NB_TLS_TEST.md) để dùng profile hiện tại.

| | |
|---|---|
| Ngày phát hiện | 24/09/2026 |
| Mức ảnh hưởng | **Chặn**: thiết bị chưa gửi được telemetry nào tới BAS |
| Trạng thái | Đã khoanh vùng nguyên nhân, chờ quyết định hướng xử lý |
| Thiết bị | `BAS_TEST_001` |

## Tóm tắt

Modem SIM7022 (firmware `2110B07SIM7022`) báo lỗi **32 "handshake fail"** mỗi lần kết nối MQTT TLS tới `broker.emqx.io:8883`. Toàn bộ các bước trước đó đều đạt: SIM, mạng NB-IoT, IP, đồng bộ giờ, nạp CA.

Phép thử đối chứng với 4 broker, luôn bật xác minh chứng chỉ, cho kết quả rõ: **broker có chứng chỉ đúng tên máy chủ thì kết nối được; broker dùng chứng chỉ wildcard (`*.domain`) thì luôn lỗi 32.** `broker.emqx.io` dùng chứng chỉ `*.emqx.io`.

## Môi trường

| Thành phần | Giá trị |
|---|---|
| MCU | ESP32 DevKit, ESP32-D0WD-V3 rev 3.1, 4 MB flash |
| Modem | Board Combros "NB-IoT for Raspberry Pi", module **SIM7022** |
| `ATI` | `SIM7022 R2110` |
| `AT+CGMR` | **`2110B07SIM7022`** |
| Kết nối | ESP32 GPIO17 (TX) → RXD, GPIO16 (RX) ← TXD, 115200 baud |
| Nhà mạng | Viettel NB-IoT: `+COPS: 0,2,"45204",9`, `+CEREG: 0,1` |
| APN / IP | `v-internet` / 11.175.60.192 |
| Firmware ESP32 | `firmware/bas_sim7022_mqtts` (lệnh AT trực tiếp, core esp32 3.3.12) |
| Tài liệu lệnh | SIM7022 AT Command Manual V1.05 (ch. 7 SSL, ch. 8 MQTT(S)), SIM7022 SSL Application Note V1.00 |

## Cấu hình yêu cầu

| Tham số | Giá trị |
|---|---|
| Broker | `broker.emqx.io:8883`, MQTT over TLS, MQTT 3.1.1 |
| Client ID | `bas-BAS_TEST_001` |
| Topic | `bas/BAS_TEST_001/telemetry` |
| Keepalive / QoS / Retain / Clean session | 60 s / 0 / false / true |
| Username / Password | để trống |
| TLS | Bắt buộc xác minh chứng chỉ server, **không được** dùng chế độ bỏ qua kiểm tra |

## Các bước tái hiện

Lệnh AT thực tế đã gửi (trích log Serial):

```text
> AT+CCLK?
< +CCLK: "2026/09/24,10:16:26+28"
> AT+CCERTLIST
< +CCERTLIST: "digicert_g2.pem"
> AT+CSSLCFG="sslversion",0,3
> AT+CSSLCFG="authmode",0,1
> AT+CSSLCFG="ignorelocaltime",0,0
> AT+CSSLCFG="negotiatetime",0,120
> AT+CSSLCFG="cacert",0,"digicert_g2.pem"
> AT+CSSLCFG="enableSNI",0,1
> AT+CSSLCFG?
< +CSSLCFG: 0,3,1,0,120,"digicert_g2.pem","","",1
> AT+CMQTTSTART
< +CMQTTSTART: 0
> AT+CMQTTACCQ=0,"bas-BAS_TEST_001",1
< OK
> AT+CMQTTSSLCFG=0,0
< OK
> AT+CMQTTCONNECT=0,"tcp://broker.emqx.io:8883",60,1
< OK
< +CMQTTCONNECT: 0,32          (sau khoảng 22 s)
```

- **Kết quả mong đợi:** `+CMQTTCONNECT: 0,0`.
- **Kết quả thực tế:** `+CMQTTCONNECT: 0,32`. Mã 32 là "handshake fail" theo AT Manual V1.05, mục 8.3.1.

Lỗi lặp lại ở mọi lần thử: 6 lần liên tiếp theo backoff 5/10/20/30/60 s, và 2 lần gửi lệnh tay qua cầu AT. Lỗi đến sau khoảng 22 s, nghĩa là DNS và kết nối TCP tới cổng 8883 đã thông; quá trình hỏng ở giữa bước bắt tay TLS.

## Phép thử đối chứng

Mọi phép thử dùng cùng modem, SIM, vị trí và lệnh AT như trên. Chỉ thay host và CA tương ứng. CA của từng broker đều được kiểm tra trước bằng OpenSSL (`Verify return code: 0`). Chế độ **xác minh chứng chỉ luôn bật** (`authmode 1`, `ignorelocaltime 0`).

| Broker | Chứng chỉ server | Chuỗi chứng chỉ | CA nạp vào modem | Kết quả |
|---|---|---|---|---|
| test.mosquitto.org:8883 | `test.mosquitto.org` (tên chính xác) | 1 chứng chỉ, 923 B | mosquitto.org CA | **`0,0`** sau 16,5 s |
| broker.hivemq.com:8883 | SAN có `broker.hivemq.com` (tên chính xác) | 3 chứng chỉ, khoảng 4 KB, root ký chéo (Starfield) | Amazon Root CA 1 | **`0,0`** sau 44,8 s |
| broker.emqx.io:8883 | **`*.emqx.io`** (wildcard) | 3 chứng chỉ, 3 926 B, root ký chéo | DigiCert Global Root G2 | **`0,32`** sau 22–23 s |
| broker.emqx.io:8883 | **`*.emqx.io`** (wildcard) | như trên | DigiCert Global Root G2 **+** DigiCert Global Root CA | **`0,32`** sau 22,4 s |
| mqtt.flespi.io:8883 | **`*.flespi.io`** (wildcard) | 3 chứng chỉ, CA GlobalSign | GlobalSign Root CA R6 | **`0,32`** sau 34,5 s |

Thông số TLS phía server (đo bằng OpenSSL từ máy tính): broker.emqx.io chọn TLS 1.2, `ECDHE-RSA-AES256-GCM-SHA384` (0xC030). Cipher này có trong danh sách SIM7022 hỗ trợ (SSL Application Note, bảng 2). test.mosquitto.org dùng cùng cipher và kết nối được.

## Các nguyên nhân đã loại trừ

| Giả thuyết | Bằng chứng loại trừ |
|---|---|
| Sai dây, baud, modem không chạy | Tất cả lệnh AT đều có phản hồi. Modem kết nối TLS thành công tới 2 server khác |
| Đồng hồ modem sai (khi bật kiểm tra thời hạn) | `+CCLK` đúng ngày. Hai server khác cũng kiểm tra thời hạn mà vẫn kết nối được |
| CA sai hoặc nạp lỗi | CA được OpenSSL xác minh. `AT+CCERTLIST` và `AT+CSSLCFG?` đúng. Cơ chế nạp CA chạy được với 2 server khác |
| Chuỗi chứng chỉ quá lớn | broker.hivemq.com có chuỗi khoảng 4 KB (lớn hơn EMQX) vẫn kết nối được |
| Root ký chéo | broker.hivemq.com cũng có root ký chéo mà kết nối được. Thêm DigiCert Global Root CA cho EMQX không thay đổi kết quả |
| Cipher / phiên bản TLS | EMQX và test.mosquitto.org dùng cùng TLS 1.2 và cipher 0xC030 |
| Firmware modem không hỗ trợ MQTTS | `AT+CSSLCFG=?`, `AT+CCERTDOWN=?`, `AT+CMQTTSTART=?` đều OK, và MQTTS chạy được với server khác |

## Nguyên nhân

Điểm chung duy nhất của hai trường hợp lỗi là **chứng chỉ server dùng tên wildcard**. Hai trường hợp này dùng hai nhà CA khác nhau (DigiCert, GlobalSign). Hai trường hợp thành công đều có chứng chỉ cấp đúng tên máy chủ.

→ Firmware modem **`2110B07SIM7022`** không khớp được tên wildcard khi xác minh chứng chỉ, nên bắt tay TLS thất bại.

**Mức độ chắc chắn:**
- Kết luận dựa trên phép thử đối chứng, chưa có xác nhận từ SIMCom. Tài liệu SIM7022 hiện có không nhắc tới hỗ trợ hay giới hạn về chứng chỉ wildcard.
- Không thể kiểm chứng bằng cách tắt riêng phần kiểm tra tên: modem không có tùy chọn này, và yêu cầu dự án cấm tắt xác minh chứng chỉ.
- Tín hiệu vô tuyến yếu (`AT+CSQ` từng trả `0,0`), nhưng đây không phải nguyên nhân: các server tên chính xác vẫn kết nối được trong cùng điều kiện.

## Hướng xử lý đề xuất

| # | Hướng | Ưu điểm | Nhược điểm / việc cần làm |
|---|---|---|---|
| 1 | **Broker BAS dùng chứng chỉ cấp đúng tên máy chủ** (không wildcard) | Không cần sửa code; chỉ đổi `MQTT_HOST`, `MQTT_PORT`, CA trong `config.h` / `ca_cert.h` | Cần đội BAS cung cấp broker và CA. **Khuyến nghị** cho triển khai thật |
| 2 | Tạm test với `broker.hivemq.com:8883` | Đã xác minh modem kết nối được | Backend BAS phải subscribe cùng broker. Đây là broker công cộng, chỉ dùng dữ liệu thử |
| 3 | Yêu cầu SIMCom/nhà phân phối cập nhật firmware modem mới hơn `2110B07` | Giữ nguyên broker.emqx.io | Phụ thuộc bên ngoài, chưa biết thời gian |
| 4 | ESP32 tự làm TLS (mbedTLS) trên kết nối TCP của modem | Giữ nguyên broker.emqx.io; mbedTLS trên ESP32 hỗ trợ wildcard | Viết lại phần kết nối, tốn RAM hơn, phải kiểm thử lại |

**Không chọn** cách tắt xác minh chứng chỉ (`authmode 0`) hay bỏ qua kiểm tra thời hạn, vì yêu cầu bảo mật của dự án không cho phép.

## Thông tin cần gửi SIMCom (nếu chọn hướng 3)

- Module: SIM7022, `ATI` = `SIM7022 R2110`, `AT+CGMR` = `2110B07SIM7022`.
- Hiện tượng: `AT+CMQTTCONNECT` với `server_type=1`, `authmode=1` trả `+CMQTTCONNECT: 0,32` khi server dùng chứng chỉ wildcard (`*.emqx.io`, `*.flespi.io`). Server có chứng chỉ đúng tên thì kết nối bình thường.
- Câu hỏi:
  1. Firmware này có hỗ trợ khớp tên wildcard khi xác minh chứng chỉ không?
  2. Có bản firmware mới hơn sửa lỗi này không?
  3. Có lệnh nào lấy mã lỗi TLS chi tiết hơn mã 32 không?

## Tài liệu liên quan

- Firmware và hướng dẫn: [`firmware/bas_sim7022_mqtts/README.md`](../firmware/bas_sim7022_mqtts/README.md)
- Nhật ký kiểm thử: [`VALIDATION.md`](../VALIDATION.md)
- Log Serial lần chạy đầu: `runtime/bas-serial-first-run.log`
- Tài liệu SIMCom: [`docs/SIM7022_Series_AT_Command_Manual_V1.05.pdf`](SIM7022_Series_AT_Command_Manual_V1.05.pdf), [`docs/SIM7022_Series_SSL_Application_Note_V1.00.pdf`](SIM7022_Series_SSL_Application_Note_V1.00.pdf)

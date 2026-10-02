> **Luồng cũ, không dùng cho BAS.** Chạy các lệnh bên dưới từ thư mục `legacy/`. Luồng BAS MQTT TLS xem [README gốc](../README.md).

# ESP32 + SIM7022 + Viettel NB-IoT → MQTT trên Windows

Dự án demo gửi telemetry bằng **mạng NB-IoT của SIM7022**. ESP32 giao tiếp UART với modem; không dùng Wi-Fi, PPP hay PubSubClient trên ESP32. Máy Windows dùng Wi-Fi để nhận dữ liệu qua Internet. Broker Mosquitto chạy thật trên Windows, dashboard đọc dữ liệu từ broker và lưu SQLite.

```text
ESP32 ─UART─ SIM7022 ─NB-IoT Viettel─ Internet ─TCP endpoint public─┐
                                                                │
Windows: ngrok agent ── 127.0.0.1:1884 Mosquitto ── collector ── SQLite
                                                      └─ http://127.0.0.1:8088
```

## 1. Chạy phần Windows trước

Yêu cầu: Python 3.10+ và Mosquitto 2.1.x (broker, mosquitto_pub/sub/passwd). Máy này đã có Python 3.12 và Mosquitto 2.1.2. Không cần cài thư viện Python bên ngoài, Docker hoặc Node.js.

Mở PowerShell tại thư mục dự án:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\setup.ps1
powershell -ExecutionPolicy Bypass -File .\windows\start.ps1
```

Hoặc nhấp đôi `run-local.cmd`. Trình duyệt mở **http://127.0.0.1:8088**. Giữ cửa sổ chạy server mở; Ctrl+C dừng các tiến trình của dự án.

Mở cửa sổ PowerShell thứ hai:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\simulate.ps1 -Count 10
```

Dashboard phải hiển thị `windows-simulator`, nhiệt độ/độ ẩm mẫu và số bản tin tăng. **Bài thử này chỉ đi qua broker trên máy, chưa kiểm tra NB-IoT.** Dashboard đọc được cả JSON và payload `key=value;key=value` từ firmware.

Setup tạo mật khẩu ngẫu nhiên trong `config.local.json`, bản hash trong `runtime/mqtt-passwords` và firmware `config.h`. Không công khai các file này. Broker chỉ lắng nghe loopback, yêu cầu mật khẩu, không sửa dịch vụ Mosquitto/cấu hình có sẵn ở Program Files. Cổng dự án là 1884 để tránh cổng 1883 đang dùng. Dashboard cũng chỉ mở trên loopback, không có API điều khiển thiết bị.

**Mỗi lần chạy `run-local.cmd` sẽ dừng phiên dự án cũ rồi khởi động mới**, bao gồm launcher, broker dùng `runtime/mosquitto.conf`, dashboard, subscriber và chương trình simulate của dự án. Nhấp `stop-local.cmd` để chỉ dừng, không khởi động lại. Script xác định tiến trình theo đường dẫn dự án và quan hệ tiến trình con, không đóng các ứng dụng khác hay dịch vụ Mosquitto hệ thống. Nếu không đọc được thông tin tiến trình hoặc không dừng được phiên cũ, launcher báo lỗi và không tiếp tục mở thêm server.

Tunnel ngrok chạy theo `tcp 127.0.0.1:<mqtt_port>` của dự án cũng được dừng. Sau khi khởi động lại server, chạy lại `windows/tunnel.ps1` và kiểm tra endpoint public có đổi không. Các tab trình duyệt vẫn mở; tải lại trang và chọn lại COM nếu cần. Lệnh dừng không xóa dữ liệu SQLite hoặc cấu hình.

Nếu cổng bị ứng dụng khác chiếm, launcher báo rõ cổng HTTP/MQTT. Có thể sửa `mqtt_port` / `http_port` trong `config.local.json`, chạy setup lại, rồi khởi động lại dự án. Không chạy setup trong lúc broker dự án đang hoạt động. `setup.ps1` tạo lại `config.h`; các chỉnh sửa thủ công trong file này sẽ được thay bằng cấu hình chuẩn.

## Màn hình Serial độc lập để đối chiếu ESP32 và web

1. Chạy `run-local.cmd`. Nếu server đang chạy từ phiên bản cũ, dừng bằng Ctrl+C và chạy lại.
2. Nhấp đôi `open-serial.cmd`, hoặc nút **Mở màn hình Serial ESP32 và đối chiếu MQTT** trên dashboard. Địa chỉ mặc định: **http://127.0.0.1:8088/serial**.
3. Dùng Chrome hoặc Edge trên Windows. Đóng Serial Monitor/Plotter trong Arduino IDE và ứng dụng khác đang giữ cùng COM.
4. Cắm USB ESP32, giữ baud **115200**, bấm **Chọn COM và kết nối**, chọn đúng cổng USB ESP32 (không chọn UART trực tiếp của modem).
5. Bật ESP32, chờ dòng `Publishing: device=esp32-01;seq=...`. Mỗi lần gửi tạo một dòng trong bảng đối chiếu. Màn hình đọc log USB trực tiếp, không gửi payload lên broker.

Hai cửa sổ Serial và dashboard hoạt động cạnh nhau; không cần thay firmware hiện tại. Khi mở cổng, một số board có thể reset do mạch USB-UART. Khi nạp firmware, bấm **Ngắt Serial** để Arduino IDE sử dụng COM.

Kết quả:

- **Khớp hoàn toàn**: toàn bộ chuỗi payload Serial giống payload MQTT đã nhận (topic, device, seq đúng và thời gian nhận phù hợp).
- **Khác payload**: cùng device, seq và uptime nhưng nội dung khác; chọn dòng để xem cả hai payload và vị trí ký tự khác đầu tiên.
- **Đang chờ MQTT / Chưa thấy MQTT sau 180 giây**: chưa thấy bản tin tương ứng; không tự kết luận mất gói. Kiểm tra tunnel, broker, kết nối web và log modem.
- **Cần kiểm tra**: gặp cùng seq nhưng khác/thiếu uptime; không tự coi đó là lỗi dữ liệu vì ESP32 có thể đã reset.

Màn hình bỏ dữ liệu MQTT trước thời điểm kết nối Serial, đối chiếu trong cửa sổ từ 3 giây trước tới 180 giây sau dòng Serial để tránh dùng bản tin cũ. Các mốc dùng đồng hồ Windows; đây không phải phép đo độ trễ mạng chính xác. API trả 100 bản tin mới nhất mỗi lần polling 2 giây, phù hợp chu kỳ firmware 60 giây; khi tải rất cao hoặc tab bị ngủ có thể bỏ lỡ bản tin. Firmware chưa có ID duy nhất cho từng lần khởi động nên vẫn cần đối chiếu log nếu seq/uptime bị lặp sau reset.

Giữ tối đa 500 lần gửi, 2.000 dòng log và 2.000 bản tin MQTT trong tab. **Lưu log và đối chiếu** tải JSON về máy. Đóng tab hoặc kết nối lại bắt đầu phiên mới, nên xuất trước nếu muốn giữ lịch sử. `simulated=1` là số đo giả lập trên ESP32; payload đó vẫn có thể đi thật qua NB-IoT. Để kiểm tra đường truyền, không chạy `simulate.ps1` cùng lúc.

Màn hình dùng [Web Serial của trình duyệt](https://developer.chrome.com/docs/capabilities/serial) trên địa chỉ loopback. Người dùng chọn COM trực tiếp trong hộp thoại của trình duyệt; không cần cài pyserial hay cấp cổng COM cho Python.

## 2. Đưa MQTT về Windows qua Wi-Fi

SIM7022 không thể dùng địa chỉ Wi-Fi `192.168.x.x` hoặc `127.0.0.1` để gọi về máy bạn. Phương án demo kèm theo dùng **đường hầm TCP ngrok**, không cần mở cổng router. Cần tài khoản/agent ngrok của bạn có quyền tạo TCP endpoint; điều kiện và hạn mức tùy tài khoản/gói hiện hành.

1. Cài [ngrok cho Windows](https://ngrok.com/downloads/windows), thêm vào PATH.
2. Cấu hình authtoken bằng lệnh ngrok cung cấp trong dashboard tài khoản của bạn. Không gửi token vào chat và không đưa vào repository.
3. Giữ broker chạy, mở một PowerShell khác:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\tunnel.ps1
```

Tương đương `ngrok tcp 127.0.0.1:1884`. Không dùng `ngrok http`: MQTT ở đây là TCP, không phải HTTP.

Ví dụ ngrok hiển thị `tcp://0.tcp.example.net:12345` (địa chỉ minh họa, phải thay bằng endpoint thật), chạy:

```powershell
# Dừng server trước khi tạo lại cấu hình, sau đó chạy start.ps1 lại.
powershell -ExecutionPolicy Bypass -File .\windows\setup.ps1 -PublicHost "TEN_HOST_THAT" -PublicPort 12345 -Apn "APN_CUA_SIM"
```

`PublicHost` không chứa `tcp://`, không chứa cổng. `PublicPort` là cổng public của tunnel, không nhất thiết là 1884. Giữ ngrok và server cùng chạy khi dùng ESP32. Nếu endpoint đổi sau khi chạy lại ngrok, cập nhật và nạp lại firmware. Windows phải thức, Wi-Fi phải có Internet.

Ngrok chỉ chuyển tiếp TCP; chặng MQTT từ modem đến endpoint trong demo **chưa dùng TLS**. Dùng dữ liệu thử nghiệm và mật khẩu riêng. Muốn triển khai thật cần thêm MQTT TLS với CA/server verification phù hợp firmware; đường hầm tự nó không biến MQTT plaintext thành TLS đầu-cuối.

Nếu không có TCP endpoint ngrok: dùng một TCP reverse tunnel khác mà bạn quản lý hoặc port forwarding khi router có IP public thực. Không mở inbound firewall cho cấu hình loopback hiện tại. Chỉ chỉnh listener/firewall khi chủ động chuyển sang mô hình port forwarding. Giao diện Windows vẫn là server đích, không đổi thành broker công cộng.

## 3. APN Viettel và phần cứng

SIM đã kích hoạt NB-IoT là điều kiện đầu tiên. **APN phụ thuộc gói SIM/hợp đồng Viettel**, dự án không tự đoán `v-internet` hay APN khác. Nhập đúng APN do Viettel cung cấp. Để trống sẽ giữ profile PDP đang có trong modem, không khẳng định profile đó đúng. APN phải có đường IP đến endpoint; APN riêng không ra Internet cần Viettel định tuyến/VPN tương ứng.

Mặc định cho ESP32 DevKit thông thường:

| ESP32 | SIM7022 board |
|---|---|
| GPIO17 TX | RX UART của modem |
| GPIO16 RX | TX UART của modem |
| GND | GND |

Chưa biết model board SIM7022 của bạn nên **không điều khiển RESET/PWRKEY tự động**, không lấy cấu hình reset SIM7600. Bật modem theo hướng dẫn board. Cấp nguồn đủ dòng theo board và lắp anten NB-IoT trước khi chạy. Xác nhận mức điện áp UART của board: module trần/board không có chuyển mức không được mặc định nối thẳng với GPIO 3,3 V. Không cấp nguồn modem từ chân GPIO.

Đổi `modem_rx`, `modem_tx`, `modem_baud` trong `config.local.json` rồi chạy setup nếu board dùng chân/tốc độ khác. GPIO16/17 có thể không khả dụng trên một số ESP32 có PSRAM. Baud mặc định dự án là 115200, phải khớp firmware modem.

## 4. Nạp firmware

1. Arduino IDE: cài **esp32 by Espressif Systems**, chọn đúng board ESP32 và COM port. Dự án dùng API Arduino cơ bản; không cần cài thư viện MQTT.
2. Mở `firmware/sim7022_mqtt/sim7022_mqtt.ino`.
3. Kiểm tra `config.h` đã được setup tạo: public host/port, APN, UART, MQTT user/password/topic.
4. Nạp, mở Serial Monitor 115200 baud.

Firmware tự thử bộ lệnh theo thứ tự `CMQTT`, `QC_PLUS`, `QC_DOLLAR`. Có thể ép bộ lệnh qua `MQTT_DIALECT` trong `config.h` sau khi xác định firmware. `QC_DOLLAR` nhằm tương thích bộ lệnh trong tài liệu SIM7022 V1.00 đi kèm; `QC_PLUS` cho bộ lệnh QC dùng dấu `+`; `CMQTT` cho firmware mới có nhóm CMQTT. Tự nhận diện chỉ xác nhận lệnh test tồn tại, chưa thay thế kiểm tra trên modem thật.

Luồng chạy: AT → tắt echo → kiểm tra SIM → nhận diện MQTT → cấu hình APN nếu có → chờ `CEREG` trạng thái 1 hoặc 5 → kiểm tra attach → kết nối MQTT → publish QoS 1 mỗi 60 giây. Mỗi lệnh có timeout; kết nối thất bại được thử lại với thời gian nghỉ tăng dần tới 5 phút. Các vòng chờ UART có `delay(1)` để nhường CPU. Không có vòng busy-wait vô hạn. PSM/eDRX được yêu cầu tắt cho demo liên tục; firmware chưa tối ưu pin.

Payload:

```text
device=esp32-01;seq=1;uptime_s=85;temperature=25.1;humidity=65;simulated=1
```

`temperature` và `humidity` là **giá trị giả lập**. Thay phần tạo payload trong `publishTelemetry()` bằng giá trị cảm biến và đặt `simulated=0` khi có dữ liệu thật. Firmware chỉ truyền telemetry lên server; chưa triển khai nhận lệnh điều khiển. QoS 1 có thể lặp bản tin; SQLite lưu từng lần nhận. Bộ đếm seq trở về đầu khi ESP32 reset; chưa có hàng đợi lưu flash khi mất mạng.

Log mong đợi: `MQTT connected`, `Publishing: ...`, `MQTT publish acknowledged`. Đây là ACK giao thức, cần đối chiếu bản tin trên dashboard để xác nhận đến đúng server. Không gửi nội dung MQTT credentials ra Serial, nhưng không nên chia sẻ toàn bộ log chứa thông tin SIM chưa biên tập.

## 5. Kiểm tra / chẩn đoán

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\test.ps1
```

Kiểm thử tạo broker riêng trên cổng loopback tạm thời: kiểm tra payload, publish/subscribe thật, ghi SQLite, từ chối sai mật khẩu và kết nối ẩn danh. Kết quả không chứng minh modem đã chạy trên mạng Viettel.

| Hiện tượng | Kiểm tra |
|---|---|
| AT timeout | Nguồn, PWRKEY, GND, RX/TX chéo, baud, mức điện áp |
| SIM not ready | SIM, PIN; không tự nhập PIN nhiều lần |
| CEREG không về 1/5 | Anten, vùng phủ NB-IoT, dịch vụ SIM, APN/band theo nhà mạng |
| Attach được nhưng MQTT không nối | APN có Internet không; tunnel còn chạy; host/port đúng; username/password |
| Unknown MQTT firmware | Bật `AT_BRIDGE_ONLY=true`, dùng Serial Monitor CR+LF gửi `ATI`, `AT+CGMR`, lưu phản hồi |
| QoS publish timeout | Sóng yếu, tunnel chết, modem/network lỗi; xem mã URC trong log |
| Dashboard không có dữ liệu | Chạy simulate; kiểm tra topic trùng nhau và `runtime/subscriber.log` |
| Server không khởi động | `runtime/broker.log`, `runtime/dashboard.log`, kiểm tra cổng đang dùng |

Các log, cấu hình phát sinh, SQLite nằm trong `runtime/`, không thay đổi file PDF gốc. Có thể tải 100 bản tin mới nhất từ dashboard bằng nút JSON. SQLite chưa giới hạn thời gian lưu; demo dài ngày cần thêm chính sách lưu trữ.

## Tài liệu đối chiếu

- [SIMCom SIM7022](https://en.simcom.com/product/SIM7022.html).
- `SIM7022 Series_AT Command Manual_V1.00.pdf` có sẵn trong dự án, chương MQTT.
- [Bản văn bản manual V1.00 do SIMCom phát hành, lưu trên Manualzz](https://manualzz.com/doc/68712254/simcom-sim7022-command-manual) dùng để đối chiếu bộ lệnh QC cũ.
- [SIMCom AT manual V1.05, bản lưu trên Waveshare](https://files.waveshare.com/wiki/SIM7028-NB-IoT-HAT/SIM7028%20NB-IoT%20HAT-Doc/SIM7022_Series_AT_Command_Manual_V1.05.pdf).
- [Mosquitto config](https://mosquitto.org/man/mosquitto-conf-5.html), [mosquitto_sub](https://mosquitto.org/man/mosquitto_sub-1.html).
- [ngrok TCP endpoint](https://ngrok.com/docs/gateway/endpoints/tcp).

Tình trạng kiểm chứng thực tế được ghi trong `VALIDATION.md`.

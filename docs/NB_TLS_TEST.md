# Kiểm thử TLS NB-IoT

Source chính: `firmware/bas_mqtts`. Chưa đồng bộ bản đóng gói/ZIP.

`nb_tls_profile.h` chọn cặp hostname/port/CA cho NB. Mặc định
`BAS_NB_TLS_PROFILE=1` là EMQX (không đổi backend/Wi-Fi). Profile 2 là
Mosquitto TLS để chẩn đoán độc lập. Không tự fallback, không tắt xác minh
chứng chỉ. Các giá trị NB_MQTT_HOST/PORT và CA_FILE_NAME trong config cũ
được profile ghi đè trong nbiot.cpp; muốn dùng broker riêng phải thêm profile
với đúng CA. Không sửa riêng hostname.

Biên dịch mặc định trên Mac từ thư mục repo:

```sh
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile --fqbn esp32:esp32:esp32 firmware/bas_mqtts
```

Biên dịch bản đối chứng (không phải lệnh nạp):

```sh
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile --fqbn esp32:esp32:esp32 --build-property 'compiler.cpp.extra_flags=-DBAS_NB_TLS_PROFILE=2' firmware/bas_mqtts
```

Trước khi nạp phải dừng và ngắt monitor, xác nhận cổng ESP32. Khi thử
profile 2 chỉ chọn NB, dùng dữ liệu giả lập; subscriber phải nối
test.mosquitto.org:8883 với mosquitto.org CA hoặc WSS
wss://test.mosquitto.org:8081/mqtt với system trust. Monitor hiện tại vẫn
subscribe EMQX; query `?broker=mosquitto` KHÔNG đổi firmware/subscriber.
Profile 2 không gửi đến backend BAS hiện đang subscribe EMQX.

Nguồn cấu hình Mosquitto: https://test.mosquitto.org/ (8883 dùng CA
mosquitto.org; broker công cộng chỉ phục vụ thử nghiệm, không gửi secrets).

Điều kiện đạt: CONNECT 0,0, publish thành công và subscriber độc lập nhận
đúng topic/payload. Compile hoặc CA hiện diện không chứng minh handshake.
Nếu profile 2 thành công nhưng profile 1 vẫn lỗi thì tập trung kiểm tra
tương thích TLS/chứng chỉ EMQX trên modem, chưa kết luận wildcard là nguyên nhân.

Test native:
```sh
c++ -std=c++11 firmware/tests/nb_tls_test.cpp -o /tmp/bas-nb-tls-test
/tmp/bas-nb-tls-test
c++ -std=c++11 -DBAS_NB_TLS_PROFILE=2 firmware/tests/nb_tls_test.cpp -o /tmp/bas-nb-tls-test-2
/tmp/bas-nb-tls-test-2
node --test windows/bas_receive_test/*.test.mjs
```

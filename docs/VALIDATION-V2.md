# Kết quả kiểm tra (BAS MQTT TLS)

Kết quả của luồng cũ (Mosquitto + ngrok) nằm ở `legacy/VALIDATION.md`.

## Firmware BAS MQTT TLS (`firmware/bas_sim7022_mqtts`), 24/09/2026

Đã kiểm chứng:

- Biên dịch bằng arduino-cli 1.5.1 và core esp32 3.3.12, board `esp32:esp32:esp32`, cả hai chế độ payload (tối thiểu và đầy đủ), không cảnh báo từ sketch. Kích thước khoảng 299 KB flash, 24 KB RAM tĩnh.
- `broker.emqx.io:8883` hỗ trợ TLS 1.2 với cipher ECDHE-RSA-AES256-GCM-SHA384 (0xC030), có trong danh sách cipher của SIM7022. Chuỗi chứng chỉ đi về DigiCert Global Root G2. OpenSSL xác minh được chuỗi và hostname khi chỉ dùng CA này.
- `windows/bas_mqtt_check.py --self-test` đạt: cùng CA, TLS 1.2, MQTT 3.1.1, clean session, keepalive 60, không user/pass; bản tin JSON 10 trường được publish và nhận lại nguyên vẹn.
- Chuỗi JSON do firmware tạo (định dạng `%.1f` trên float) là JSON hợp lệ.

Chưa kiểm chứng:

- Chưa nạp firmware vào ESP32 và chưa chạy với SIM7022 thật.
- Chưa có kết quả `AT+CGMR`, nên chưa biết modem có nhóm `AT+CMQTT*`/`AT+CSSLCFG` hay không. Tài liệu V1.00 trong dự án không có các lệnh này; tài liệu V1.05 có. Firmware sẽ dò khi khởi động và dừng lại, báo rõ, nếu modem không hỗ trợ.
- Chưa xác nhận APN Viettel; `NB_APN` đang để trống.
- Phiên bản MQTT 3.1.1 là mặc định của modem, không có lệnh để đặt.

### Chạy trên phần cứng thật, 24/09/2026

- ESP32-D0WD-V3 (COM6) nối board Combros SIM7022 qua GPIO17→RXD, GPIO16←TXD, 115200 baud. Lần đầu dây cắm vào RX0/TX0 nên modem không trả lời; đổi sang GPIO16/17 thì chạy.
- Modem `SIM7022 R2110`, `AT+CGMR` = `2110B07SIM7022`, có đủ `AT+CSSLCFG`/`AT+CCERTDOWN`/`AT+CMQTT*`.
- Qua được các bước [2] đến [9b]: SIM READY, CEREG 1, Viettel 45204 AcT 9, APN `v-internet`, IP, đồng bộ giờ (sau khi sửa lỗi đọc năm 4 chữ số của `+CCLK`), nạp CA, cấu hình SSL.
- `AT+CMQTTCONNECT` tới broker.emqx.io:8883 luôn trả `+CMQTTCONNECT: 0,32`. Cơ chế thử lại 5/10/20/30/60 s chạy đúng.
- Chẩn đoán, vẫn giữ xác minh chứng chỉ: test.mosquitto.org và broker.hivemq.com (chứng chỉ tên chính xác) kết nối `0,0`; broker.emqx.io và mqtt.flespi.io (chứng chỉ wildcard) đều lỗi 32. Nguyên nhân: modem không bắt tay được với chứng chỉ wildcard.
- Chưa có bản tin nào tới `bas/BAS_TEST_001/telemetry`, vì chưa kết nối được broker.emqx.io.

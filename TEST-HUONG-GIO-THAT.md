# Bản sửa đọc ES-WS-04 thật

Mở `firmware/bas_mqtts/bas_mqtts.ino`. Payload lấy từ Modbus RS485,
không lấy dữ liệu giả lập. Lệnh PAYLOAD MIN/FULL không thay số đo cảm biến.

- RS485: RX16/TX17, slave 1, 4800 baud, tự đổi chiều trên board ATMC.
- SIM7022: ESP32 RX21 nối modem TX, TX22 nối modem RX; chung GND.
- Đọc mỗi 100 ms, mẫu quá 3 giây không còn hợp lệ; gửi mục tiêu mỗi 1 giây.
  NB-IoT gửi tuần tự, không chồng lệnh AT; mạng chậm có thể kéo dài chu kỳ.
- NB mặc định profile 2: Mosquitto TLS8883 đúng cặp CA.
- Wi-Fi vẫn EMQX TLS8883; điền Wi-Fi trong config.h riêng hoặc qua Serial.
- Topic cả hai kênh: bas/BAS_TEST_001/telemetry.
- Sau khởi động gửi MODE NB hoặc MODE WIFI bằng Serial115200/Newline.
  AUTO_START vẫn false để không phát lên broker công cộng khi chưa chọn.

Trên https://server.aitrg.io.vn/iot-test/ chọn Mosquitto cho NB,
EMQX cho Wi-Fi, topic như trên, bấm Nhận MQTT. Xem ô hướng gió và JSON.
Không mong đợi ô tốc độ gió/khoảng cách có số từ cảm biến hướng gió.

Xác nhận thực tế: xoay cảm biến và kiểm tra log SENSOR angle thay đổi,
MQTT publish thành công và góc tương ứng trên web. Ngắt cảm biến:
payload phải status khác ok và angle null sau thời gian stale.
Không xem test mô phỏng hay biên dịch thành công là bằng chứng phần cứng.

Nếu trước đây có config.h, cập nhật các cấu hình mới vào đó vì nó ghi đè
config.example.h. Không đổi dây khi đang cấp nguồn; xác nhận jumper UART
board đúng GPIO21/22 và nguồn cảm biến theo nhãn thiết bị.

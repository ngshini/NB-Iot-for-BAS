# Gửi hướng gió và tốc độ gió cùng một bản tin

Firmware `bas_mqtts` đọc ES-WS-04 địa chỉ 1 và ES-WS-02 địa chỉ 2
trên cùng bus RS485 tích hợp ATMC (RX16/TX17), 4800 baud, 8N1.
Không sử dụng bộ XY-485 cắm vào chân RX/TX chưa xác định của board.
Không nối A/B trực tiếp vào GPIO.

Trước khi sử dụng: ngắt nguồn để thay dây; đổi địa chỉ ES-WS-02 khi chỉ
riêng nó nối trên bus theo README của `wind_dual_rs485`, xác minh địa chỉ 2.
Sau đó nối hai cảm biến chung A/B và nguồn phù hợp nhãn 10–30 VDC.
Firmware này không tự ghi/đổi địa chỉ cảm biến.
Nếu có `config.h` riêng, bổ sung `WIND_SPEED_ADDRESS 2` và
`WIND_SPEED_READ_INTERVAL_MS 1000UL` như config.example.h.

Một MQTT publish mỗi giây trên topic cũ `bas/BAS_TEST_001/telemetry`:

```json
{"sensor":"wind-pair","windDirection":137.0,"windSpeed":3.6,"status":"ok","complete":true,"direction_unit":"deg","speed_unit":"m/s","direction_status":"ok","speed_status":"ok","direction_age_ms":10,"speed_age_ms":20}
```

Số trên là ví dụ, không phải số đo phần cứng. Hai số đo được chụp chung
dưới khóa từ các lần đọc Modbus tuần tự, không đo đồng thời tuyệt đối.
Tuổi mẫu riêng cho biết độ lệch thời gian. Tốc độ đọc mỗi giây, hướng gió
đọc khoảng 100 ms khi bus không bị timeout. Mất cảm biến tốc độ có thể làm
lần đọc hướng gió chậm thêm tối đa một timeout 350 ms mỗi giây.

Trường không hợp lệ/quá 3 giây là `null`, không thay bằng 0. `complete:false`
khi thiếu một cảm biến; status ngoài cùng `ok` để web vẫn hiển thị trường
còn hợp lệ, trạng thái riêng nằm ở direction_status/speed_status.
Web hiện có hỗ trợ hai trường số windDirection/windSpeed này.
NB-IoT và Wi-Fi dùng chung hàm đóng gói, nhưng broker phải khớp trang web.

## Kiểm chứng phần cứng ngày 03/10/2026

Đã cấu hình riêng ES-WS-02 từ địa chỉ 1 sang 2, đọc lại xác minh thành công.
Sau khi nối lại hai cảm biến, đã nạp `bas_mqtts` vào ESP32 MAC
`a8:42:e3:ee:38:9c`; flash được kiểm tra hash thành công.
Hướng gió hơn 400 lần đọc với errors=0 trong lần kiểm tra.
Luồng public `/iot-test/events` nhận các bản tin mới `complete:true`, cả hai
trạng thái `ok`, khoảng mỗi giây. Tốc độ thay đổi 0.0 → 0.6 → 1.1 → 1.7 m/s,
hướng gió cũng thay đổi. Đây là kiểm tra đọc/truyền, không phải hiệu chuẩn.
Chưa xác minh ngắt/cấp nguồn toàn bộ bộ mạch sau cấu hình hai cảm biến.

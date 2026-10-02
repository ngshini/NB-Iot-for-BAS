# Public receiver NB-IoT

Firmware đọc ES-WS-04 qua RS485 -> Mosquitto TLS8883.
Relay chỉ subscribe `bas/BAS_TEST_001/telemetry` qua TLS8886 với CA hệ thống.
Nó không publish, không tạo số đo, không ghi BAS DB và không cho nhập broker tùy ý.
Web public nhận SSE cùng origin `/iot-test/events`; EMQX giữ đường WSS cũ.
Không lưu lịch sử server; trang mở mới đợi bản tin mới.

Triển khai trên server tại deployments/iot-20261002; container bas-iot-relay
dùng image bas-backend sẵn có, script mount read-only, network bas_default,
không mở port host, restart unless-stopped. Frontend nginx chuyển SSE nội bộ,
X-Accel-Buffering=no đi xuyên host nginx/Cloudflare.

Backup nginx-before-relay.conf và receiver-before-relay.mjs giữ bản trước sửa.
Image frontend trước relay được tag bas-frontend:before-relay-20261002.
Image mới bas-frontend:latest chứa cả receiver và nginx SSE.
Container relay độc lập compose: nếu tạo lại toàn bộ Docker network cần gắn
lại relay vào bas_default. Full build từ source nginx cũ cũng cần giữ location SSE.

Broker công cộng không bảo đảm độ ổn định hoặc danh tính thiết bị.
Đây là luồng test dữ liệu thật, không phải cấu hình production.

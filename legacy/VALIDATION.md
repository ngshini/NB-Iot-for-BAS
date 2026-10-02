# Kết quả kiểm tra

## Chế độ khởi động mới thay phiên cũ

- `run-local.cmd` gọi stop trước khi mở server mới; `stop-local.cmd` dừng riêng.
- Đã dừng phiên dự án cũ và xác nhận cổng 1884/8088 không còn LISTENING.
- Chỉ chọn tiến trình theo đường dẫn dự án, cấu hình broker và tiến trình con; giữ nguyên dịch vụ broker hệ thống và ứng dụng khác.
- 7 bài kiểm tra logic cổng đạt với chế độ mới: cổng còn bị giữ sau bước dừng sẽ báo lỗi thay vì dùng lại tiến trình không rõ nguồn gốc.
- Mục “tái sử dụng server” bên dưới là kết quả phiên bản trước, đã được thay bằng chế độ dừng rồi khởi động mới theo yêu cầu.

## Sửa lỗi mở dự án lần thứ hai (WinError 10048)

- Xác nhận broker 1884 và dashboard 8088 đã chạy; API báo subscriber hoạt động.
- Launcher mới tái sử dụng server sau khi kiểm tra API và xác thực broker bằng SUBACK, không publish dữ liệu thử.
- Đã chạy launcher với `--no-browser` trên phiên thực đang hoạt động: thoát thành công, không tạo server trùng.
- 7 bài kiểm tra khởi động đạt, gồm cổng trống, dự án đã chạy, thành phần chạy một phần, ứng dụng khác giữ cổng, sai tài khoản và cấu hình hai cổng trùng nhau.

Kiểm tra trên Windows của dự án ngày 24/09/2026:

- Python 3.12: kiểm tra cú pháp toàn bộ file Python đạt.
- PowerShell: tất cả script phân tích cú pháp thành công.
- Mosquitto 2.1.2: 5 bài kiểm tra tự động đạt (`windows/test.ps1`).
- Publish QoS 1 có tài khoản → subscriber thật → dữ liệu SQLite: đạt cho cả payload firmware và JSON.
- Sai mật khẩu và kết nối không có tài khoản: broker từ chối.
- Chạy broker dự án trên loopback 1884 và dashboard trên loopback 8088: đạt.
- Gửi 3 bản tin giả lập, gọi HTTP `/api/telemetry`: nhận đủ 3 bản tin, dữ liệu đúng.
- HTTP `/` trả trang dashboard; `/config.local.json` trả 404.
- Chưa kiểm tra giao diện bằng ảnh chụp trình duyệt: Chrome headless thoát lỗi trong môi trường này; HTTP/API đã được kiểm tra trực tiếp.

Chưa kiểm chứng:

- Chưa biên dịch/nạp firmware ESP32: Arduino IDE có sẵn nhưng ESP32 core/toolchain chưa có trong môi trường sử dụng được. Cần cài core ESP32 và chọn đúng board trong Arduino IDE.
- Chưa thử UART với SIM7022, chưa biết board/baud/mức điện áp/firmware thực tế.
- Chưa kết nối mạng Viettel thật; APN vẫn trống để giữ profile modem hiện có.
- Chưa tạo TCP tunnel public: cần endpoint từ tài khoản ngrok của người dùng hoặc đường mạng public tương đương.
- Hỗ trợ các dialect MQTT dựa trên tài liệu AT; phải xác nhận phản hồi thực tế của modem.

Dữ liệu nhiệt độ/độ ẩm là giả lập có nhãn; không phải số đo cảm biến thật. Broker/dashboard chạy được nội bộ không đồng nghĩa đường NB-IoT đã thông.

## Bổ sung màn hình Serial độc lập

- Đã thêm `/serial`, liên kết trên dashboard và `open-serial.cmd`.
- 6 bài kiểm tra Python đạt: gồm các bài MQTT/SQLite trước đó và HTTP route trang Serial, JavaScript, API, chặn đọc cấu hình riêng.
- 11 bài kiểm tra JavaScript đạt: khớp nguyên văn, khác số đo, sai nguồn/topic, dữ liệu cũ, trùng seq sau reset, MQTT đến sau, bản tin QoS lặp, khoảng trắng payload, dòng Serial chia nhiều đoạn, giới hạn độ dài và cú pháp script trình duyệt.
- JavaScript được chạy bằng Node đi kèm Arduino IDE; không cần cài dependency để sử dụng màn hình Serial.
- Chưa kiểm tra chọn COM/đọc USB trên ESP32 thật hoặc giao diện qua trình duyệt thực. Cần dùng Chrome/Edge, đóng Serial Monitor khác và chọn cổng COM ESP32 để kiểm tra phần cứng.
- Màn hình không sửa firmware và không publish dữ liệu lên MQTT; nó đọc USB và đối chiếu API của server.


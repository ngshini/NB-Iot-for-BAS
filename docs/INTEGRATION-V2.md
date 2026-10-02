# Tích hợp V2 vào V3 — 02/10/2026

Nguồn: `../esp32-NBIoT V2/esp32-NBIoT` (tính từ gốc repo).
Đích phát triển: `NB-Iot-for-BAS` trong workspace `NBIoT V3`.

## Đã tích hợp

- Bổ sung các sketch TF03 MQTT, TF03 web, ES-WS-04 RS485, BAS Wi-Fi và BAS SIM7022 cũ.
- Bổ sung launcher Windows, công cụ biên dịch/nạp, monitor RS485 và subscriber MQTT Python.
- Giữ toàn bộ firmware `bas_mqtts` của V3, gồm TLS profiles, CA và kiểm thử hiện có.
- Ghép thẻ số đo TF03, xử lý payload TF03 và thời gian mất dữ liệu vào monitor V3;
  giữ xử lý trạng thái SKIP/TLS đã có.
- Sửa `atmc-tf03-web.cmd` để chọn đúng sketch `atmc_tf03_web`.
- Giữ `legacy/` để tham khảo; hướng dẫn cũ yêu cầu chạy từ thư mục đó.
- Lưu README và kết quả kiểm tra V2 trong `docs/README-V2.md`, `docs/VALIDATION-V2.md`.
  Đây là ghi nhận lịch sử, không phải kết quả kiểm thử lại V3. Nhận định nguyên nhân
  wildcard trong tài liệu V2 chưa được kết luận; ưu tiên giới hạn trong `NB_TLS_TEST.md`.

## Phạm vi

Không sao chép mật khẩu trong `config.h`/`config.local.json`, log runtime,
cơ sở dữ liệu hay bản sao flash. Tạo cấu hình từ `config.example.h` cho sketch cần dùng.
Ảnh và tài liệu cảm biến ở ngoài thư mục mã nguồn vẫn nằm trong `esp32-NBIoT V2`.
Thư mục V2 gốc được giữ nguyên.

Các sketch cảm biến và sketch NB-IoT vẫn là các chương trình riêng.
`bas_mqtts` còn phát dữ liệu giả lập; việc tích hợp thư mục không chứng minh
truyền số đo cảm biến thật qua SIM7022. Chưa nạp hoặc thử lại trên phần cứng.

## Kiểm tra sau tích hợp

- PowerShell: tất cả script được parser kiểm tra, không có lỗi cú pháp.
- Python: 9 file được kiểm tra bằng `ast.parse`, không có lỗi cú pháp.
- Đối chiếu nguồn: đủ 71 đường dẫn mã/công cụ thuộc phạm vi sao chép
  (không tính README và VALIDATION); các file trùng dùng bản V3 hoặc bản đã ghép.
- Kiểm tra hook nhận TF03 và xử lý SKIP của V3; `git diff --check` đạt.
- Xác nhận Git bỏ qua cấu hình riêng, runtime và build.
- Chưa xác nhận build: Arduino CLI 0.35.3 trên máy đã bắt đầu xử lý
  `bas_mqtts` nhưng không hoàn tất sau vài phút; đã dừng lần kiểm tra.
  Chưa kiểm thử trình duyệt hoặc phần cứng. Cần biên dịch từng sketch cần dùng
  bằng `windows/bas_flash.ps1 -Sketch <ten-sketch> -CompileOnly` trước khi nạp.

#pragma once
static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="vi"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ATMC · TF03</title><style>
body{font:17px system-ui;background:#101b2a;color:#edf5ff;max-width:780px;margin:40px auto;padding:20px}
section{background:#1b2c41;padding:26px;border-radius:20px;margin:20px 0}h1{font-size:28px}
#distance{font-size:72px;font-weight:700;margin:18px 0}small,p{color:#bbcee4}a{color:#79d6ff}
.row{display:flex;gap:24px;flex-wrap:wrap}strong{color:#7de1c0}canvas{width:100%;height:160px}
</style><h1>ATMC / Cảm biến laser TF03</h1><p>Dữ liệu trực tiếp qua Wi-Fi</p>
<section><strong id="status">Đang kết nối…</strong><div id="distance">—</div>
<div class="row"><span>Cường độ: <b id="strength">—</b></span><span>Tuổi dữ liệu: <b id="age">—</b></span></div>
<p id="network"></p><canvas id="chart" width="700" height="160" aria-label="Biểu đồ khoảng cách gần đây"></canvas>
<small>Biểu đồ tối đa 60 lần cập nhật; chỉ hiển thị số đo hợp lệ.</small></section>
<p><a href="/settings">Cấu hình Wi-Fi và cảm biến</a> · <a href="/api/data">Dữ liệu JSON</a></p>
<p>Không có số đo? Kiểm tra GPIO nhận, baud và tín hiệu cảm biến trong trang cấu hình.</p>
<script>
const el=id=>document.getElementById(id), points=[];
const labels={ok:'Đang nhận tín hiệu',pin_not_configured:'Chưa cấu hình GPIO nhận TF03',no_data:'Chưa nhận được khung TF03',stale:'Mất tín hiệu cảm biến',invalid:'Số đo không hợp lệ',uart_error:'Không mở được UART'};
function draw(){let c=el('chart'),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);let v=points.filter(Number.isFinite);if(!v.length)return;let lo=Math.min(...v),hi=Math.max(...v),span=Math.max(.1,hi-lo);x.strokeStyle='#7de1c0';x.lineWidth=2;x.beginPath();let gap=true;points.forEach((n,i)=>{if(n===null){gap=true;return}let px=i*c.width/59,py=145-(n-lo)*125/span;if(gap)x.moveTo(px,py);else x.lineTo(px,py);gap=false});x.stroke()}
async function poll(){try{const r=await fetch('/api/data',{cache:'no-store',signal:AbortSignal.timeout(4000)});if(!r.ok)throw Error();const d=await r.json();el('status').textContent=labels[d.status]||d.status;el('distance').textContent=d.distance===null?'—':d.distance.toFixed(2)+' m';el('strength').textContent=d.strength??'—';el('age').textContent=d.age_ms===null?'—':d.age_ms+' ms';el('network').textContent=d.wifi_connected?'Wi-Fi đã kết nối · IP '+d.ip:'Đang dùng Wi-Fi cấu hình của bo';points.push(d.distance);if(points.length>60)points.shift();draw()}catch(e){el('status').textContent='Mất kết nối với bo';el('distance').textContent='—';el('strength').textContent='—';el('age').textContent='—';points.push(null);if(points.length>60)points.shift();draw()}finally{setTimeout(poll,500)}}poll();
</script></html>)HTML";

static const char SETTINGS_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="vi"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Cấu hình ATMC</title>
<style>body{font:17px system-ui;max-width:650px;margin:30px auto;padding:20px;background:#101b2a;color:#edf5ff}label,input,select{display:block;margin:12px 0}input,select,button{font:inherit;padding:10px;max-width:95%}form{padding:20px;background:#1b2c41;margin:20px 0;border-radius:16px}a{color:#79d6ff}</style>
<h1>Cấu hình ATMC / TF03</h1><a href="/">← Xem khoảng cách</a>
<form method="post" action="/sensor"><h2>Cảm biến</h2><input type="hidden" name="token" value="%TOKEN%">
<p>SDA/SCL là nhãn trên bo. Nhập GPIO ESP32 thực sự nối tới TX của TF03 theo sơ đồ ATMC; không tự suy ra là 21/22.</p>
<label>GPIO nhận (-1: tắt)<input type="number" required name="pin" min="-1" max="39" value="%PIN%"></label>
<label>Baud<select name="baud">%BAUDS%</select></label>
<label>Giá trị ngoài tầm của cảm biến (cm)<input type="number" required min="1" max="65535" name="range" value="%RANGE%"></label>
<p>Mặc định TF03: 115200 baud, khung nhị phân, đơn vị cm. Cảm biến phải được đặt đúng chế độ này.</p>
<button>Lưu cảm biến và khởi động lại</button></form>
<form method="post" action="/wifi"><h2>Wi-Fi 2,4 GHz</h2><input type="hidden" name="token" value="%TOKEN%">
<label>Tên mạng<input name="ssid" maxlength="32" required autocomplete="off"></label>
<label>Mật khẩu<input type="password" name="password" maxlength="63" autocomplete="new-password"></label>
<p>Để trống mật khẩu chỉ khi mạng không có mật khẩu. Không gửi biểu mẫu này nếu muốn giữ Wi-Fi hiện tại.</p>
<button>Lưu Wi-Fi và khởi động lại</button></form></html>)HTML";

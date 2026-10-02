'use strict';
const $ = id => document.getElementById(id);
const labels = {match:'Khớp hoàn toàn',different:'Khác payload',waiting:'Đang chờ MQTT',waiting_late:'Chưa thấy MQTT sau 180 giây',ambiguous:'Cần kiểm tra: trùng seq, khác/thiếu uptime'};
let port = null, reader = null, closing = false, connecting = false;
let records = [], logs = [], mqtt = new Map(), topic = '', sessionStart = Infinity, nextId = 0, selected = null;
let webHealthy = false;
function appendLog(line) {
  const time = Date.now();
  logs.push({time, line}); if (logs.length > 2000) logs.shift();
  $('log').textContent = logs.map(x => new Date(x.time).toLocaleTimeString('vi-VN')+'  '+x.line).join('\n');
  if ($('autoscroll').checked) $('log').scrollTop = $('log').scrollHeight;
  const value = SerialCompare.publishing(line);
  if (value) {
    const row = {...value, time, id: ++nextId}; records.push(row);
    if (records.length > 500) records.shift();
    if (!records.some(r => r.id === selected)) selected = row.id;
    render();
  }
}
function render() {
  const messages = [...mqtt.values()]; let matches = 0, different = 0;
  $('rows').replaceChildren();
  for (const record of [...records].reverse()) {
    const result = SerialCompare.compare(record, messages, topic, Date.now()); record.result = result;
    if (result.status === 'match') matches++;
    if (result.status === 'different') different++;
    const tr = document.createElement('tr'); if (selected === record.id) tr.className = 'selected';
    const values = [new Date(record.time).toLocaleTimeString('vi-VN'), record.data.device, record.data.seq, labels[result.status], result.mqtt ? new Date(result.mqtt.received).toLocaleTimeString('vi-VN') : '—'];
    values.forEach((value, index) => {const td=document.createElement('td');td.textContent=value;if(index===3)td.className=result.status;tr.append(td)});
    tr.onclick = () => {selected = record.id; render()}; $('rows').append(tr);
  }
  $('sent').textContent=records.length; $('matched').textContent=matches; $('different').textContent=different;
  const row = records.find(r => r.id === selected);
  if (row) {
    $('detailStatus').textContent = labels[row.result.status] + (!webHealthy ? ' · Web đang mất kết nối; kết quả đã nhận được giữ lại.' : '');
    $('serialPayload').textContent = row.payload;
    $('mqttPayload').textContent = row.result.mqtt ? row.result.mqtt.payload : 'Chưa có bản tin tương ứng';
    let detail = '';
    if (row.result.status === 'match') detail = 'Toàn bộ payload giống nhau, không chỉ giá trị nhiệt độ/độ ẩm.' + (row.result.copies > 1 ? ' Có '+row.result.copies+' lần nhận giống nhau (QoS 1 có thể lặp).' : '');
    if (row.result.status === 'different') {
      const b=row.result.mqtt.payload; let i=0; while(i<row.payload.length && i<b.length && row.payload[i]===b[i]) i++;
      detail='Khác từ vị trí ký tự '+(i+1)+'. Độ dài Serial: '+row.payload.length+'; MQTT: '+b.length+'.';
    }
    $('difference').textContent=detail;
  }
}
async function poll() {
  try {
    const response = await fetch('/api/telemetry', {signal: AbortSignal.timeout(5000)});
    if (!response.ok) throw new Error('HTTP '+response.status);
    const state=await response.json(); topic=state.topic; webHealthy=true;
    for(const m of state.messages) if(Date.parse(m.received)>=sessionStart) mqtt.set(m.id,m);
    if(mqtt.size>2000) mqtt=new Map([...mqtt.entries()].sort((a,b)=>Date.parse(b[1].received)-Date.parse(a[1].received)).slice(0,2000));
    $('mqttStatus').textContent='Web MQTT: đọc được dữ liệu · Topic '+topic+(state.subscriber_running?'':' · Tiến trình nhận MQTT đã dừng')+(state.error?' · '+state.error:'');
  } catch(e) {webHealthy=false;$('mqttStatus').textContent='Web MQTT: mất kết nối — kiểm tra run-local.cmd. '+e.message;}
  render(); setTimeout(poll,2000);
}
async function connect() {
  if(connecting || port) return;
  connecting=true; $('connect').disabled=true; $('error').textContent='';
  try {
    const chosen=await navigator.serial.requestPort();
    await chosen.open({baudRate:Number($('baud').value),dataBits:8,stopBits:1,parity:'none',flowControl:'none'});
    port=chosen; closing=false; sessionStart=Date.now(); records=[]; logs=[]; mqtt.clear(); selected=null;
    $('log').textContent=''; $('serialPayload').textContent='—'; $('mqttPayload').textContent='—'; $('difference').textContent=''; $('detailStatus').textContent='Đang chờ dòng Publishing: từ ESP32.';
    $('baud').disabled=true; $('disconnect').disabled=false;
    $('serialStatus').textContent='Serial: đã kết nối USB ESP32 · '+$('baud').value+' baud'; render();
    const decoder=new TextDecoder(); const lines=new SerialCompare.LineBuffer();
    try {
      reader=port.readable.getReader();
      while(!closing) {
        const {value,done}=await reader.read(); if(done) break;
        for(const line of lines.push(decoder.decode(value,{stream:true}))) appendLog(line);
      }
    } finally {
      if(reader){reader.releaseLock();reader=null;}
      if(port){try{await port.close();}finally{port=null;}}
    }
  } catch(e) {
    if(e.name!=='NotFoundError') $('error').textContent='Serial: '+e.message+'. Đóng Serial Monitor/Plotter và kiểm tra cáp USB, driver COM.';
  } finally {
    connecting=false; $('connect').disabled=false; $('disconnect').disabled=true; $('baud').disabled=false;
    $('serialStatus').textContent='Serial: đã ngắt / chưa kết nối. Kết quả phiên trước vẫn được giữ để xuất log.';
  }
}
$('connect').onclick=connect;
$('disconnect').onclick=async()=>{closing=true;$('disconnect').disabled=true;if(reader){try{await reader.cancel()}catch(e){$('error').textContent=e.message}}};
$('export').onclick=()=>{
  const data={exported:new Date().toISOString(),topic,sessionStart:Number.isFinite(sessionStart)?new Date(sessionStart).toISOString():null,logs,records};
  const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));
  const link=document.createElement('a');link.href=url;link.download='esp32-serial-mqtt-'+Date.now()+'.json';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
};
if(!('serial' in navigator)) {$('connect').disabled=true;$('error').textContent='Trình duyệt không hỗ trợ Web Serial. Mở trang này bằng Chrome hoặc Edge trên Windows.';}
poll();

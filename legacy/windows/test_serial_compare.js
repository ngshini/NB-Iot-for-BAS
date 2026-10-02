'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const C = require('./serial_compare.js');
const time = Date.parse('2026-09-24T10:00:00Z');
const payload = 'device=esp32-01;seq=7;uptime_s=120;temperature=25.7;simulated=1';
const record = {...C.publishing('Publishing: '+payload), time};
const message = {id:1, topic:'nbiot/test', received:new Date(time+2000).toISOString(), payload};
function check(name, test) {test();console.log('PASS '+name)}
check('exact MQTT payload matches Serial',()=>assert.equal(C.compare(record,[message],'nbiot/test',time+3000).status,'match'));
check('changed sensor value is a mismatch',()=>{
 const m={...message,payload:payload.replace('25.7','99.9')};
 assert.equal(C.compare(record,[m],'nbiot/test',time+3000).status,'different');
});
check('wrong device/topic and old database rows cannot match',()=>{
 for(const m of [{...message,topic:'wrong'}, {...message,payload:payload.replace('esp32-01','windows-simulator')}, {...message,received:new Date(time-60000).toISOString()}])
  assert.equal(C.compare(record,[m],'nbiot/test',time+3000).status,'waiting');
});
check('reset with same seq and different uptime is ambiguous',()=>{
 const m={...message,payload:payload.replace('uptime_s=120','uptime_s=10')};
 assert.equal(C.compare(record,[m],'nbiot/test',time+3000).status,'ambiguous');
});
check('late MQTT replaces waiting status',()=>{
 assert.equal(C.compare(record,[],'nbiot/test',time+181000).status,'waiting_late');
 assert.equal(C.compare(record,[message],'nbiot/test',time+181000).status,'match');
});
check('QoS duplicate counted and exact candidate preferred',()=>{
 const wrong={...message,id:2,payload:payload.replace('25.7','33.0')};
 const result=C.compare(record,[wrong,message,{...message,id:3}],'nbiot/test',time+3000);
 assert.equal(result.status,'match');assert.equal(result.copies,2);
});
check('payload whitespace is not silently normalized',()=>{
 const r={...C.publishing('Publishing: '+payload+' '),time};
 assert.equal(C.compare(r,[message],'nbiot/test',time+3000).status,'different');
});
check('fragmented CRLF, multiple lines, and oversized input',()=>{
 const b=new C.LineBuffer();assert.deepEqual(b.push('Publi'),[]);
 assert.deepEqual(b.push('shing: '+payload+'\r'),[]);
 assert.deepEqual(b.push('\nOK\n'),['Publishing: '+payload,'OK']);
 assert.deepEqual(b.push('x'.repeat(9000)+'\nnormal\n'),['normal']);
});
check('ordinary logs do not become transmit records',()=>{
 assert.equal(C.publishing('MQTT publish acknowledged'),null);
 assert.equal(C.publishing('Publishing: broken'),null);
});
check('JSON payload supported',()=>{
 const text=JSON.stringify({device:'esp32-01',seq:7,uptime_s:120});
 const r={...C.publishing('Publishing: '+text),time};
 assert.equal(C.compare(r,[{...message,payload:text}],'nbiot/test',time+3000).status,'match');
});
check('browser scripts parse',()=>{
 new vm.Script(fs.readFileSync(__dirname+'/serial_monitor.js','utf8'));
 new vm.Script(fs.readFileSync(__dirname+'/serial_compare.js','utf8'));
});

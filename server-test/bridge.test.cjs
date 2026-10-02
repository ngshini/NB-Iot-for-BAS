const test=require('node:test');
const assert=require('node:assert/strict');
test('relay preserves raw payload without generating sensor data',()=>{
 const {eventFor}=require('./bridge.cjs');
 const p=Buffer.from('{"sensor":"ES-WS-04","angle":123.4,"status":"ok"}');
 const e=eventFor('bas/BAS_TEST_001/telemetry',p,false);
 assert.equal(e.payload,p.toString());
 assert.equal(e.topic,'bas/BAS_TEST_001/telemetry');
 assert.equal(e.retained,false);
 assert.ok(e.received_ms>0);
});

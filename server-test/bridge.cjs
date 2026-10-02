const http=require('node:http');
function eventFor(topic,payload,retained){return {topic,payload:payload.toString('utf8'),retained:!!retained,received_ms:Date.now()};}
module.exports={eventFor};
if(require.main===module){
 const mqtt=require('mqtt');
 const topic='bas/BAS_TEST_001/telemetry',clients=new Set();
 let connected=false;
 const client=mqtt.connect('mqtts://test.mosquitto.org:8886',{clientId:'bas-web-relay-'+require('node:crypto').randomUUID(),rejectUnauthorized:true,family:4,minVersion:'TLSv1.2',maxVersion:'TLSv1.2',reconnectPeriod:5000,connectTimeout:30000});
 const send=(r,name,data)=>r.write(`event: ${name}\ndata: ${JSON.stringify(data)}\n\n`);
 client.on('connect',()=>client.subscribe(topic,{qos:0},err=>{connected=!err;for(const r of clients)send(r,'status',{connected});console.log('subscription',connected);}));
 client.on('offline',()=>{connected=false;for(const r of clients)send(r,'status',{connected});});
 client.on('error',err=>console.error('MQTT:',err.message));
 client.on('message',(t,p,packet)=>{if(t!==topic||p.length>65536)return;const e=eventFor(t,p,packet.retain);for(const r of clients)send(r,'telemetry',e);});
 http.createServer((req,res)=>{
  if(req.url==='/health'){res.writeHead(connected?200:503,{'Content-Type':'application/json'});return res.end(JSON.stringify({connected,topic}));}
  if(req.url!=='/events'){res.writeHead(404);return res.end();}
  if(clients.size>=100){res.writeHead(503);return res.end();}
  res.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-store','X-Accel-Buffering':'no'});
  res.write(': ready\n\n');clients.add(res);send(res,'status',{connected});
  const timer=setInterval(()=>res.write(': heartbeat\n\n'),15000);
  req.on('close',()=>{clearInterval(timer);clients.delete(res);});
 }).listen(18093,'0.0.0.0');
}

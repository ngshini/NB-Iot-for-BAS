import {buildMqttPacket,decodeMqttPackets,mqttString,parsePublishPacket} from './mqtt_core.mjs';
export const BROKERS=Object.freeze({mosquitto:'wss://test.mosquitto.org:8081/mqtt',emqx:'wss://broker.emqx.io:8084/mqtt'});
// Browser-only, read-only subscriptions. Never routes arbitrary hosts through BAS.
export class MqttReceiver {
 constructor({Socket=globalThis.WebSocket,onState=()=>{},onMessage=()=>{}}={}){Object.assign(this,{Socket,onState,onMessage});this.ws=null;}
 stop(){clearInterval(this.ping);clearTimeout(this.deadline);if(this.events){this.events.close();this.events=null;}const s=this.ws;this.ws=null;if(s){if(s.readyState===1)s.send(new Uint8Array([224,0]));s.close();}this.onState('disconnected');}
 connect(broker,topics){
  if(!Object.hasOwn(BROKERS,broker))throw Error('Chọn broker trong danh sách.');
  topics=[...new Set(topics)];
  if(!topics.length||topics.some(t=>!t||t.includes('\0')||new TextEncoder().encode(t).length>1024||t.split('/').some((p,i,a)=>(p.includes('#')&&(p!=='#'||i!==a.length-1))||(p.includes('+')&&p!=='+'))))throw Error('Topic không hợp lệ.');
  if(broker==='mosquitto'&&globalThis.location?.hostname==='server.aitrg.io.vn'){
   if(topics.length!==1||topics[0]!=='bas/BAS_TEST_001/telemetry')throw Error('Relay NB-IoT hiện chỉ nhận bas/BAS_TEST_001/telemetry.');
   this.stop();this.onState('connecting');const e=this.events=new EventSource('/iot-test/events');
   e.addEventListener('status',event=>{if(e!==this.events)return;const s=JSON.parse(event.data);this.onState(s.connected?'connected':'connecting');});
   e.addEventListener('telemetry',event=>{if(e===this.events)this.onMessage(JSON.parse(event.data));});
   e.onerror=()=>{if(e===this.events)this.onState('connecting','Relay đang kết nối lại.');};
   return;
  }
  this.stop();const s=this.ws=new this.Socket(BROKERS[broker],'mqtt');s.binaryType='arraybuffer';let remainder=new Uint8Array(),lastSeen=Date.now();
  const fail=message=>{if(s!==this.ws)return;this.stop();this.onState('error',message);};
  this.onState('connecting');this.deadline=setTimeout(()=>fail('Hết thời gian kết nối. Kiểm tra mạng và thử lại.'),20000);
  s.onopen=()=>{if(s!==this.ws)return;s.send(buildMqttPacket(16,[...mqttString('MQTT'),4,2,0,60,...mqttString('harbor-'+globalThis.crypto.randomUUID())]));};
  s.onmessage=event=>{
   if(s!==this.ws)return;lastSeen=Date.now();
   try{
    const chunk=new Uint8Array(event.data),merged=new Uint8Array(remainder.length+chunk.length);
    if(merged.length>1048576)throw Error('Bản tin vượt giới hạn 1 MB.');merged.set(remainder);merged.set(chunk,remainder.length);
    const decoded=decodeMqttPackets(merged);remainder=decoded.remainder;
    for(const p of decoded.packets){
     if(p.type===32){if(p.body.length!==2||p.body[1]!==0)throw Error('Broker từ chối CONNECT.');s.send(buildMqttPacket(130,[0,1,...topics.flatMap(t=>[...mqttString(t),0])]));}
     else if(p.type===144){if(p.body.length!==topics.length+2||p.body[0]!==0||p.body[1]!==1||p.body.slice(2).some(c=>c>2))throw Error('Broker từ chối SUBSCRIBE.');clearTimeout(this.deadline);this.onState('connected');clearInterval(this.ping);this.ping=setInterval(()=>{if(Date.now()-lastSeen>65000){fail('Broker không phản hồi. Bấm Nhận MQTT để kết nối lại.');return;}if(s.readyState===1)s.send(new Uint8Array([192,0]));},25000);}
     else if((p.type&240)===48){const message=parsePublishPacket(p);if((p.type&6)===2){const offset=2+((p.body[0]<<8)|p.body[1]);s.send(buildMqttPacket(64,p.body.slice(offset,offset+2)));}this.onMessage({...message,retained:!!(p.type&1),received_ms:Date.now()});}
    }
   }catch(e){fail(e.message);}
  };
  s.onerror=()=>fail('Không kết nối được WebSocket của broker. Kiểm tra mạng hoặc chọn lại broker.');
  s.onclose=()=>fail('Broker đã ngắt kết nối. Bấm Nhận MQTT để thử lại.');
 }
}

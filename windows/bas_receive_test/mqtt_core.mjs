export const BAS_FIELDS = [
  'distance', 'bowDistance', 'sternDistance', 'bowSpeed', 'sternSpeed', 'angle',
  'waterLevel', 'waterFlow', 'waterDirection', 'windForce', 'windDirection',
]

const encoder = new TextEncoder()
const decoder = new TextDecoder()

export function mqttString(value) {
  const bytes = encoder.encode(value)
  if (bytes.length > 65535) throw new RangeError('MQTT string is too long')
  return new Uint8Array([bytes.length >> 8, bytes.length & 255, ...bytes])
}

export function buildMqttPacket(type, body) {
  const data = body instanceof Uint8Array ? body : new Uint8Array(body)
  const remainingLength = []
  let length = data.length
  do {
    let digit = length % 128
    length = Math.floor(length / 128)
    if (length) digit |= 0x80
    remainingLength.push(digit)
  } while (length)
  return new Uint8Array([type, ...remainingLength, ...data])
}

export function decodeMqttPackets(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input)
  const packets = []
  let cursor = 0

  while (cursor < bytes.length) {
    const start = cursor
    if (bytes.length - cursor < 2) break
    const type = bytes[cursor++]
    let length = 0
    let multiplier = 1
    let digit
    let count = 0
    do {
      if (cursor >= bytes.length) return { packets, remainder: bytes.slice(start) }
      digit = bytes[cursor++]
      length += (digit & 0x7f) * multiplier
      multiplier *= 128
      count++
      if (count > 4) throw new Error('Invalid MQTT remaining length')
    } while (digit & 0x80)

    if (bytes.length - cursor < length) return { packets, remainder: bytes.slice(start) }
    packets.push({ type, body: bytes.slice(cursor, cursor + length) })
    cursor += length
  }

  return { packets, remainder: bytes.slice(cursor) }
}

export function parsePublishPacket(packet) {
  if ((packet.type & 0xf0) !== 0x30 || packet.body.length < 2) {
    throw new Error('Not an MQTT PUBLISH packet')
  }
  const topicLength = (packet.body[0] << 8) | packet.body[1]
  let offset = 2 + topicLength
  if (offset > packet.body.length) throw new Error('Invalid MQTT topic length')
  const topic = decoder.decode(packet.body.slice(2, offset))
  if ((packet.type & 0x06) !== 0) offset += 2
  if (offset > packet.body.length) throw new Error('Invalid MQTT packet identifier')
  return { topic, payload: decoder.decode(packet.body.slice(offset)) }
}

export function flattenTelemetry(value, prefix = '') {
  const rows = []
  const visit = (current, path) => {
    if (Array.isArray(current)) {
      if (!current.length) rows.push({ path, value: [] })
      else current.forEach((item, index) => visit(item, `${path}[${index}]`))
      return
    }
    if (current && typeof current === 'object') {
      const entries = Object.entries(current)
      if (!entries.length) rows.push({ path, value: {} })
      else entries.forEach(([key, item]) => visit(item, path ? `${path}.${key}` : key))
      return
    }
    rows.push({ path: path || 'value', value: current })
  }
  visit(value, prefix)
  return rows
}

export function validateBasTelemetry(text) {
  let data
  try {
    data = JSON.parse(text)
  } catch (error) {
    return { ok: false, error: `JSON không hợp lệ: ${error.message}` }
  }
  if (!data || typeof data !== 'object' || Array.isArray(data)) {
    return { ok: false, error: 'Payload phải là một JSON object' }
  }
  const present = BAS_FIELDS.filter(field => Object.hasOwn(data, field))
  if (!present.length) return { ok: false, error: 'JSON hợp lệ nhưng không có trường telemetry BAS', data }
  const numericFields = present.filter(field => !field.endsWith('Direction'))
  const invalid = numericFields.filter(field => typeof data[field] !== 'number' || !Number.isFinite(data[field]))
  if (invalid.length) return { ok: false, error: `Giá trị không phải số: ${invalid.join(', ')}`, data }
  const keys = Object.keys(data)
  const kind = keys.length === 1 && keys[0] === 'distance'
    ? 'minimal'
    : keys.some(field => !BAS_FIELDS.includes(field)) ? 'extended' : 'full'
  return {
    ok: true,
    kind,
    present,
    missing: BAS_FIELDS.filter(field => !Object.hasOwn(data, field)),
    data,
  }
}

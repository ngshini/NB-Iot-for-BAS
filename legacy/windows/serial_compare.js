/* Pure comparison helpers shared by the monitor and automated tests. */
(function(root) {
  'use strict';
  function fields(payload) {
    try { const d = JSON.parse(payload); if (d && typeof d === 'object' && !Array.isArray(d)) return d; } catch (_) {}
    const d = Object.create(null);
    for (const part of payload.split(';')) {
      const at = part.indexOf('=');
      if (at > 0) d[part.slice(0, at)] = part.slice(at + 1);
    }
    return d;
  }
  function publishing(line) {
    if (!line.startsWith('Publishing: ')) return null;
    const payload = line.slice('Publishing: '.length); // Preserve actual payload whitespace.
    const data = fields(payload);
    if (data.device === undefined || data.seq === undefined) return null;
    return {payload, data};
  }
  function compare(record, messages, topic, now) {
    const a = record.data;
    const candidates = messages.filter(m => {
      const b = fields(m.payload);
      const received = Date.parse(m.received);
      // Match only the expected topic, device, seq and a bounded receive window.
      // Small negative tolerance allows OS/browser USB buffering.
      return m.topic === topic && String(b.device) === String(a.device) &&
        String(b.seq) === String(a.seq) && received >= record.time - 3000 &&
        received <= record.time + 180000;
    });
    const exact = candidates.find(m => m.payload === record.payload);
    if (exact) return {status: 'match', mqtt: exact, copies: candidates.filter(m => m.payload === record.payload).length};
    // uptime distinguishes repeated seq values after a reset. Do not declare a
    // mismatch against another boot's packet. Without uptime the result is ambiguous.
    const sameBoot = candidates.find(m => a.uptime_s !== undefined &&
      String(fields(m.payload).uptime_s) === String(a.uptime_s));
    if (sameBoot) return {status: 'different', mqtt: sameBoot};
    if (candidates.length) return {status: 'ambiguous', mqtt: candidates[0]};
    return {status: now - record.time > 180000 ? 'waiting_late' : 'waiting'};
  }
  class LineBuffer {
    constructor() { this.pending = ''; this.discard = false; }
    push(text) {
      const lines = [];
      for (const c of text) {
        if (c === '\n') {
          if (!this.discard) lines.push(this.pending.endsWith('\r') ? this.pending.slice(0, -1) : this.pending);
          this.pending = ''; this.discard = false;
        } else if (!this.discard) {
          this.pending += c;
          if (this.pending.length > 8192) { this.pending = ''; this.discard = true; }
        }
      }
      return lines;
    }
  }
  const api = {fields, publishing, compare, LineBuffer};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.SerialCompare = api;
})(typeof globalThis === 'undefined' ? this : globalThis);

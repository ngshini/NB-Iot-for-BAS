#pragma once
#include "tf03_parser.h"
// Compile-time protocol fixtures: 125 cm, strength 350, checksum 0x8e.
constexpr bool parserChecks() {
  constexpr uint8_t good[]={0x59,0x59,0x7d,0,0x5e,1,0,0,0x8e};
  Tf03Parser p;
  for(unsigned i=0;i<8;++i)if(p.feed(good[i]))return false;
  if(!p.feed(good[8])||p.distance!=125||p.strength!=350)return false;
  // Corrupted checksum must not emit a sample.
  for(unsigned i=0;i<8;++i)if(p.feed(good[i]))return false;
  if(p.feed(0))return false;
  // Noise and a partial header must not prevent recovery.
  p.feed(0x44);p.feed(0x59);
  unsigned emitted=0;
  for(auto b:good)if(p.feed(b))++emitted;
  if(emitted!=1||p.distance!=125||p.strength!=350)return false;
  // A dropped byte followed by a good frame must recover exactly once.
  for(unsigned i=0;i<9;++i)if(i!=3 && p.feed(good[i]))return false;
  emitted=0;for(auto b:good)if(p.feed(b))++emitted;
  return emitted==1 && p.distance==125 && p.strength==350;
}
static_assert(parserChecks(), "TF03 parser protocol regression");

import {test} from 'node:test';
import assert from 'node:assert/strict';
import {parseOLED,parseParams,parseBank,parsePhrase,rgba} from '../web/protocol.mjs';
test('strict versioned snapshots and SSD1306 page layout',()=>{
  const bytes=new Uint8Array(1024);bytes[0]=1;bytes[128+127]=128;
  const image=rgba(bytes,false);
  const at=(x,y)=>image[(y*128+x)*4];
  assert.equal(at(0,0),255);assert.equal(at(0,1),0);assert.equal(at(127,15),255);
  assert.equal(at(127,8),0);assert.equal(rgba(bytes,true)[0],0);assert.equal(rgba(bytes,true)[4],255);
  const frame=parseOLED('PVS OLED 1 42 15 1 1 '+'ab'.repeat(64));
  assert.equal(frame.sequence,42);assert.equal(frame.chunk,15);assert.equal(frame.inverted,true);
  assert.equal(frame.bytes.length,64);assert.equal(frame.bytes[63],171);
  for(const line of ['PVS OLED 2 1 0 0 1 '+'00'.repeat(64),'PVS OLED 1 1 16 0 1 '+'00'.repeat(64),'PVS OLED 1 1 0 0 1 00','Storage: ready'])assert.equal(parseOLED(line),null);
});

test('parameter values, ownership and rejection results are validated',()=>{
  const valid='PVS PARAMS 1 72 96 128 128 22050 8 3 12 1 1 0 ok';
  const state=parseParams(valid);
  assert.deepEqual(state.values,[72,96,128,128,22050,8]);
  assert.equal(state.hardware,3);assert.equal(state.locked,12);assert.equal(state.busy,true);
  assert.equal(parseParams(valid.replace(' ok',' hardware')).result,'hardware');
  for(const line of [valid.replace('1 72','1 0'),valid.replace('22050','999999'),valid.replace('3 12','64 12'),valid.replace(' ok',' wrong'),valid+' extra',valid.replace('1 72','1 -1'),valid.replace('8 3','9 3')])assert.equal(parseParams(line),null);
});

test('bank and phrase packets validate lengths, keys, locks and empty slots',()=>{
  assert.deepEqual(parseBank('PVS BANK 1 5 42 -1'),{bank:5,revision:42,playing:-1});
  assert.equal(parseBank('PVS BANK 1 13 42 0'),null);
  assert.equal(parseBank('PVS BANK 1 1 42 12'),null);
  const phrase=parsePhrase('PVS PHRASE 1 5 42 0 0 5 S 7 48454c4c4f');
  assert.equal(new TextDecoder().decode(phrase.bytes),'HELLO');assert.equal(phrase.locks,7);
  assert.equal(parsePhrase('PVS PHRASE 1 5 42 11 0 0 E 0 -').kind,'E');
  assert.equal(parsePhrase('PVS PHRASE 1 5 42 0 7 5 S 0 -').bytes.length,0);
  for(const line of ['PVS PHRASE 1 5 42 12 0 0 E 0 -','PVS PHRASE 1 5 42 0 0 5 S 0 00','PVS PHRASE 1 5 42 0 0 0 E 1 -','PVS PHRASE 1 5 42 0 0 384 S 0 00'])assert.equal(parsePhrase(line),null);
});

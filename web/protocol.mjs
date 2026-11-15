// SSD1306 page addressing: each byte stores one column of eight vertical pixels.
export function parseOLED(line) {
  const match = /^PVS OLED 1 (\d+) (\d+) ([01]) ([01]) ([0-9a-f]{128})$/.exec(line.trim());
  if (!match || Number(match[2]) > 15) return null;
  return {sequence:Number(match[1]), chunk:Number(match[2]), inverted:match[3]==='1',
    online:match[4]==='1', bytes:Uint8Array.from(match[5].match(/../g), value=>parseInt(value,16))};
}
export function rgba(pixels, inverted) {
  const result = new Uint8ClampedArray(128 * 64 * 4);
  for (let y=0; y<64; y++) for (let x=0; x<128; x++) {
    const lit = Boolean(pixels[(y >> 3)*128+x] & (1 << (y & 7))) !== inverted;
    const i=(y*128+x)*4;
    result[i]=result[i+1]=result[i+2]=lit ? 255 : 0;
    result[i+3]=255;
  }
  return result;
}

export const parameters = [
  {name:'speed', label:'Speed', min:1, max:255, unit:''},
  {name:'pitch', label:'Pitch', min:0, max:255, unit:''},
  {name:'mouth', label:'Mouth', min:0, max:255, unit:''},
  {name:'throat', label:'Throat', min:0, max:255, unit:''},
  {name:'rate', label:'Sample rate', min:8000, max:88200, unit:'Hz'},
  {name:'bits', label:'Bit depth', min:1, max:8, unit:'bit'},
];
export function parseParams(line) {
  const fields=line.trim().split(/\s+/);
  if(fields.length!==15||fields.slice(0,3).join(' ')!=='PVS PARAMS 1')return null;
  if(!fields.slice(3,14).every(value=>/^\d+$/.test(value)))return null;
  const values=fields.slice(3,9).map(Number), [hardware,locked,busy,editable,mode]=fields.slice(9,14).map(Number);
  if(values.some((value,i)=>value<parameters[i].min||value>parameters[i].max)||hardware>63||locked>63||busy>1||editable>1||mode>6)return null;
  if(!['ok','hardware','range','unavailable','locked','parameter','syntax'].includes(fields[14]))return null;
  return {values,hardware,locked,busy:Boolean(busy),editable:Boolean(editable),mode,result:fields[14]};
}

export function parseBank(line) {
  const match=/^PVS BANK 1 (\d+) (\d+) (-1|\d+)$/.exec(line.trim());
  if(!match)return null;
  const [bank,revision,playing]=match.slice(1).map(Number);
  if(bank<1||bank>12||revision>0xffffffff||playing>11)return null;
  return {bank,revision,playing};
}
export function parsePhrase(line) {
  const match=/^PVS PHRASE 1 (\d+) (\d+) (\d+) (\d+) (\d+) ([SPE]) ([0-7]) (-|[0-9a-f]+)$/.exec(line.trim());
  if(!match)return null;
  const [bank,revision,key,chunk,length]=match.slice(1,6).map(Number);
  const kind=match[6],locks=Number(match[7]),hex=match[8];
  if(bank<1||bank>12||revision>0xffffffff||key>11||chunk>7||length>383)return null;
  const expected=Math.max(0,Math.min(48,length-chunk*48));
  if(hex==='-'?expected!==0:hex.length!==expected*2)return null;
  if(kind==='E'&&(length||locks))return null;
  const bytes=hex==='-'?new Uint8Array():Uint8Array.from(hex.match(/../g),value=>parseInt(value,16));
  return {bank,revision,key,chunk,length,kind,locks,bytes};
}

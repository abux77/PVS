import {parseOLED, parseParams, parseBank, parsePhrase, parameters, rgba} from './protocol.mjs';
const $=id=>document.getElementById(id);
const canvas=$('oled'), context=canvas.getContext('2d');
const encoder=new TextEncoder(), edits=new Map(), drafts=new Set();
let port, reader, writer, running=false, connecting=false, readTask, pollTask, pending, lastFrame=0, closing, controlState, diagnosticsRequested=false;
let manuallyDisconnected=false;
const pvsFilter={usbVendorId:0xcafe,usbProductId:0x4003};
const keyLabels=[...'123456789*0#'];
let phraseCache, phraseMetadata, queuedKey=null;
const modes=['NORMAL','VOICE','SAMPLE','CHROMATIC','PRESET','USB EDIT','FIRMWARE UPDATE'];
const rejection={hardware:'The physical knob has taken control. Switch hardware mode to edit this parameter here.',locked:'A phrase lock is controlling that parameter during playback.',unavailable:'Voice editing is unavailable during USB editing or a firmware update.',range:'That value is outside the hardware range.',parameter:'The firmware did not recognize this parameter.',syntax:'The firmware could not read this edit.'};
function status(message){$('status').textContent=message;}
function log(line){const el=$('log');el.textContent=(el.textContent==='No messages yet.'?'':el.textContent+'\n')+line;el.textContent=el.textContent.slice(-12000);el.scrollTop=el.scrollHeight;}
function render(pixels,inverted){context.putImageData(new ImageData(rgba(pixels,inverted),128,64),0,0);}
function renderControls(state){
  controlState=state;
  $('voice-mode').textContent=!running?(state?'DISCONNECTED / LAST VALUES':'CONNECT TO READ VALUES'):`${modes[state.mode]}${state.busy?' / PLAYING':''}`;
  $('hardware-mode').textContent=running&&state?modes[state.mode]:'DISCONNECTED';
  parameters.forEach((parameter,index)=>{
    const card=$(`control-${parameter.name}`), bit=1<<index;
    const hardware=Boolean(state?.hardware&bit), locked=Boolean(state?.locked&bit);
    const disabled=!running||!state?.editable||hardware||locked;
    card.classList.toggle('hardware',hardware);
    card.querySelector('.owner').textContent=!running||!state?'OFFLINE':locked?'PHRASE LOCK':hardware?'KNOB ACTIVE':!state.editable?'READ ONLY':'WEB EDITABLE';
    for(const input of card.querySelectorAll('input')){
      input.disabled=disabled;
      if(state&&!drafts.has(parameter.name)&&!edits.has(parameter.name))input.value=state.values[index];
    }
  });
  renderPhraseButtons();
}
function renderPhraseButtons(){
  const ready=phraseCache&&phraseMetadata&&phraseCache.bank===phraseMetadata.bank&&phraseCache.revision===phraseMetadata.revision;
  $('bank-label').textContent=phraseMetadata?`BANK ${String(phraseMetadata.bank).padStart(2,'0')}`:'BANK —';
  keyLabels.forEach((label,index)=>{
    const card=$(`phrase-key-${index}`);if(!card)return;
    const phrase=phraseCache?.phrases[index];
    card.disabled=!running||!controlState?.editable||!ready||!phrase||phrase.kind==='E';
    card.classList.toggle('playing',Boolean(running&&ready&&phraseMetadata.playing===index));
    card.classList.toggle('empty',phrase?.kind==='E');
    if(card.phraseData===phrase)return;
    card.phraseData=phrase;
    card.querySelector('.phrase-kind').textContent=!phrase?'—':phrase.kind==='P'?'PHONETIC':phrase.kind==='S'?'TEXT':'EMPTY';
    card.querySelector('.phrase-text').textContent=phrase?.text||(phrase?.kind==='E'?'Unassigned':'Waiting for PVS…');
    const locks=card.querySelector('.phrase-locks');locks.replaceChildren();
    for(const [bit,name] of [[1,'SPEED / PITCH'],[2,'MOUTH / THROAT'],[4,'RATE / BITS']]){
      if(!(phrase?.locks&bit))continue;
      const badge=document.createElement('span');badge.className='lock-badge';badge.textContent=`LOCK · ${name}`;locks.append(badge);
    }
    if(!locks.childElementCount){const span=document.createElement('span');span.className='no-locks';span.textContent=phrase?'NO LOCKS':'—';locks.append(span);}
    card.title=`Key ${label}${phrase?`: ${phrase.text}`:''}`;
    card.setAttribute('aria-label',`Key ${label}${phrase?`, ${phrase.text||'Unassigned'}`:''}${phrase?.locks?', parameter locks':''}`);
  });
}
keyLabels.forEach((label,index)=>{
  const card=document.createElement('button');card.type='button';card.className='phrase-key';card.id=`phrase-key-${index}`;card.disabled=true;
  card.innerHTML=`<span class="phrase-key-top"><span class="key-number">${label}</span><span class="phrase-kind">—</span></span><span class="phrase-text">Waiting for PVS…</span><span class="phrase-locks"></span>`;
  card.addEventListener('click',()=>{if(running&&!card.disabled){queuedKey=index;$('phrase-status').textContent=`Starting key ${label}…`;}});
  $('phrase-grid').append(card);
});
parameters.forEach(parameter=>{
  const card=document.createElement('div');card.className='parameter';card.id=`control-${parameter.name}`;
  card.innerHTML=`<div class="parameter-top"><label for="${parameter.name}-value">${parameter.label}</label><span class="owner">OFFLINE</span></div><div class="parameter-readout"><input id="${parameter.name}-value" type="number" min="${parameter.min}" max="${parameter.max}" step="1" disabled aria-label="${parameter.label} value"><small>${parameter.unit}</small></div><input id="${parameter.name}-slider" type="range" min="${parameter.min}" max="${parameter.max}" step="1" disabled aria-label="${parameter.label}">`;
  $('controls').append(card);
  for(const input of card.querySelectorAll('input')){
    input.addEventListener('input',()=>{
      drafts.add(parameter.name);
      for(const other of card.querySelectorAll('input'))if(other!==input&&input.value!=='')other.value=input.value;
    });
    input.addEventListener('change',()=>{
      drafts.delete(parameter.name);
      const value=Number(input.value);
      if(!input.value||!Number.isInteger(value)||value<parameter.min||value>parameter.max){
        $('control-status').textContent=`${parameter.label} must be ${parameter.min}–${parameter.max}.`;renderControls(controlState);return;
      }
      if(!running||input.disabled)return;
      edits.set(parameter.name,value);$('control-status').textContent=`Sending ${parameter.label.toLowerCase()}…`;
    });
    input.addEventListener('blur',()=>{if(drafts.delete(parameter.name))renderControls(controlState);});
  }
});
renderControls(null);
async function refreshBank(){
  const bank=await request('bank','bank');phraseMetadata=bank;renderPhraseButtons();
  if(phraseCache?.revision===bank.revision&&phraseCache.bank===bank.bank)return;
  $('phrase-status').textContent=`Reading bank ${bank.bank}…`;
  const phrases=[];
  for(let key=0;key<12&&running;key++){
    let first,bytes;
    for(let chunk=0;;chunk++){
      let part;
      try{part=await request(`phrase ${key} ${chunk}`,'phrase',chunk,key);}
      catch(error){
        if(!running)throw error;
        // A bank may become shorter between requests. Read its new revision
        // on the next poll instead of disconnecting the instrument.
        $('phrase-status').textContent='Refreshing the phrase bank…';
        return;
      }
      if(part.revision!==bank.revision||part.bank!==bank.bank)return;
      if(!first){first=part;bytes=new Uint8Array(part.length);}
      if(part.length!==first.length||part.kind!==first.kind||part.locks!==first.locks)return;
      bytes.set(part.bytes,chunk*48);
      if((chunk+1)*48>=first.length)break;
    }
    phrases.push({kind:first.kind,locks:first.locks,text:new TextDecoder().decode(bytes)});
  }
  if(!running)return;
  phraseCache={...bank,phrases};renderPhraseButtons();
  $('phrase-status').textContent=`Bank ${bank.bank} · ${phrases.filter(phrase=>phrase.kind!=='E').length} loaded phrases`;
}
function rejectPending(message){if(pending){clearTimeout(pending.timer);pending.reject(new Error(message));pending=null;}}
async function readLines(){
  const decoder=new TextDecoder();let buffer='';
  try {
    while(running){const {value,done}=await reader.read();if(done)break;buffer+=decoder.decode(value,{stream:true});
      let end;while((end=buffer.indexOf('\n'))>=0){
        const line=buffer.slice(0,end).trim();buffer=buffer.slice(end+1);
        // Nonblocking console diagnostics may end mid-line. A complete
        // protocol packet following them still has its own unique prefix.
        const frame=parseOLED(line.slice(Math.max(0,line.indexOf('PVS OLED 1 '))));
        const params=parseParams(line.slice(Math.max(0,line.indexOf('PVS PARAMS 1 '))));
        const bank=parseBank(line.slice(Math.max(0,line.indexOf('PVS BANK 1 '))));
        const phrase=parsePhrase(line.slice(Math.max(0,line.indexOf('PVS PHRASE 1 '))));
        const response=frame||params||bank||phrase;
        if(response&&pending&&((frame&&pending.kind==='oled'&&frame.chunk===pending.chunk)||(params&&pending.kind==='params')||(bank&&pending.kind==='bank')||(phrase&&pending.kind==='phrase'&&phrase.key===pending.key&&phrase.chunk===pending.chunk))){
          const request=pending;pending=null;clearTimeout(request.timer);request.resolve(response);
        }else if(line&&!response)log(line);
      }
      if(buffer.length>16000)buffer='';
    }
  }catch(error){if(running){log(error.message);status(error.message);}}
  finally{if(running)status('USB connection ended. Reconnect PVS to resume.');running=false;rejectPending('USB connection ended.');reader.releaseLock();}
}
async function request(command,kind,chunk,key){
  const response=new Promise((resolve,reject)=>{
    pending={kind,chunk,key,resolve,reject,timer:setTimeout(()=>rejectPending(`No ${kind} response. Update the PVS companion firmware and reconnect.`),1500)};
  });
  try{await writer.write(encoder.encode(command+'\n'));}catch(error){rejectPending(error.message);}
  return response;
}
async function poll(){
  try{
    while(running){
      const pixels=new Uint8Array(1024);let first;
      for(let chunk=0;chunk<16&&running;chunk++){
        const part=await request(`oled ${chunk}`,'oled',chunk);
        if(!first)first=part;
        if(part.sequence!==first.sequence||part.inverted!==first.inverted||part.online!==first.online)throw new Error('Display snapshot interrupted. Reconnect PVS.');
        pixels.set(part.bytes,chunk*64);
      }
      if(!running)break;
      render(pixels,first.inverted);lastFrame=Date.now();
      $('oled').setAttribute('aria-label',first.online?'Live PVS OLED display, mirrored from connected hardware.':'Live virtual PVS display for an instrument without an OLED.');
      $('frame-info').textContent=`LIVE / FRAME ${first.sequence}`;$('connection').textContent=first.online?'LIVE DISPLAY':'VIRTUAL DISPLAY';$('led').classList.add('live');
      status(first.online?'Connected to PVS.':'Connected to PVS. Showing its virtual display; no hardware OLED is connected.');
      if(diagnosticsRequested){
        diagnosticsRequested=false;
        await writer.write(encoder.encode('status\n'));
        // Let the bounded firmware TX queue drain before another response.
        await new Promise(resolve=>setTimeout(resolve,100));
      }
      // One outstanding request at a time; edits cannot interleave OLED chunks.
      if(queuedKey!==null&&running){
        const key=queuedKey;queuedKey=null;
        const state=await request(`key ${key}`,'params');renderControls(state);
        $('phrase-status').textContent=state.result==='ok'?`Triggered key ${keyLabels[key]}.`:rejection[state.result];
      }
      for(const [name,value] of [...edits]){
        if(!running)break;
        edits.delete(name);
        const state=await request(`set ${name} ${value}`,'params');renderControls(state);
        const label=parameters.find(parameter=>parameter.name===name).label;
        $('control-status').textContent=state.result==='ok'?`${label} updated on PVS.`:rejection[state.result];
      }
      if(!running)break;
      const state=await request('params','params');renderControls(state);
      await refreshBank();
      if($('control-status').textContent.startsWith('Connect PVS'))$('control-status').textContent='Values follow PVS. Edit any parameter marked WEB EDITABLE.';
      await new Promise(resolve=>setTimeout(resolve,100));
    }
  }catch(error){if(running){status(error.message);log(error.message);}}
  finally{if(port)await disconnect(false);}
}
function disconnect(manual=true){
  if(manual)manuallyDisconnected=true;
  if(closing)return closing;
  closing=closePort(manual).finally(()=>{closing=null;});return closing;
}
async function closePort(manual){
  running=false;rejectPending('Disconnected.');edits.clear();drafts.clear();diagnosticsRequested=false;queuedKey=null;
  try{await reader?.cancel();await readTask;}catch{}
  try{writer?.releaseLock();await port?.close();}catch(error){log(error.message);}
  reader=writer=port=null;renderControls(controlState);
  $('connect').textContent='Connect PVS';$('diagnostics').disabled=true;$('led').classList.remove('live');$('connection').textContent='DISCONNECTED';
  $('frame-info').textContent=lastFrame?'LAST FRAME / DISCONNECTED':'WAITING FOR HARDWARE';
  $('control-status').textContent='Connect PVS to read and edit its six voice parameters.';
  $('phrase-status').textContent='Disconnected. Reconnect to read the current bank.';
  if(manual)status('Disconnected. Connect PVS to resume the live display.');
}
async function connectPVS(choosePort){
  if(connecting||running)return;
  connecting=true;$('connect').disabled=true;
  try{
    // Complete the previous connection's cleanup before starting new readers.
    if(closing)await closing;
    if(port)await disconnect(false);
    if(pollTask)await pollTask;
    const selected=await choosePort();
    if(!selected)return;
    await selected.open({baudRate:115200,bufferSize:4096});port=selected;
    await port.setSignals({dataTerminalReady:true});
    reader=port.readable.getReader();writer=port.writable.getWriter();running=true;lastFrame=0;
    phraseCache=phraseMetadata=null;renderPhraseButtons();
    $('connect').textContent='Disconnect';$('diagnostics').disabled=false;status('Connected. Waiting for the first display snapshot…');
    readTask=readLines();pollTask=poll();
  }catch(error){status(error.name==='NotFoundError'?'No port selected. Connect when you are ready.':error.message);if(port)await disconnect(false);}
  finally{connecting=false;$('connect').disabled=false;}
}
function autoConnect(){
  if(manuallyDisconnected)return;
  return connectPVS(async()=>{
    const permitted=await navigator.serial.getPorts();
    const candidates=permitted.filter(candidate=>{
      const info=candidate.getInfo();
      return info.usbVendorId===pvsFilter.usbVendorId&&info.usbProductId===pvsFilter.usbProductId;
    });
    if(candidates.length===1)return candidates[0];
    if(candidates.length>1)status('Multiple PVS ports are available. Choose one with Connect PVS.');
    return null;
  });
}
$('connect').addEventListener('click',async()=>{
  if(connecting)return;
  if(port){$('connect').disabled=true;await disconnect();await pollTask;$('connect').disabled=false;return;}
  manuallyDisconnected=false;
  await connectPVS(()=>navigator.serial.requestPort({filters:[pvsFilter]}));
});
$('diagnostics').addEventListener('click',()=>{if(running)diagnosticsRequested=true;});
$('scale').addEventListener('change',()=>{const scale=Number($('scale').value);canvas.style.width=`${128*scale}px`;canvas.style.height=`${64*scale}px`;});
render(new Uint8Array(1024),false);
if(!('serial' in navigator)||!window.isSecureContext){$('connect').disabled=true;status('Open this page in desktop Chrome or Edge at localhost or over HTTPS to connect to PVS.');}
else{
  navigator.serial.addEventListener('connect',()=>{void autoConnect();});
  navigator.serial.addEventListener('disconnect',event=>{
    if(event.target===port||event.port===port)void disconnect(false);
  });
  void autoConnect();
}

"""Optional UI integration checks: pip install playwright; use system Chromium."""
import os
from playwright.sync_api import sync_playwright

with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=os.environ.get('CHROMIUM', '/usr/bin/chromium'), headless=True, args=['--no-sandbox'])
    page = browser.new_page(viewport={'width':1280,'height':960})
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.add_init_script('''
      window.mockState={online:true,bank:5,revision:1,playing:-1,values:[72,96,128,128,22050,8],hardware:3,locked:0,mode:0};
      window.mockPhrases=Array.from({length:12},(_,key)=>({text:key===11?'':key===2?'A'.repeat(383):key===3?'<img src=x onerror="window.badPhrase=true">':`Phrase ${key+1}`,kind:key===11?'E':key===1?'P':'S',locks:key===0?7:key===1?2:0}));
      window.mockTriggered=-1;
      let controller,sequence=0;
      window.mockPickerCalls=0;window.mockOpens=0;window.mockAttached=true;
      let granted=Boolean(sessionStorage.getItem('mockPVSGranted'));
      const names=['speed','pitch','mouth','throat','rate','bits'];
      function reply(text){controller.enqueue(new TextEncoder().encode(text+'\\n'));}
      function params(result='ok'){const s=window.mockState;reply(`PVS PARAMS 1 ${s.values.join(' ')} ${s.hardware} ${s.locked} 0 1 ${s.mode} ${result}`);}
      const port={getInfo(){return {usbVendorId:0xcafe,usbProductId:0x4003};},async open(){window.mockOpens++;this.readable=new ReadableStream({start(c){controller=c;}});
        this.writable=new WritableStream({write(data){const words=new TextDecoder().decode(data).trim().split(' ');
          if(words[0]==='oled'){const chunk=Number(words[1]);if(!chunk)sequence++;
            reply(`PVS OLED 1 ${sequence} ${chunk} 0 ${window.mockState.online?1:0} ${'00'.repeat(64)}`);}
          else if(words[0]==='bank'){const s=window.mockState;reply(`PVS BANK 1 ${s.bank} ${s.revision} ${s.playing}`);}
          else if(words[0]==='phrase'){
            const key=Number(words[1]),chunk=Number(words[2]);
            if(window.mockSwapOnChunk&&key===2&&chunk===1){window.mockSwapOnChunk=false;window.mockState.bank=6;window.mockState.revision++;window.mockPhrases[0].text='New bank phrase';return;}
            const s=window.mockState,p=window.mockPhrases[key],bytes=new TextEncoder().encode(p.text);
            const hex=Array.from(bytes.slice(chunk*48,chunk*48+48),value=>value.toString(16).padStart(2,'0')).join('')||'-';
            reply(`PVS PHRASE 1 ${s.bank} ${s.revision} ${key} ${chunk} ${bytes.length} ${p.kind} ${p.locks} ${hex}`);
          }
          else if(words[0]==='key'){window.mockTriggered=Number(words[1]);window.mockState.playing=Number(words[1]);params();}
          else if(words[0]==='params')params();
          else if(words[0]==='set'){const index=names.indexOf(words[1]),s=window.mockState;
            if(s.hardware&(1<<index))params('hardware');
            else if(s.locked&(1<<index))params('locked');
            else{s.values[index]=Number(words[2]);params();}}
          else {reply('Firmware build: test');controller.enqueue(new TextEncoder().encode('unfinished diagnostic'));}
        }});},async setSignals(){},async close(){}};
      const serial=new EventTarget();
      serial.requestPort=async()=>{window.mockPickerCalls++;granted=true;sessionStorage.setItem('mockPVSGranted','1');return port;};
      serial.getPorts=async()=>[{getInfo:()=>({usbVendorId:123,usbProductId:456}),open:()=>{throw new Error('Unrelated serial device opened');}},...(granted&&window.mockAttached?[port]:[])];
      window.mockPlug=()=>{window.mockAttached=true;serial.dispatchEvent(new Event('connect'));};
      window.mockUnplug=()=>{window.mockAttached=false;const event=new Event('disconnect');event.port=port;serial.dispatchEvent(event);};
      Object.defineProperty(navigator,'serial',{value:serial});
    ''')
    page.goto(os.environ.get('PVS_TEST_URL','http://127.0.0.1:8877'))
    assert page.locator('#controls input').count()==12
    assert page.locator('#mouth-value').is_disabled()
    assert page.evaluate('window.mockPickerCalls')==0
    page.locator('#connect').click()
    page.wait_for_function("document.querySelector('#mouth-value').value==='128'")
    page.wait_for_function("!document.querySelector('#phrase-key-0').disabled")
    assert page.locator('#phrase-grid > button').count()==12
    assert page.locator('#phrase-key-0 .lock-badge').count()==3
    assert page.locator('#phrase-key-1 .phrase-kind').inner_text()=='PHONETIC'
    assert len(page.locator('#phrase-key-2 .phrase-text').inner_text())==383
    assert page.locator('#phrase-key-3 img').count()==0
    assert page.locator('#phrase-key-11').is_disabled()
    assert page.locator('#hardware-mode').inner_text()=='NORMAL'
    page.locator('#phrase-key-0').click()
    page.wait_for_function('window.mockTriggered===0')
    page.wait_for_function("document.querySelector('#phrase-key-0').classList.contains('playing')")
    # Changing bank mid-transfer must not commit mixed phrase metadata.
    page.evaluate('window.mockSwapOnChunk=true;window.mockState.revision++')
    page.wait_for_function("document.querySelector('#bank-label').textContent==='BANK 06' && document.querySelector('#phrase-key-0 .phrase-text').textContent==='New bank phrase' && !document.querySelector('#phrase-key-0').disabled")
    assert page.locator('#speed-value').is_disabled()
    assert page.locator('#pitch-slider').is_disabled()
    assert page.locator('#mouth-value').is_enabled()
    page.locator('#mouth-value').fill('155')
    page.locator('#mouth-value').press('Tab')
    page.wait_for_function('window.mockState.values[2]===155')
    page.wait_for_function("document.querySelector('#control-status').textContent==='Mouth updated on PVS.'")
    # Physical movement and mode changes reach the page without web input.
    page.evaluate('window.mockState.values[0]=111;window.mockState.hardware=12;window.mockState.mode=1')
    page.wait_for_function("document.querySelector('#speed-value').value==='111' && document.querySelector('#mouth-value').disabled")
    assert page.locator('#hardware-mode').inner_text()=='VOICE'
    assert page.locator('#speed-value').is_enabled()
    assert page.locator('#mouth-slider').is_disabled()
    page.evaluate('window.mockState.locked=48')
    page.wait_for_function("document.querySelector('#bits-value').disabled")
    assert page.locator('#control-rate .owner').inner_text()=='PHRASE LOCK'
    # Firmware wins if ownership changes between UI telemetry and the write.
    page.locator('#speed-value').fill('140')
    page.evaluate('window.mockState.hardware|=1')
    page.locator('#speed-value').press('Tab')
    page.wait_for_function("document.querySelector('#control-status').textContent.includes('physical knob')")
    assert page.evaluate('window.mockState.values[0]')==111
    page.locator('details.diagnostics > summary').click()
    page.locator('#diagnostics').click()
    page.wait_for_function("document.querySelector('#log').textContent.includes('Firmware build: test')")
    page.wait_for_timeout(300)
    assert page.locator('#connection').inner_text()=='LIVE DISPLAY'
    assert page.locator('#bits-value').is_disabled()
    page.locator('#scale').select_option('8')
    assert page.locator('canvas').bounding_box()['width']==1024
    page.set_viewport_size({'width':390,'height':844})
    assert page.evaluate('document.documentElement.scrollWidth')==390
    page.locator('#connect').click()
    page.wait_for_function("document.querySelector('#connection').textContent==='DISCONNECTED'")
    assert page.locator('#rate-value').is_disabled()
    page.locator('#connect').click()
    page.wait_for_function("document.querySelector('#connection').textContent==='LIVE DISPLAY'")
    # A previously authorized PVS reconnects on reload without a chooser.
    page.reload()
    page.wait_for_function("document.querySelector('#connection').textContent==='LIVE DISPLAY'")
    assert page.evaluate('window.mockPickerCalls')==0
    assert page.evaluate('window.mockOpens')==1
    # Replugging reconnects, while an explicit Disconnect remains respected.
    page.evaluate('window.mockUnplug()')
    page.wait_for_function("document.querySelector('#connection').textContent==='DISCONNECTED'")
    page.evaluate('window.mockPlug()')
    page.wait_for_function("document.querySelector('#connection').textContent==='LIVE DISPLAY'")
    assert page.evaluate('window.mockOpens')==2
    assert page.evaluate('window.mockPickerCalls')==0
    # A screenless instrument still exposes its virtual display and controls.
    page.evaluate('window.mockState.online=false')
    page.wait_for_function("document.querySelector('#connection').textContent==='VIRTUAL DISPLAY'")
    assert page.locator('#mouth-value').is_enabled()
    page.locator('#mouth-value').fill('151')
    page.locator('#mouth-value').press('Tab')
    page.wait_for_function('window.mockState.values[2]===151')
    page.locator('#connect').click()
    page.wait_for_function("document.querySelector('#connection').textContent==='DISCONNECTED'")
    page.evaluate('window.mockPlug()')
    page.wait_for_timeout(250)
    assert page.locator('#connection').inner_text()=='DISCONNECTED'
    assert page.evaluate('window.mockOpens')==2
    assert not errors,errors
    browser.close()
    print('PASS: controls, ownership, diagnostics, layout; first-time picker, automatic authorized connection, USB replug, manual disconnect')

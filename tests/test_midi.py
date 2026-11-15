"""USB-MIDI packet validation and bounded servicing using the actual receiver."""
from pathlib import Path
import os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pvs-midi-') as tmp:
 path=Path(tmp)
 (path/'tusb.h').write_text('#include <stdbool.h>\n#include <stdint.h>\nbool tud_midi_packet_read(uint8_t packet[4]);\n')
 (path/'test.c').write_text('''#include "midi.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>
static unsigned cc_count,note_count,queued;
static uint8_t last1,last2;
void pvs_midi_cc(uint8_t a,uint8_t b){++cc_count;last1=a;last2=b;}
void pvs_midi_note(uint8_t a,uint8_t b){++note_count;last1=a;last2=b;}
bool tud_midi_packet_read(uint8_t packet[4]){
 if(!queued)return false;
 --queued;
 memcpy(packet,(uint8_t[]){0x0b,0xb0,20,127},4);return true;
}
int main(void){
 midi_handle_packet((uint8_t[]){0x0b,0xb0,20,127});assert(cc_count==1&&last1==20&&last2==127);
 midi_handle_packet((uint8_t[]){0x09,0x90,36,100});assert(note_count==1&&last1==36&&last2==100);
 uint8_t ignored[][4]={{0x09,0x90,36,0},{0x08,0x80,36,127},{0x09,0x91,36,127},
  {0x0b,0xb1,20,127},{0x1b,0xb0,20,127},{0x09,0xb0,20,127},
  {0x0b,0x90,36,127},{0x0b,0xb0,128,0},{0x09,0x90,36,128},{0x0f,0xf8,0,0}};
 for(unsigned i=0;i<sizeof ignored/sizeof ignored[0];++i)midi_handle_packet(ignored[i]);
 assert(cc_count==1&&note_count==1);
 queued=20;midi_task();assert(queued==4&&cc_count==17);
 midi_task();assert(!queued&&cc_count==21);
}
''')
 exe=path/'test'
 subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+tmp,'-I'+str(root/'include'),str(root/'src/midi.c'),str(path/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: USB-MIDI channel/cable/CIN validation, note-off filtering, malformed packets, bounded receive budget')

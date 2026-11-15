"""Check the real scan decoder and one-time file conversion against key legends."""
from pathlib import Path
import importlib.util
import os
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/matrix.c').read_text()
function = 'static uint16_t decode_scan(uint32_t raw) {' + source.split('static uint16_t decode_scan(uint32_t raw) {', 1)[1].split('\n}', 1)[0] + '\n}'
harness = '#include "keypad.h"\n#include <assert.h>\n#include <string.h>\n' + function + '''
int main(void) {
    const char *old_keys = "147*2580369#ABCD";
    const char *new_keys = "123456789*0#ABCD";
    assert(decode_scan(0xffffu) == 0);
    assert(decode_scan(0) == 0xffffu);
    for (unsigned old = 0; old < 16; ++old) {
        unsigned bit = (3u - old % 4u) * 4u + old / 4u;
        unsigned logical = (unsigned)(strchr(new_keys, old_keys[old]) - new_keys);
        assert(decode_scan(0xffffu ^ (1u << bit)) == (1u << logical));
        assert(keypad_label(logical) == old_keys[old]);
    }
}
'''
with tempfile.TemporaryDirectory(prefix='pvs-keypad-test-') as directory:
    c, exe = Path(directory) / 'test.c', Path(directory) / 'test'
    c.write_text(harness)
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11','-Wall','-Wextra','-Werror','-I'+str(root/'include'),str(c),'-o',str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
spec = importlib.util.spec_from_file_location('migration', root/'tools/migrate_keypad.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
old = ''.join(f'S:old{i}\n' for i in range(1,13))
new = m.convert('phrases_1.txt', old)
phrases = [line for line in new.splitlines() if line.startswith('S:')]
assert phrases == [f'S:old{i}' for i in (1,5,9,2,6,10,3,7,11,4,8,12)]
locks = m.convert('locks_1.txt', '3,7,72,96,128,128,22050,8\n4,4,72,96,128,128,22050,8\n8,4,72,96,128,128,22050,8\n12,4,72,96,128,128,22050,8\n')
for label in ('7','*','0','#'):
    assert '\n'+label+',' in locks
print('PASS: 16 physical keys and phrase/lock conversion match printed legends')

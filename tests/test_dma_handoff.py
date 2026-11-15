"""Exercise the actual PWM start gate with completion pending but BUSY clear."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/audio_pwm.c').read_text()
function = 'static void start_queued(void) {' + source.split('static void start_queued(void) {', 1)[1].split('\n}', 1)[0] + '\n}'
harness = '''
#include <assert.h>
#include <stdbool.h>
static bool stopped, queued[2], started;
static int active_channel = -1;
static unsigned next_play, triggers;
static int channels[2] = {0, 1};
static bool busy[2];
static bool dma_channel_is_busy(int c) { return busy[c]; }
static void dma_start_channel_mask(unsigned mask) {
    assert(active_channel == (int)next_play);
    assert(mask == (1u << next_play));
    ++triggers;
}
''' + function + '''
int main(void) {
    queued[0] = true;
    start_queued();
    assert(triggers == 1 && active_channel == 0 && started);
    /* DMA has finished, but reap read flags just BEFORE completion.
       BUSY is now false, queued remains true, and the read address advanced.
       Never trigger this descriptor a second time. */
    for (unsigned i = 0; i < 10; ++i) start_queued();
    assert(triggers == 1);
    /* Completion is acknowledged; the next ready buffer can now start. */
    queued[0] = false;
    active_channel = -1;
    next_play = 1;
    queued[1] = true;
    start_queued();
    assert(triggers == 2 && active_channel == 1);
    active_channel = -1;
    stopped = true;
    start_queued();
    assert(triggers == 2);
}
'''
with tempfile.TemporaryDirectory(prefix='pvs-dma-test-') as directory:
    c = Path(directory) / 'test.c'
    exe = Path(directory) / 'test'
    c.write_text(harness)
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: completed DMA cannot retrigger before completion is reaped')

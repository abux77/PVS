"""Model RP2040 PIO register effects in the driver's startup sequence.

SM_RESTART clears ISR and makes OSR empty, but preserves OSR contents.
This catches a silent-output failure that renderer PCM tests cannot see.
It does not replace checking the GP0 waveform on hardware.
"""
from pathlib import Path
import re

source = (Path(__file__).resolve().parents[1] / "src/audio_pio.c").read_text()
begin = source.split("void audio_pwm_begin(uint8_t bits) {", 1)[1].split("\n}", 1)[0]
fifo = []
isr, osr, shifted = 0, 0, 32  # audio_pwm_stop() has restarted the SM
operations = re.findall(r"pio_sm_(?:put|exec|restart)\([^;]+;", begin)
assert len(operations) == 5, "Update the register model for the changed startup sequence"
for operation in operations:
    if "pio_sm_put" in operation:
        fifo.append(int(re.search(r",\s*(\d+)\);", operation)[1]))
    elif "pio_encode_pull" in operation:
        osr, shifted = fifo.pop(0), 0
    elif "pio_sm_restart" in operation:
        isr, shifted = 0, 32
    elif "pio_encode_mov(pio_isr, pio_osr)" in operation:
        isr = osr
    else:
        assert "pio_encode_jmp(program_offset)" in operation, operation

assert isr == 255, f"PWM counter is {isr}; restarting after loading ISR silences GP0"
assert shifted == 32 and not fifo, "First OUT must autopull actual PCM"
assert 2 + (isr + 1) * 3 == 770, "PWM period must match the sample-rate divider"
print("PASS: PIO startup preserves PWM counter and starts with empty OSR")

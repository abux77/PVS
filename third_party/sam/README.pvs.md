# Vendored SAM C renderer

Copied from the user's existing `build/_deps/sam_upstream-src/src`, from
https://github.com/s-macke/SAM at commit
`a7b36efac730957b59471a42a45fd779f94d77dd`.
`UPSTREAM_README.md` preserves upstream authorship and license information.

The existing C renderer is used at the user's explicit request. PVS supplies
bounded phoneme batches through `src/sam/sam_renderer.c`; the reciter and parser
are C translations of the supplied Python reference. Upstream `sam.c` and
`reciter.c` are not linked, so their duplicate A/X/Y definitions and large heap
allocation are absent. The adapter owns the renderer's registers and `debug`.

Local changes in `render.c`:

- Route five-sample writes to a bounded streaming sink, retaining timetable
  overlap. No utterance-sized PCM allocation.
- Reset output timing per utterance and clear frame tails between batches.
- Return on sink cancellation and avoid final sampled-frame underflow.
- Skip zero-width interpolation and punctuation with no preceding frames.
- Remove unreachable post-render code that wrote directly into the old buffer.
- Simplify `trans()` formant scaling to doubled high-byte multiplication, as
  in vidarh/SAM commit `c86ea395743b8ea4ad071c2167fd1f7f96648f7b`. Exhaustive
  comparison covers all 65,536 byte pairs; PCM golden tests cover output.

The upstream formant, prosody, transition and sample synthesis code remains the
sound source. Its output is intentionally different from the Python fallback
renderer. The application modes, phrase formats and modulation follow Python.

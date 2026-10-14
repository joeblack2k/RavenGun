# Tools — selective librio.so patches to mash a chosen set of RIO switches

Reusable for any RIO-board title (NERF here; Jurassic Park has the same code inlined). Each builder
starts from the connected-patched NERF librio.so (sha 51ec8ad6…, = dump a32fb2e6… + RIO_Connected ->
xor eax,eax;ret) and makes `RIO_SW_Count` return an ever-changing count (rdtsc, monotonic) for the
chosen ids, real body for the rest. Verify the disassembly of the patch site before trusting a run.

- `build-sel.py <id> <out>`      — one id, 8-byte entry stub at 0xa5b0 (cmp edi,id; jne a5b8; rdtsc; ret).
- `build-thresh.py <le|ge> <K> <out>` — a prefix {0..K} or suffix {K..} range, same 8-byte stub.
- `build-pair.py <A> <B> <out>`  — exactly two ids, via a cave carved from the unused RIO_SW_State body
                                    (0xa540, measured 0 calls); entry 0xa5b0 -> jmp 0xa540.
- `build-set.py <out> <id...>`   — an arbitrary id set, cmp/je chain in the same cave.

Run harness (scratch, not kept): bind the built lib over both plugin paths and run the native route --
  scripts/play-native.sh --game NERF --gamescope \
    --bind-ro <lib> /pm/Nerf_Data/Plugins/x86_64/librio.so \
    --bind-ro <lib> /pm/Nerf_Data/Plugins/x86_64_ori/librio.so \
    --bind-ro patches/NERF/data/udev-rules.d /etc/udev/rules.d

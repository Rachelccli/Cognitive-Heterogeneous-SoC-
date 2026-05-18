# Vivado BD Bring-Up Guide

## Purpose

This guide moves the frozen HLS baseline from C-simulation into the smallest board-relevant system:

```text
PS DDR -> AXI DMA MM2S -> HLS IP -> AXI DMA S2MM -> PS DDR
```

The first goal is not maximum throughput. The first goal is to prove that packet framing, AXI-Lite control, AXI DMA transfers, reset, and board-visible outputs all agree with the HLS baseline before adding more intelligence.

## Bring-Up Order

Use one HLS IP at a time:

1. `qrd_rls_fp32_8x8_axis`
2. `qrd_fixed_pe_monitor_top`
3. `dual_precision_qrd_top`

Do not start with the hybrid top. If the first board run fails, a single FP32 path gives the cleanest debug surface.

## Exported HLS IPs

The local ignored `ip_repo/` directory already contains:

```text
ip_repo/qrd_rls_fp32_8x8_axis.zip
ip_repo/qrd_fixed_pe_monitor_top.zip
ip_repo/dual_precision_qrd_top.zip
```

If Vivado does not accept the ZIP files directly as repositories, unpack them first:

```powershell
mkdir ip_repo\unpacked\qrd_rls_fp32_8x8_axis
mkdir ip_repo\unpacked\qrd_fixed_pe_monitor_top
mkdir ip_repo\unpacked\dual_precision_qrd_top

tar -xf ip_repo\qrd_rls_fp32_8x8_axis.zip -C ip_repo\unpacked\qrd_rls_fp32_8x8_axis
tar -xf ip_repo\qrd_fixed_pe_monitor_top.zip -C ip_repo\unpacked\qrd_fixed_pe_monitor_top
tar -xf ip_repo\dual_precision_qrd_top.zip -C ip_repo\unpacked\dual_precision_qrd_top
```

Then add `ip_repo/unpacked` in:

```text
Vivado -> Tools -> Settings -> IP -> Repository
```

`ip_repo/`, `vivado_prj*/`, and `bd_prj*/` are intentionally ignored. Exported binaries and generated Vivado projects should not be part of the clean GitHub baseline.

## Why GUI First

The first Block Design should be created in the Vivado GUI.

- The first pass is about interface inspection, not automation.
- GUI validation makes AXI clocks, resets, address assignment, and width mismatches obvious.
- After the first working design exists, use `File -> Project -> Write Tcl` or `write_bd_tcl` to make the design reproducible.

Once the FP32 design is proven, scripting the same topology is sensible. Before that, full Tcl is bravely elegant and slightly too clever for its own good.

## Shared Stream Contract

All three tops use `qrd_axis_types.h`:

| Stream | Type | Beats per snapshot | Bytes per snapshot | Meaning |
| --- | --- | ---: | ---: | --- |
| `snapshot_in` | `axis_cpx64_t` | 9 | 72 | 8 array channels plus 1 reference input |
| `weights_out` | `axis_cpx64_t` | 8 | 64 | 8 complex adaptive weights |
| `diag_out` | `axis_float32_t` | 8 | 32 | FP32 diagonal diagnostics |
| `delta_R_out` | `axis_float32_t` | 8 | 32 | fixed/hybrid diagonal-delta diagnostics |

`axis_cpx64_t` packs one complex sample as:

```text
data[31:0]  = float32 real
data[63:32] = float32 imag
```

`TLAST` is part of correctness, not decoration:

- `snapshot_in` must assert `TLAST` on beat 8 only.
- `weights_out` asserts `TLAST` on beat 7 only.
- `diag_out` / `delta_R_out` assert `TLAST` on beat 7 only.

The HLS wrappers explicitly check input framing and raise `frame_error` if the 9-beat contract is violated.

## Minimal BD 1: FP32 Path

### Required IP Blocks

| Block | Role |
| --- | --- |
| `Zynq UltraScale+ MPSoC` | PS, DDR access, AXI-Lite master |
| `AXI DMA` named `axi_dma_data` | MM2S input plus S2MM `weights_out` |
| second `AXI DMA` named `axi_dma_aux` | S2MM `diag_out` |
| `qrd_rls_fp32_8x8_axis` | HLS compute IP |
| `AXI SmartConnect` or `AXI Interconnect` | AXI-Lite and DDR routing |
| `Processor System Reset` | reset fanout |

The second DMA is necessary because the FP32 IP has two output streams. Leaving `diag_out` unconsumed can backpressure the HLS IP and stall the whole kernel.

### PS Configuration

Enable at least:

- `M_AXI_HPM0_FPD` for AXI-Lite register writes from PS to PL peripherals.
- `S_AXI_HP0_FPD` or `S_AXI_HPC0_FPD` for DMA access to DDR.
- `pl_clk0`, initially at a conservative board-friendly clock such as `100 MHz` or `150 MHz`.

For the first bring-up, conservative clocks beat heroic clocks. HLS already proved algorithmic behavior; the board pass is about system truth.

### DMA Configuration

Use Simple DMA mode first:

- Disable Scatter-Gather.
- `axi_dma_data`
  - MM2S enabled, stream width `64`.
  - S2MM enabled, stream width `64`.
- `axi_dma_aux`
  - MM2S disabled if possible.
  - S2MM enabled, stream width `32`.

### Complete FP32 Connection Matrix

Use two SmartConnect blocks for clarity:

- `sc_ctrl`: AXI-Lite control path from PS to DMA/HLS registers.
- `sc_mem`: AXI memory path from DMA masters to PS DDR through an HP/HPC port.

Vivado names SmartConnect ports as `Sxx_AXI` for incoming masters and `Mxx_AXI` for outgoing slaves. This is the most common source of confusion: a DMA `M_AXI_*` master connects into a SmartConnect `Sxx_AXI` port.

#### Zynq MPSoC PS Ports

| PS port | Connect to | Purpose | Notes |
| --- | --- | --- | --- |
| `M_AXI_HPM0_FPD` | `sc_ctrl/S00_AXI` | PS writes AXI-Lite control registers in DMA and HLS IP | Enable this PS master port in block automation |
| `S_AXI_HP0_FPD` or `S_AXI_HPC0_FPD` | `sc_mem/M00_AXI` | DMA masters read/write PS DDR | Enable one HP/HPC slave port for PL masters |
| `pl_clk0` | all PL `*_aclk` / `ap_clk` inputs | Common PL clock | Start with `100 MHz` or `150 MHz` |
| `pl_resetn0` | reset generator input via block automation | Source reset for PL reset tree | Use Vivado automation if possible |
| `maxihpm0_fpd_aclk` | `pl_clk0` | Clock for PS AXI-Lite master interface | Required when `M_AXI_HPM0_FPD` is enabled |
| `saxihp0_fpd_aclk` or matching HP/HPC clock | `pl_clk0` | Clock for PS HP/HPC DDR-facing slave interface | Port name changes with selected HP/HPC interface |
| `pl_ps_irq0` | optional `xlconcat/dout` | Optional interrupt input from DMA/HLS | Polling works without this in the first smoke test |

#### AXI-Lite Control Path

| Source port | Destination port | Bus width | Purpose | Notes |
| --- | --- | ---: | --- | --- |
| `zynq_ultra_ps_e_0/M_AXI_HPM0_FPD` | `sc_ctrl/S00_AXI` | AXI | PS master enters control interconnect | One PS control master is enough |
| `sc_ctrl/M00_AXI` | `axi_dma_data/S_AXI_LITE` | AXI-Lite | Program data DMA control/status registers | Required for MM2S input and S2MM weights receive |
| `sc_ctrl/M01_AXI` | `axi_dma_aux/S_AXI_LITE` | AXI-Lite | Program auxiliary DMA receive channel | Required for `diag_out`; otherwise HLS can stall |
| `sc_ctrl/M02_AXI` | `qrd_rls_fp32_8x8_axis_0/s_axi_control` | AXI-Lite | Program HLS registers: `lambda`, `reset`, `ap_start`; read `frame_error` | Scalar HLS ports are registers inside this bus |

#### AXI Memory-Mapped DDR Path

| Source port | Destination port | Bus width | Purpose | Notes |
| --- | --- | ---: | --- | --- |
| `axi_dma_data/M_AXI_MM2S` | `sc_mem/S00_AXI` | AXI | Data DMA reads input snapshots from DDR | PS fills a 72-byte input buffer |
| `axi_dma_data/M_AXI_S2MM` | `sc_mem/S01_AXI` | AXI | Data DMA writes `weights_out` back to DDR | PS reads a 64-byte output buffer |
| `axi_dma_aux/M_AXI_S2MM` | `sc_mem/S02_AXI` | AXI | Auxiliary DMA writes `diag_out` back to DDR | PS reads a 32-byte diagnostic buffer |
| `sc_mem/M00_AXI` | `zynq_ultra_ps_e_0/S_AXI_HP0_FPD` or `S_AXI_HPC0_FPD` | AXI | All DMA memory transactions reach PS DDR | Use the same HP/HPC port clocked by `pl_clk0` |

If `axi_dma_aux` still has an unused `M_AXI_MM2S` because MM2S was not disabled, either disable MM2S in IP customization or leave that channel unconnected only if Vivado validation allows it. The clean design disables the unused MM2S channel.

#### AXI4-Stream Data Path

| Source port | Destination port | Width | Beats | Purpose | TLAST rule |
| --- | --- | ---: | ---: | --- | --- |
| `axi_dma_data/M_AXIS_MM2S` | `qrd_rls_fp32_8x8_axis_0/snapshot_in` | 64 bit | 9 | Send one snapshot: 8 array channels plus reference | Input `TLAST` on beat 8 |
| `qrd_rls_fp32_8x8_axis_0/weights_out` | `axi_dma_data/S_AXIS_S2MM` | 64 bit | 8 | Receive 8 complex adaptive weights | Output `TLAST` on beat 7 |
| `qrd_rls_fp32_8x8_axis_0/diag_out` | `axi_dma_aux/S_AXIS_S2MM` | 32 bit | 8 | Receive 8 FP32 diagonal diagnostics | Output `TLAST` on beat 7 |

If Vivado reports an AXIS data-width mismatch, fix the DMA stream width in customization first. If the GUI still cannot match widths, insert an `AXIS Data Width Converter` only on the mismatched stream. Do not change the HLS IP interface just to satisfy the first BD.

#### Clock Connections

| Source clock | Destination clock pins | Purpose | Notes |
| --- | --- | --- | --- |
| `zynq_ultra_ps_e_0/pl_clk0` | `qrd_rls_fp32_8x8_axis_0/ap_clk` | HLS kernel clock | Same clock domain keeps debug simple |
| `zynq_ultra_ps_e_0/pl_clk0` | `axi_dma_data/s_axi_lite_aclk` | Data DMA AXI-Lite clock | Required |
| `zynq_ultra_ps_e_0/pl_clk0` | `axi_dma_data/m_axi_mm2s_aclk` | Data DMA MM2S memory clock | Required |
| `zynq_ultra_ps_e_0/pl_clk0` | `axi_dma_data/m_axi_s2mm_aclk` | Data DMA S2MM memory clock | Required |
| `zynq_ultra_ps_e_0/pl_clk0` | `axi_dma_aux/s_axi_lite_aclk` | Aux DMA AXI-Lite clock | Required |
| `zynq_ultra_ps_e_0/pl_clk0` | `axi_dma_aux/m_axi_s2mm_aclk` | Aux DMA S2MM memory clock | Required |
| `zynq_ultra_ps_e_0/pl_clk0` | `sc_ctrl/aclk`, `sc_mem/aclk` | SmartConnect clocks | Port names can appear as `aclk` or `aclk0` depending on Vivado |
| `zynq_ultra_ps_e_0/pl_clk0` | `proc_sys_reset_0/slowest_sync_clk` | Reset synchronizer clock | Required |
| `zynq_ultra_ps_e_0/pl_clk0` | PS AXI interface clocks such as `maxihpm0_fpd_aclk`, `saxihp0_fpd_aclk` | Clock PS AXI ports | Exact HP/HPC clock pin depends on selected PS interface |

For first bring-up, keep every PL-side interface in one clock domain. Multi-clock refinement can wait until the design works.

#### Reset Connections

| Source reset | Destination reset pins | Polarity | Purpose | Notes |
| --- | --- | --- | --- | --- |
| PS reset via block automation | `proc_sys_reset_0/ext_reset_in` | Vivado-managed | Generate synchronized PL resets | Let Vivado block automation handle PS reset polarity |
| `proc_sys_reset_0/peripheral_aresetn` | `qrd_rls_fp32_8x8_axis_0/ap_rst_n` | active-low | Reset HLS IP | Required |
| `proc_sys_reset_0/peripheral_aresetn` | `axi_dma_data/axi_resetn` | active-low | Reset data DMA | Required |
| `proc_sys_reset_0/peripheral_aresetn` | `axi_dma_aux/axi_resetn` | active-low | Reset auxiliary DMA | Required |
| `proc_sys_reset_0/peripheral_aresetn` | `sc_ctrl/aresetn`, `sc_mem/aresetn` | active-low | Reset SmartConnect | Required |

Do not connect `pl_resetn0` directly to random active-high reset pins. Use `Processor System Reset`; it exists to make this boring and safe.

#### Optional Interrupt Connections

| Source interrupt | Destination | Purpose | First-pass recommendation |
| --- | --- | --- | --- |
| `axi_dma_data/mm2s_introut` | `xlconcat/In0` | Data DMA send completion/error interrupt | Optional; polling is simpler first |
| `axi_dma_data/s2mm_introut` | `xlconcat/In1` | Weights receive completion/error interrupt | Optional |
| `axi_dma_aux/s2mm_introut` | `xlconcat/In2` | Diagnostics receive completion/error interrupt | Optional |
| `qrd_rls_fp32_8x8_axis_0/interrupt` | `xlconcat/In3` | HLS `ap_done` interrupt | Optional |
| `xlconcat/dout` | `zynq_ultra_ps_e_0/pl_ps_irq0` | Route PL interrupts to PS | Add after polling version works |

For the first bring-up, polling AXI DMA status and HLS `ap_done` is less magical. Interrupts are useful later, but they add one more thing that can go wrong while the first smoke test is still fragile.

#### Address Editor Expectations

| Address segment | Master | Slave | Purpose |
| --- | --- | --- | --- |
| HLS control segment | PS `M_AXI_HPM0_FPD` | `qrd_rls_fp32_8x8_axis_0/s_axi_control` | HLS `ap_start`, `lambda`, `reset`, `frame_error` |
| Data DMA control segment | PS `M_AXI_HPM0_FPD` | `axi_dma_data/S_AXI_LITE` | DMA MM2S/S2MM programming |
| Aux DMA control segment | PS `M_AXI_HPM0_FPD` | `axi_dma_aux/S_AXI_LITE` | Diagnostic S2MM programming |
| DDR segment for data DMA MM2S | `axi_dma_data/M_AXI_MM2S` | PS DDR through HP/HPC | Read input snapshot buffer |
| DDR segment for data DMA S2MM | `axi_dma_data/M_AXI_S2MM` | PS DDR through HP/HPC | Write weights buffer |
| DDR segment for aux DMA S2MM | `axi_dma_aux/M_AXI_S2MM` | PS DDR through HP/HPC | Write diag buffer |

The exact base addresses are assigned by Vivado. The HLS register offsets inside the HLS base address remain fixed: `ap_ctrl=0x00`, `lambda=0x10`, `reset=0x18`, `frame_error=0x20`.

#### Address Warning Triage

Treat these Vivado validation messages as actionable, not cosmetic:

| Warning pattern | Impact | Fix |
| --- | --- | --- |
| `qrd_rls_fp32_8x8_axis_0/s_axi_control/Reg is not assigned into zynq.../Data` | PS cannot start/configure the HLS IP or read `frame_error` | Assign it under PS `Data`, for example base `0xA002_0000`, range `64K` |
| `axi_dma_0/S_AXI_LITE/Reg is not assigned into zynq.../Data` | PS cannot program the data DMA | Assign it under PS `Data`, for example base `0xA000_0000`, range `64K` |
| `axi_dma_1/S_AXI_LITE/Reg is not assigned into zynq.../Data` | PS cannot program the aux DMA | Assign it under PS `Data`, for example base `0xA001_0000`, range `64K` |
| `HP0_DDR_LOW is not assigned into axi_dma_0/Data_S2MM` | Data DMA cannot legally write `weights_out` to DDR | Assign `HP0_DDR_LOW` into `axi_dma_0/Data_S2MM` |
| `HP0_DDR_LOW is not assigned into axi_dma_1/Data_S2MM` | Aux DMA cannot legally write `diag_out` to DDR | Assign `HP0_DDR_LOW` into `axi_dma_1/Data_S2MM` |
| `HP0_QSPI`, `HP0_LPS_OCM`, or unused `HP0_DDR_HIGH` not assigned | Usually harmless if the first test only uses low DDR | Right-click and `Exclude` unused segments |
| `SmartConnect ... has no assigned address segments` | Active interconnect may become undefined or black-box during implementation | Assign address segments through it, or delete it if it is an unused leftover SmartConnect |

For the first design, map all active DMA masters to `HP0_DDR_LOW`. It is okay for `axi_dma_0/Data_MM2S`, `axi_dma_0/Data_S2MM`, and `axi_dma_1/Data_S2MM` to see the same DDR low address segment; these are separate master address spaces. Keep input, weights, and diagnostics in separate software buffers, not necessarily separate BD address windows.

Do not split DDR into artificial non-overlapping windows unless the software allocator is also constrained to place buffers inside those windows. A full `HP0_DDR_LOW` mapping is less fragile for the first bare-metal or Linux DMA smoke test.

### GUI Sequence

1. Create a new Vivado project under an ignored local directory such as `vivado_prj_fp32_min/`.
2. Select the KV260 board part if installed. If the board files are unavailable, select the matching Kria K26 device used by the target board.
3. Add the HLS IP repository.
4. Create a Block Design, for example `fp32_min_bd`.
5. Add `Zynq UltraScale+ MPSoC`, then run Block Automation.
6. Enable the PS AXI ports and `pl_clk0`.
7. Add the two DMA IPs, the HLS FP32 IP, SmartConnect, and Processor System Reset.
8. Configure the DMA widths and disable Scatter-Gather.
9. Make the stream, AXI-Lite, DDR, clock, and reset connections from the tables above.
10. Run `Validate Design`.
11. Open `Address Editor`, assign all AXI-Lite ranges, and verify DMA masters can reach DDR.
12. Generate Output Products.
13. Create HDL Wrapper.
14. Run Synthesis, Implementation, and Generate Bitstream.
15. Export Hardware with bitstream to an ignored local `.xsa`.

## FP32 Software Smoke Test

### Register Map

The exported FP32 driver reports:

| Register | Offset |
| --- | ---: |
| `ap_ctrl` | `0x00` |
| `lambda` | `0x10` |
| `reset` | `0x18` |
| `frame_error` | `0x20` |

### One-Snapshot Transaction

Use one snapshot first:

1. Prepare `72` bytes of input data: 9 complex-float packets.
2. Allocate `64` bytes for weights and `32` bytes for diagonal output.
3. Flush input cache and invalidate output buffers if software caches are enabled.
4. Program HLS registers:
   - `lambda = 0.955f`
   - `reset = 1` for the very first frame
5. Start S2MM receives first:
   - weights receive length `64`
   - diag receive length `32`
6. Set HLS `ap_start`.
7. Start MM2S send length `72`.
8. Wait for all DMA transfers to complete.
9. Read `frame_error`; it must stay `0`.
10. Invalidate caches and compare weights/diag against a saved HLS reference vector.
11. Clear `reset` to `0` before the next normal snapshot.

Use manual `ap_start` per snapshot for the first debug pass. `auto_restart` can wait until the single-frame path is boringly correct.

### FP32 Pass Criteria

- No DMA hang.
- No `frame_error`.
- Exactly 9 input beats, 8 weight beats, and 8 diag beats observed.
- Weights and diagonal outputs agree with the HLS reference within expected floating-point tolerance.
- Repeated snapshots update state across calls when `reset=0`.

## Minimal BD 2: FIXED_PE Path

Replace the HLS IP with `qrd_fixed_pe_monitor_top`; keep the same two-DMA topology:

- `snapshot_in`: 64-bit input stream.
- `weights_out`: 64-bit output stream.
- `delta_R_out`: 32-bit auxiliary output stream.

### Important AXI-Lite Registers

| Register | Offset |
| --- | ---: |
| `lambda` | `0x10` |
| `cond_threshold` | `0x18` |
| `reset` | `0x20` |
| `enable_norm` | `0x28` |
| `norm_eps` | `0x30` |
| `scale_out` | `0x38` |
| `cond_estimate_out` | `0x48` |
| `delta_R_norm_out` | `0x58` |
| `switch_recommend` | `0x68` |
| `overflow_flag` | `0x78` |
| `frame_error` | `0x88` |

### Recommended Initial Settings

```text
lambda         = 0.955
cond_threshold = 380.0
enable_norm    = 1
norm_eps       = 1e-6
```

### FIXED_PE Pass Criteria

- FP32 smoke-test mechanics still pass.
- `scale_out`, `cond_estimate_out`, and `delta_R_norm_out` become readable after snapshots.
- `switch_recommend` and `overflow_flag` behave plausibly on saved baseline/stress vectors.
- Normalized nominal scenes run without unexpected overflow.

## Minimal BD 3: HYBRID_LP Path

Replace the HLS IP with `dual_precision_qrd_top`; keep the same two-DMA topology.

### Important AXI-Lite Registers

| Register | Offset |
| --- | ---: |
| `mode` | `0x10` |
| `lambda` | `0x18` |
| `cond_threshold` | `0x20` |
| `auto_hold_window` | `0x28` |
| `reset` | `0x30` |
| `enable_norm` | `0x38` |
| `norm_eps` | `0x40` |
| `scale_out` | `0x48` |
| `cond_estimate_out` | `0x58` |
| `delta_R_norm_out` | `0x68` |
| `switch_recommend` | `0x78` |
| `overflow_flag` | `0x88` |
| `frame_error` | `0x98` |
| `selected_float_out` | `0xa8` |
| `migration_done` | `0xb8` |
| `migration_direction` | `0xc8` |
| `active_float_out` | `0xd8` |
| `lambda_transit_out` | `0xe8` |

### Recommended Initial Settings

```text
mode             = 2
lambda           = 0.955
cond_threshold   = 380.0
auto_hold_window = 8
enable_norm      = 1
norm_eps         = 1e-6
```

### Debug Order

Use the hybrid top itself to isolate problems:

1. `mode=1`: force FP32 through the hybrid wrapper.
2. `mode=0`: force FIXED through the hybrid wrapper.
3. `mode=3`: HYBRID_FIXED_LAMBDA precision-only ablation.
4. `mode=2`: HYBRID_LP plus rule-based VFF.

This sequence separates stream/BD problems from state bridge and scheduler problems.

### HYBRID_LP Pass Criteria

- `mode=1` agrees with the standalone FP32 path.
- `mode=0` agrees with the standalone FIXED_PE path within expected fixed-point tolerance.
- `mode=3` follows the frozen precision-only baseline.
- `mode=2` emits plausible `selected_float_out`, `migration_done`, and `lambda_transit_out` traces on transient and stress vectors.
- Saved HLS scene vectors reproduce the same qualitative behavior as the frozen C-sim baseline.

## Recommended First Board Test Set

Use a tiny deterministic bring-up set before larger scenes:

1. One nominal frame, reset asserted.
2. Several repeated nominal frames, reset deasserted.
3. A deliberately malformed frame if software allows it, to prove `frame_error`.
4. One saved transient window.
5. One saved burst/high-risk window for FIXED_PE and HYBRID_LP.

After this passes, move to full scene windows and power/latency measurement.

## Common Failure Modes

| Symptom | Likely cause |
| --- | --- |
| Kernel never finishes | auxiliary output stream not consumed, missing S2MM receive, or `ap_start` not asserted |
| `frame_error=1` | MM2S transfer length not `72` bytes or `TLAST` arrives on the wrong beat |
| DMA output length mismatch | wrong stream width or wrong receive length |
| First frame okay, later frames nonsense | reset held high too long or state reset unexpectedly |
| Software sees stale output | cache flush/invalidate missing |
| Hybrid appears stuck in FP32/FIXED | control register programming wrong or scheduler status not being sampled after valid output |
| Address Editor refuses mapping | PS HP/HPC port not enabled or wrong SmartConnect direction |

## What To Measure After Bring-Up

Do not overclaim before these exist:

1. Per-snapshot latency on board.
2. Sustained snapshot throughput.
3. Vivado power estimate with identical traffic assumptions.
4. If available, board-level power while running:
   - FP32-only
   - FIXED-only
   - HYBRID_FIXED_LAMBDA
   - HYBRID_LP
5. HYBRID counters:
   - `selected_float_ratio`
   - migration count
   - lambda range
   - output SINR versus software/HLS reference

Only after real power evidence exists should `selected_float_ratio` be promoted from an activity proxy to an energy claim.

## Reproducibility Rule

Once the first GUI BD is working:

1. Save the Vivado project locally.
2. Export the BD Tcl:

```tcl
write_bd_tcl -force fp32_min_bd.tcl
```

3. Review and clean the Tcl.
4. Add the reviewed Tcl later if a reproducible hardware-integration branch is desired.

The first GitHub baseline should still remain source-centric; generated bitstreams, `.xsa`, implementation runs, and exported IP ZIPs stay local.

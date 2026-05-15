# run_hls_qrd_fixed_pe_update_top.tcl
# Purpose: Vitis HLS C-synthesis for the core-only paper-aligned FIXED_PE QR update path.

open_project -reset hls_prj_qrd_fixed_pe_update_top
set_top qrd_rls_cordic_pe_8x8_update_top
add_files HLS/FIXED_PE/qrd_rls_cordic_pe_8x8.cpp
add_files HLS/FIXED_PE/qrd_rls_cordic_pe_8x8.h
add_files qrd_rls_cordic_8x8.h
open_solution -reset solution1 -flow_target vivado
set_part {xck26-sfvc784-2LV-c}
create_clock -period 6.5 -name default
config_compile -pipeline_loops 0
csynth_design
exit

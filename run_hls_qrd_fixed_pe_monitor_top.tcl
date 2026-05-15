# run_hls_qrd_fixed_pe_monitor_top.tcl
# Purpose: Vitis HLS C-synthesis for the paper-aligned FIXED_PE QRD-RLS top-level IP.

open_project -reset hls_prj_qrd_fixed_pe_monitor_top
set_top qrd_fixed_pe_monitor_top
add_files HLS/FIXED_PE/qrd_fixed_pe_monitor_top.cpp
add_files HLS/FIXED_PE/qrd_fixed_pe_monitor_top.h
add_files HLS/FIXED_PE/qrd_rls_cordic_pe_8x8.cpp
add_files HLS/FIXED_PE/qrd_rls_cordic_pe_8x8.h
add_files HLS/COMMON/snapshot_normalizer.cpp
add_files HLS/COMMON/snapshot_normalizer.h
add_files HLS/COMMON/cond_estimator.cpp
add_files HLS/COMMON/cond_estimator.h
add_files qrd_axis_types.h
add_files qrd_rls_cordic_8x8.h
open_solution -reset solution1 -flow_target vivado
set_part {xck26-sfvc784-2LV-c}
create_clock -period 6.5 -name default
config_compile -pipeline_loops 0
csynth_design
exit

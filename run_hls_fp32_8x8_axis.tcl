# run_hls_fp32_8x8_axis.tcl
# Purpose: Vitis HLS C-synthesis script for qrd_rls_fp32_8x8_axis.

open_project -reset hls_prj_fp32_8x8_axis
set_top qrd_rls_fp32_8x8_axis
add_files HLS/FP32/qrd_rls_fp32_8x8.cpp
add_files HLS/FP32/qrd_rls_fp32_8x8.h
add_files HLS/FP32/qrd_rls_fp32_8x8_axis.cpp
add_files HLS/FP32/qrd_rls_fp32_8x8_axis.h
add_files qrd_axis_types.h
open_solution -reset solution1 -flow_target vivado
set_part {xck26-sfvc784-2LV-c}
create_clock -period 6.5 -name default
csynth_design
exit

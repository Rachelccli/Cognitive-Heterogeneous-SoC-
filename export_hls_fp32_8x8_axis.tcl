# export_hls_fp32_8x8_axis.tcl
# Purpose: Export the synthesized FP32 QRD-RLS AXIS IP into the local IP repository.

open_project hls_prj_fp32_8x8_axis
open_solution solution1
export_design -format ip_catalog -rtl verilog -output ip_repo/qrd_rls_fp32_8x8_axis
exit

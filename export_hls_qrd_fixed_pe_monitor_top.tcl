# export_hls_qrd_fixed_pe_monitor_top.tcl
# Purpose: Export the synthesized FIXED_PE monitor top into the local IP repository.

open_project hls_prj_qrd_fixed_pe_monitor_top
open_solution solution1
export_design -format ip_catalog -rtl verilog -output ip_repo/qrd_fixed_pe_monitor_top
exit

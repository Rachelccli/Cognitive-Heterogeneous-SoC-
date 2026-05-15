# export_hls_dual_precision_qrd_top.tcl
# Purpose: Export the synthesized HYBRID_LP dual-precision QRD-RLS top into the local IP repository.

open_project hls_prj_dual_precision_qrd_top
open_solution solution1
export_design -format ip_catalog -rtl verilog -output ip_repo/dual_precision_qrd_top
exit

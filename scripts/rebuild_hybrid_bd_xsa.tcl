set proj_path [file normalize "E:/ASSCC/project_qrdrls_hybird/project_qrdrls_hybird.xpr"]
set xsa_path [file normalize "E:/ASSCC/project_qrdrls_hybird/bd_hybird.xsa"]

open_project $proj_path

set bd_file [get_files "*design_3.bd"]
if {[llength $bd_file] == 0} {
    puts "ERROR: design_3.bd not found"
    exit 1
}

catch {set_property AutoIncrementalCheckpoint false [get_runs synth_1]}
catch {reset_property IncrementalCheckpoint [get_runs synth_1]}

update_ip_catalog -rebuild
upgrade_ip [get_ips design_3_dual_precision_qrd_t_0_0]
generate_target all $bd_file
export_ip_user_files -of_objects $bd_file -no_script -sync -force

reset_run synth_1
reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1

open_run impl_1
write_hw_platform -fixed -include_bit -force -file $xsa_path
close_project
exit

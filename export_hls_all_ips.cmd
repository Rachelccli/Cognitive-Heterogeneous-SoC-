@echo off
setlocal

set "VITIS_SETTINGS=F:\AMDDesignTools\2025.2\Vitis\settings64.bat"
set "ROOT=%~dp0"

if not exist "%VITIS_SETTINGS%" (
  echo ERROR: Vitis settings file not found: %VITIS_SETTINGS%
  exit /b 1
)

call "%VITIS_SETTINGS%"
cd /d "%ROOT%"

call vitis-run --mode hls --tcl export_hls_fp32_8x8_axis.tcl || exit /b 1
call vitis-run --mode hls --tcl export_hls_qrd_fixed_pe_monitor_top.tcl || exit /b 1
call vitis-run --mode hls --tcl export_hls_dual_precision_qrd_top.tcl || exit /b 1

echo Exported HLS IPs into %ROOT%ip_repo

# Definitional proc to organize widgets for parameters.
proc init_gui { IPINST } {
  ipgui::add_param $IPINST -name "Component_Name"
  #Adding Page
  set Page_0 [ipgui::add_page $IPINST -name "Page 0"]
  ipgui::add_param $IPINST -name "AXI_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "AXI_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "AXI_MASK" -parent ${Page_0}
  ipgui::add_param $IPINST -name "BP_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "BP_ENTRIES" -parent ${Page_0}
  ipgui::add_param $IPINST -name "BP_GSHARE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "CLINT_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "CLINT_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "CLINT_MASK" -parent ${Page_0}
  ipgui::add_param $IPINST -name "DEBUG_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "DIV_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "DMEM_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "DMEM_INIT_FILE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "DMEM_MASK" -parent ${Page_0}
  ipgui::add_param $IPINST -name "DMEM_WORDS" -parent ${Page_0}
  ipgui::add_param $IPINST -name "GHR_WIDTH" -parent ${Page_0}
  ipgui::add_param $IPINST -name "GPIO_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "GPIO_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "GPIO_MASK" -parent ${Page_0}
  ipgui::add_param $IPINST -name "GPIO_WIDTH" -parent ${Page_0}
  ipgui::add_param $IPINST -name "IMEM_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "IMEM_INIT_FILE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "IMEM_WORDS" -parent ${Page_0}
  ipgui::add_param $IPINST -name "IRQ_INITIAL_LEVEL" -parent ${Page_0}
  ipgui::add_param $IPINST -name "IRQ_MODE_RESET" -parent ${Page_0}
  ipgui::add_param $IPINST -name "IRQ_SYNC_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "MONITOR_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "MONITOR_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "MONITOR_MASK" -parent ${Page_0}
  ipgui::add_param $IPINST -name "MTVEC_RESET" -parent ${Page_0}
  ipgui::add_param $IPINST -name "MUL_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "PLIC_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "PLIC_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "PLIC_MASK" -parent ${Page_0}
  ipgui::add_param $IPINST -name "PLIC_PRIO_BITS" -parent ${Page_0}
  ipgui::add_param $IPINST -name "PLIC_SOURCES" -parent ${Page_0}
  ipgui::add_param $IPINST -name "RESET_VECTOR" -parent ${Page_0}
  ipgui::add_param $IPINST -name "SHADOW_BANK_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "TIMER_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "TIMER_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "TIMER_IRQ_ID" -parent ${Page_0}
  ipgui::add_param $IPINST -name "TIMER_MASK" -parent ${Page_0}
  ipgui::add_param $IPINST -name "TRACE_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "UART_BASE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "UART_ENABLE" -parent ${Page_0}
  ipgui::add_param $IPINST -name "UART_IRQ_ID" -parent ${Page_0}
  ipgui::add_param $IPINST -name "UART_MASK" -parent ${Page_0}
}

proc update_PARAM_VALUE.AXI_BASE { PARAM_VALUE.AXI_BASE } {
	# Procedure called to update AXI_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.AXI_BASE { PARAM_VALUE.AXI_BASE } {
	# Procedure called to validate AXI_BASE
	return true
}

proc update_PARAM_VALUE.AXI_ENABLE { PARAM_VALUE.AXI_ENABLE } {
	# Procedure called to update AXI_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.AXI_ENABLE { PARAM_VALUE.AXI_ENABLE } {
	# Procedure called to validate AXI_ENABLE
	return true
}

proc update_PARAM_VALUE.AXI_MASK { PARAM_VALUE.AXI_MASK } {
	# Procedure called to update AXI_MASK when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.AXI_MASK { PARAM_VALUE.AXI_MASK } {
	# Procedure called to validate AXI_MASK
	return true
}

proc update_PARAM_VALUE.BP_ENABLE { PARAM_VALUE.BP_ENABLE } {
	# Procedure called to update BP_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.BP_ENABLE { PARAM_VALUE.BP_ENABLE } {
	# Procedure called to validate BP_ENABLE
	return true
}

proc update_PARAM_VALUE.BP_ENTRIES { PARAM_VALUE.BP_ENTRIES } {
	# Procedure called to update BP_ENTRIES when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.BP_ENTRIES { PARAM_VALUE.BP_ENTRIES } {
	# Procedure called to validate BP_ENTRIES
	return true
}

proc update_PARAM_VALUE.BP_GSHARE { PARAM_VALUE.BP_GSHARE } {
	# Procedure called to update BP_GSHARE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.BP_GSHARE { PARAM_VALUE.BP_GSHARE } {
	# Procedure called to validate BP_GSHARE
	return true
}

proc update_PARAM_VALUE.CLINT_BASE { PARAM_VALUE.CLINT_BASE } {
	# Procedure called to update CLINT_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.CLINT_BASE { PARAM_VALUE.CLINT_BASE } {
	# Procedure called to validate CLINT_BASE
	return true
}

proc update_PARAM_VALUE.CLINT_ENABLE { PARAM_VALUE.CLINT_ENABLE } {
	# Procedure called to update CLINT_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.CLINT_ENABLE { PARAM_VALUE.CLINT_ENABLE } {
	# Procedure called to validate CLINT_ENABLE
	return true
}

proc update_PARAM_VALUE.CLINT_MASK { PARAM_VALUE.CLINT_MASK } {
	# Procedure called to update CLINT_MASK when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.CLINT_MASK { PARAM_VALUE.CLINT_MASK } {
	# Procedure called to validate CLINT_MASK
	return true
}

proc update_PARAM_VALUE.DEBUG_ENABLE { PARAM_VALUE.DEBUG_ENABLE } {
	# Procedure called to update DEBUG_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.DEBUG_ENABLE { PARAM_VALUE.DEBUG_ENABLE } {
	# Procedure called to validate DEBUG_ENABLE
	return true
}

proc update_PARAM_VALUE.DIV_ENABLE { PARAM_VALUE.DIV_ENABLE } {
	# Procedure called to update DIV_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.DIV_ENABLE { PARAM_VALUE.DIV_ENABLE } {
	# Procedure called to validate DIV_ENABLE
	return true
}

proc update_PARAM_VALUE.DMEM_BASE { PARAM_VALUE.DMEM_BASE } {
	# Procedure called to update DMEM_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.DMEM_BASE { PARAM_VALUE.DMEM_BASE } {
	# Procedure called to validate DMEM_BASE
	return true
}

proc update_PARAM_VALUE.DMEM_INIT_FILE { PARAM_VALUE.DMEM_INIT_FILE } {
	# Procedure called to update DMEM_INIT_FILE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.DMEM_INIT_FILE { PARAM_VALUE.DMEM_INIT_FILE } {
	# Procedure called to validate DMEM_INIT_FILE
	return true
}

proc update_PARAM_VALUE.DMEM_MASK { PARAM_VALUE.DMEM_MASK } {
	# Procedure called to update DMEM_MASK when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.DMEM_MASK { PARAM_VALUE.DMEM_MASK } {
	# Procedure called to validate DMEM_MASK
	return true
}

proc update_PARAM_VALUE.DMEM_WORDS { PARAM_VALUE.DMEM_WORDS } {
	# Procedure called to update DMEM_WORDS when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.DMEM_WORDS { PARAM_VALUE.DMEM_WORDS } {
	# Procedure called to validate DMEM_WORDS
	return true
}

proc update_PARAM_VALUE.GHR_WIDTH { PARAM_VALUE.GHR_WIDTH } {
	# Procedure called to update GHR_WIDTH when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.GHR_WIDTH { PARAM_VALUE.GHR_WIDTH } {
	# Procedure called to validate GHR_WIDTH
	return true
}

proc update_PARAM_VALUE.GPIO_BASE { PARAM_VALUE.GPIO_BASE } {
	# Procedure called to update GPIO_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.GPIO_BASE { PARAM_VALUE.GPIO_BASE } {
	# Procedure called to validate GPIO_BASE
	return true
}

proc update_PARAM_VALUE.GPIO_ENABLE { PARAM_VALUE.GPIO_ENABLE } {
	# Procedure called to update GPIO_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.GPIO_ENABLE { PARAM_VALUE.GPIO_ENABLE } {
	# Procedure called to validate GPIO_ENABLE
	return true
}

proc update_PARAM_VALUE.GPIO_MASK { PARAM_VALUE.GPIO_MASK } {
	# Procedure called to update GPIO_MASK when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.GPIO_MASK { PARAM_VALUE.GPIO_MASK } {
	# Procedure called to validate GPIO_MASK
	return true
}

proc update_PARAM_VALUE.GPIO_WIDTH { PARAM_VALUE.GPIO_WIDTH } {
	# Procedure called to update GPIO_WIDTH when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.GPIO_WIDTH { PARAM_VALUE.GPIO_WIDTH } {
	# Procedure called to validate GPIO_WIDTH
	return true
}

proc update_PARAM_VALUE.IMEM_BASE { PARAM_VALUE.IMEM_BASE } {
	# Procedure called to update IMEM_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IMEM_BASE { PARAM_VALUE.IMEM_BASE } {
	# Procedure called to validate IMEM_BASE
	return true
}

proc update_PARAM_VALUE.IMEM_INIT_FILE { PARAM_VALUE.IMEM_INIT_FILE } {
	# Procedure called to update IMEM_INIT_FILE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IMEM_INIT_FILE { PARAM_VALUE.IMEM_INIT_FILE } {
	# Procedure called to validate IMEM_INIT_FILE
	return true
}

proc update_PARAM_VALUE.IMEM_WORDS { PARAM_VALUE.IMEM_WORDS } {
	# Procedure called to update IMEM_WORDS when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IMEM_WORDS { PARAM_VALUE.IMEM_WORDS } {
	# Procedure called to validate IMEM_WORDS
	return true
}

proc update_PARAM_VALUE.IRQ_INITIAL_LEVEL { PARAM_VALUE.IRQ_INITIAL_LEVEL } {
	# Procedure called to update IRQ_INITIAL_LEVEL when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IRQ_INITIAL_LEVEL { PARAM_VALUE.IRQ_INITIAL_LEVEL } {
	# Procedure called to validate IRQ_INITIAL_LEVEL
	return true
}

proc update_PARAM_VALUE.IRQ_MODE_RESET { PARAM_VALUE.IRQ_MODE_RESET } {
	# Procedure called to update IRQ_MODE_RESET when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IRQ_MODE_RESET { PARAM_VALUE.IRQ_MODE_RESET } {
	# Procedure called to validate IRQ_MODE_RESET
	return true
}

proc update_PARAM_VALUE.IRQ_SYNC_ENABLE { PARAM_VALUE.IRQ_SYNC_ENABLE } {
	# Procedure called to update IRQ_SYNC_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IRQ_SYNC_ENABLE { PARAM_VALUE.IRQ_SYNC_ENABLE } {
	# Procedure called to validate IRQ_SYNC_ENABLE
	return true
}

proc update_PARAM_VALUE.MONITOR_BASE { PARAM_VALUE.MONITOR_BASE } {
	# Procedure called to update MONITOR_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.MONITOR_BASE { PARAM_VALUE.MONITOR_BASE } {
	# Procedure called to validate MONITOR_BASE
	return true
}

proc update_PARAM_VALUE.MONITOR_ENABLE { PARAM_VALUE.MONITOR_ENABLE } {
	# Procedure called to update MONITOR_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.MONITOR_ENABLE { PARAM_VALUE.MONITOR_ENABLE } {
	# Procedure called to validate MONITOR_ENABLE
	return true
}

proc update_PARAM_VALUE.MONITOR_MASK { PARAM_VALUE.MONITOR_MASK } {
	# Procedure called to update MONITOR_MASK when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.MONITOR_MASK { PARAM_VALUE.MONITOR_MASK } {
	# Procedure called to validate MONITOR_MASK
	return true
}

proc update_PARAM_VALUE.MTVEC_RESET { PARAM_VALUE.MTVEC_RESET } {
	# Procedure called to update MTVEC_RESET when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.MTVEC_RESET { PARAM_VALUE.MTVEC_RESET } {
	# Procedure called to validate MTVEC_RESET
	return true
}

proc update_PARAM_VALUE.MUL_ENABLE { PARAM_VALUE.MUL_ENABLE } {
	# Procedure called to update MUL_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.MUL_ENABLE { PARAM_VALUE.MUL_ENABLE } {
	# Procedure called to validate MUL_ENABLE
	return true
}

proc update_PARAM_VALUE.PLIC_BASE { PARAM_VALUE.PLIC_BASE } {
	# Procedure called to update PLIC_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.PLIC_BASE { PARAM_VALUE.PLIC_BASE } {
	# Procedure called to validate PLIC_BASE
	return true
}

proc update_PARAM_VALUE.PLIC_ENABLE { PARAM_VALUE.PLIC_ENABLE } {
	# Procedure called to update PLIC_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.PLIC_ENABLE { PARAM_VALUE.PLIC_ENABLE } {
	# Procedure called to validate PLIC_ENABLE
	return true
}

proc update_PARAM_VALUE.PLIC_MASK { PARAM_VALUE.PLIC_MASK } {
	# Procedure called to update PLIC_MASK when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.PLIC_MASK { PARAM_VALUE.PLIC_MASK } {
	# Procedure called to validate PLIC_MASK
	return true
}

proc update_PARAM_VALUE.PLIC_PRIO_BITS { PARAM_VALUE.PLIC_PRIO_BITS } {
	# Procedure called to update PLIC_PRIO_BITS when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.PLIC_PRIO_BITS { PARAM_VALUE.PLIC_PRIO_BITS } {
	# Procedure called to validate PLIC_PRIO_BITS
	return true
}

proc update_PARAM_VALUE.PLIC_SOURCES { PARAM_VALUE.PLIC_SOURCES } {
	# Procedure called to update PLIC_SOURCES when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.PLIC_SOURCES { PARAM_VALUE.PLIC_SOURCES } {
	# Procedure called to validate PLIC_SOURCES
	return true
}

proc update_PARAM_VALUE.RESET_VECTOR { PARAM_VALUE.RESET_VECTOR } {
	# Procedure called to update RESET_VECTOR when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.RESET_VECTOR { PARAM_VALUE.RESET_VECTOR } {
	# Procedure called to validate RESET_VECTOR
	return true
}

proc update_PARAM_VALUE.SHADOW_BANK_ENABLE { PARAM_VALUE.SHADOW_BANK_ENABLE } {
	# Procedure called to update SHADOW_BANK_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.SHADOW_BANK_ENABLE { PARAM_VALUE.SHADOW_BANK_ENABLE } {
	# Procedure called to validate SHADOW_BANK_ENABLE
	return true
}

proc update_PARAM_VALUE.TIMER_BASE { PARAM_VALUE.TIMER_BASE } {
	# Procedure called to update TIMER_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.TIMER_BASE { PARAM_VALUE.TIMER_BASE } {
	# Procedure called to validate TIMER_BASE
	return true
}

proc update_PARAM_VALUE.TIMER_ENABLE { PARAM_VALUE.TIMER_ENABLE } {
	# Procedure called to update TIMER_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.TIMER_ENABLE { PARAM_VALUE.TIMER_ENABLE } {
	# Procedure called to validate TIMER_ENABLE
	return true
}

proc update_PARAM_VALUE.TIMER_IRQ_ID { PARAM_VALUE.TIMER_IRQ_ID } {
	# Procedure called to update TIMER_IRQ_ID when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.TIMER_IRQ_ID { PARAM_VALUE.TIMER_IRQ_ID } {
	# Procedure called to validate TIMER_IRQ_ID
	return true
}

proc update_PARAM_VALUE.TIMER_MASK { PARAM_VALUE.TIMER_MASK } {
	# Procedure called to update TIMER_MASK when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.TIMER_MASK { PARAM_VALUE.TIMER_MASK } {
	# Procedure called to validate TIMER_MASK
	return true
}

proc update_PARAM_VALUE.TRACE_ENABLE { PARAM_VALUE.TRACE_ENABLE } {
	# Procedure called to update TRACE_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.TRACE_ENABLE { PARAM_VALUE.TRACE_ENABLE } {
	# Procedure called to validate TRACE_ENABLE
	return true
}

proc update_PARAM_VALUE.UART_BASE { PARAM_VALUE.UART_BASE } {
	# Procedure called to update UART_BASE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.UART_BASE { PARAM_VALUE.UART_BASE } {
	# Procedure called to validate UART_BASE
	return true
}

proc update_PARAM_VALUE.UART_ENABLE { PARAM_VALUE.UART_ENABLE } {
	# Procedure called to update UART_ENABLE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.UART_ENABLE { PARAM_VALUE.UART_ENABLE } {
	# Procedure called to validate UART_ENABLE
	return true
}

proc update_PARAM_VALUE.UART_IRQ_ID { PARAM_VALUE.UART_IRQ_ID } {
	# Procedure called to update UART_IRQ_ID when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.UART_IRQ_ID { PARAM_VALUE.UART_IRQ_ID } {
	# Procedure called to validate UART_IRQ_ID
	return true
}

proc update_PARAM_VALUE.UART_MASK { PARAM_VALUE.UART_MASK } {
	# Procedure called to update UART_MASK when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.UART_MASK { PARAM_VALUE.UART_MASK } {
	# Procedure called to validate UART_MASK
	return true
}


proc update_MODELPARAM_VALUE.RESET_VECTOR { MODELPARAM_VALUE.RESET_VECTOR PARAM_VALUE.RESET_VECTOR } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.RESET_VECTOR}] ${MODELPARAM_VALUE.RESET_VECTOR}
}

proc update_MODELPARAM_VALUE.MTVEC_RESET { MODELPARAM_VALUE.MTVEC_RESET PARAM_VALUE.MTVEC_RESET } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.MTVEC_RESET}] ${MODELPARAM_VALUE.MTVEC_RESET}
}

proc update_MODELPARAM_VALUE.SHADOW_BANK_ENABLE { MODELPARAM_VALUE.SHADOW_BANK_ENABLE PARAM_VALUE.SHADOW_BANK_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.SHADOW_BANK_ENABLE}] ${MODELPARAM_VALUE.SHADOW_BANK_ENABLE}
}

proc update_MODELPARAM_VALUE.AXI_ENABLE { MODELPARAM_VALUE.AXI_ENABLE PARAM_VALUE.AXI_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.AXI_ENABLE}] ${MODELPARAM_VALUE.AXI_ENABLE}
}

proc update_MODELPARAM_VALUE.CLINT_ENABLE { MODELPARAM_VALUE.CLINT_ENABLE PARAM_VALUE.CLINT_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.CLINT_ENABLE}] ${MODELPARAM_VALUE.CLINT_ENABLE}
}

proc update_MODELPARAM_VALUE.PLIC_ENABLE { MODELPARAM_VALUE.PLIC_ENABLE PARAM_VALUE.PLIC_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.PLIC_ENABLE}] ${MODELPARAM_VALUE.PLIC_ENABLE}
}

proc update_MODELPARAM_VALUE.GPIO_ENABLE { MODELPARAM_VALUE.GPIO_ENABLE PARAM_VALUE.GPIO_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.GPIO_ENABLE}] ${MODELPARAM_VALUE.GPIO_ENABLE}
}

proc update_MODELPARAM_VALUE.TIMER_ENABLE { MODELPARAM_VALUE.TIMER_ENABLE PARAM_VALUE.TIMER_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.TIMER_ENABLE}] ${MODELPARAM_VALUE.TIMER_ENABLE}
}

proc update_MODELPARAM_VALUE.UART_ENABLE { MODELPARAM_VALUE.UART_ENABLE PARAM_VALUE.UART_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.UART_ENABLE}] ${MODELPARAM_VALUE.UART_ENABLE}
}

proc update_MODELPARAM_VALUE.MONITOR_ENABLE { MODELPARAM_VALUE.MONITOR_ENABLE PARAM_VALUE.MONITOR_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.MONITOR_ENABLE}] ${MODELPARAM_VALUE.MONITOR_ENABLE}
}

proc update_MODELPARAM_VALUE.MUL_ENABLE { MODELPARAM_VALUE.MUL_ENABLE PARAM_VALUE.MUL_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.MUL_ENABLE}] ${MODELPARAM_VALUE.MUL_ENABLE}
}

proc update_MODELPARAM_VALUE.DIV_ENABLE { MODELPARAM_VALUE.DIV_ENABLE PARAM_VALUE.DIV_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.DIV_ENABLE}] ${MODELPARAM_VALUE.DIV_ENABLE}
}

proc update_MODELPARAM_VALUE.BP_ENABLE { MODELPARAM_VALUE.BP_ENABLE PARAM_VALUE.BP_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.BP_ENABLE}] ${MODELPARAM_VALUE.BP_ENABLE}
}

proc update_MODELPARAM_VALUE.TRACE_ENABLE { MODELPARAM_VALUE.TRACE_ENABLE PARAM_VALUE.TRACE_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.TRACE_ENABLE}] ${MODELPARAM_VALUE.TRACE_ENABLE}
}

proc update_MODELPARAM_VALUE.DEBUG_ENABLE { MODELPARAM_VALUE.DEBUG_ENABLE PARAM_VALUE.DEBUG_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.DEBUG_ENABLE}] ${MODELPARAM_VALUE.DEBUG_ENABLE}
}

proc update_MODELPARAM_VALUE.IMEM_WORDS { MODELPARAM_VALUE.IMEM_WORDS PARAM_VALUE.IMEM_WORDS } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IMEM_WORDS}] ${MODELPARAM_VALUE.IMEM_WORDS}
}

proc update_MODELPARAM_VALUE.DMEM_WORDS { MODELPARAM_VALUE.DMEM_WORDS PARAM_VALUE.DMEM_WORDS } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.DMEM_WORDS}] ${MODELPARAM_VALUE.DMEM_WORDS}
}

proc update_MODELPARAM_VALUE.PLIC_SOURCES { MODELPARAM_VALUE.PLIC_SOURCES PARAM_VALUE.PLIC_SOURCES } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.PLIC_SOURCES}] ${MODELPARAM_VALUE.PLIC_SOURCES}
}

proc update_MODELPARAM_VALUE.PLIC_PRIO_BITS { MODELPARAM_VALUE.PLIC_PRIO_BITS PARAM_VALUE.PLIC_PRIO_BITS } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.PLIC_PRIO_BITS}] ${MODELPARAM_VALUE.PLIC_PRIO_BITS}
}

proc update_MODELPARAM_VALUE.IRQ_SYNC_ENABLE { MODELPARAM_VALUE.IRQ_SYNC_ENABLE PARAM_VALUE.IRQ_SYNC_ENABLE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IRQ_SYNC_ENABLE}] ${MODELPARAM_VALUE.IRQ_SYNC_ENABLE}
}

proc update_MODELPARAM_VALUE.IRQ_MODE_RESET { MODELPARAM_VALUE.IRQ_MODE_RESET PARAM_VALUE.IRQ_MODE_RESET } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IRQ_MODE_RESET}] ${MODELPARAM_VALUE.IRQ_MODE_RESET}
}

proc update_MODELPARAM_VALUE.IRQ_INITIAL_LEVEL { MODELPARAM_VALUE.IRQ_INITIAL_LEVEL PARAM_VALUE.IRQ_INITIAL_LEVEL } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IRQ_INITIAL_LEVEL}] ${MODELPARAM_VALUE.IRQ_INITIAL_LEVEL}
}

proc update_MODELPARAM_VALUE.IMEM_BASE { MODELPARAM_VALUE.IMEM_BASE PARAM_VALUE.IMEM_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IMEM_BASE}] ${MODELPARAM_VALUE.IMEM_BASE}
}

proc update_MODELPARAM_VALUE.DMEM_BASE { MODELPARAM_VALUE.DMEM_BASE PARAM_VALUE.DMEM_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.DMEM_BASE}] ${MODELPARAM_VALUE.DMEM_BASE}
}

proc update_MODELPARAM_VALUE.DMEM_MASK { MODELPARAM_VALUE.DMEM_MASK PARAM_VALUE.DMEM_MASK } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.DMEM_MASK}] ${MODELPARAM_VALUE.DMEM_MASK}
}

proc update_MODELPARAM_VALUE.AXI_BASE { MODELPARAM_VALUE.AXI_BASE PARAM_VALUE.AXI_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.AXI_BASE}] ${MODELPARAM_VALUE.AXI_BASE}
}

proc update_MODELPARAM_VALUE.AXI_MASK { MODELPARAM_VALUE.AXI_MASK PARAM_VALUE.AXI_MASK } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.AXI_MASK}] ${MODELPARAM_VALUE.AXI_MASK}
}

proc update_MODELPARAM_VALUE.CLINT_BASE { MODELPARAM_VALUE.CLINT_BASE PARAM_VALUE.CLINT_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.CLINT_BASE}] ${MODELPARAM_VALUE.CLINT_BASE}
}

proc update_MODELPARAM_VALUE.CLINT_MASK { MODELPARAM_VALUE.CLINT_MASK PARAM_VALUE.CLINT_MASK } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.CLINT_MASK}] ${MODELPARAM_VALUE.CLINT_MASK}
}

proc update_MODELPARAM_VALUE.PLIC_BASE { MODELPARAM_VALUE.PLIC_BASE PARAM_VALUE.PLIC_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.PLIC_BASE}] ${MODELPARAM_VALUE.PLIC_BASE}
}

proc update_MODELPARAM_VALUE.PLIC_MASK { MODELPARAM_VALUE.PLIC_MASK PARAM_VALUE.PLIC_MASK } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.PLIC_MASK}] ${MODELPARAM_VALUE.PLIC_MASK}
}

proc update_MODELPARAM_VALUE.GPIO_BASE { MODELPARAM_VALUE.GPIO_BASE PARAM_VALUE.GPIO_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.GPIO_BASE}] ${MODELPARAM_VALUE.GPIO_BASE}
}

proc update_MODELPARAM_VALUE.GPIO_MASK { MODELPARAM_VALUE.GPIO_MASK PARAM_VALUE.GPIO_MASK } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.GPIO_MASK}] ${MODELPARAM_VALUE.GPIO_MASK}
}

proc update_MODELPARAM_VALUE.TIMER_BASE { MODELPARAM_VALUE.TIMER_BASE PARAM_VALUE.TIMER_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.TIMER_BASE}] ${MODELPARAM_VALUE.TIMER_BASE}
}

proc update_MODELPARAM_VALUE.TIMER_MASK { MODELPARAM_VALUE.TIMER_MASK PARAM_VALUE.TIMER_MASK } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.TIMER_MASK}] ${MODELPARAM_VALUE.TIMER_MASK}
}

proc update_MODELPARAM_VALUE.UART_BASE { MODELPARAM_VALUE.UART_BASE PARAM_VALUE.UART_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.UART_BASE}] ${MODELPARAM_VALUE.UART_BASE}
}

proc update_MODELPARAM_VALUE.UART_MASK { MODELPARAM_VALUE.UART_MASK PARAM_VALUE.UART_MASK } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.UART_MASK}] ${MODELPARAM_VALUE.UART_MASK}
}

proc update_MODELPARAM_VALUE.MONITOR_BASE { MODELPARAM_VALUE.MONITOR_BASE PARAM_VALUE.MONITOR_BASE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.MONITOR_BASE}] ${MODELPARAM_VALUE.MONITOR_BASE}
}

proc update_MODELPARAM_VALUE.MONITOR_MASK { MODELPARAM_VALUE.MONITOR_MASK PARAM_VALUE.MONITOR_MASK } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.MONITOR_MASK}] ${MODELPARAM_VALUE.MONITOR_MASK}
}

proc update_MODELPARAM_VALUE.TIMER_IRQ_ID { MODELPARAM_VALUE.TIMER_IRQ_ID PARAM_VALUE.TIMER_IRQ_ID } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.TIMER_IRQ_ID}] ${MODELPARAM_VALUE.TIMER_IRQ_ID}
}

proc update_MODELPARAM_VALUE.UART_IRQ_ID { MODELPARAM_VALUE.UART_IRQ_ID PARAM_VALUE.UART_IRQ_ID } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.UART_IRQ_ID}] ${MODELPARAM_VALUE.UART_IRQ_ID}
}

proc update_MODELPARAM_VALUE.GPIO_WIDTH { MODELPARAM_VALUE.GPIO_WIDTH PARAM_VALUE.GPIO_WIDTH } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.GPIO_WIDTH}] ${MODELPARAM_VALUE.GPIO_WIDTH}
}

proc update_MODELPARAM_VALUE.IMEM_INIT_FILE { MODELPARAM_VALUE.IMEM_INIT_FILE PARAM_VALUE.IMEM_INIT_FILE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IMEM_INIT_FILE}] ${MODELPARAM_VALUE.IMEM_INIT_FILE}
}

proc update_MODELPARAM_VALUE.DMEM_INIT_FILE { MODELPARAM_VALUE.DMEM_INIT_FILE PARAM_VALUE.DMEM_INIT_FILE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.DMEM_INIT_FILE}] ${MODELPARAM_VALUE.DMEM_INIT_FILE}
}

proc update_MODELPARAM_VALUE.BP_ENTRIES { MODELPARAM_VALUE.BP_ENTRIES PARAM_VALUE.BP_ENTRIES } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.BP_ENTRIES}] ${MODELPARAM_VALUE.BP_ENTRIES}
}

proc update_MODELPARAM_VALUE.BP_GSHARE { MODELPARAM_VALUE.BP_GSHARE PARAM_VALUE.BP_GSHARE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.BP_GSHARE}] ${MODELPARAM_VALUE.BP_GSHARE}
}

proc update_MODELPARAM_VALUE.GHR_WIDTH { MODELPARAM_VALUE.GHR_WIDTH PARAM_VALUE.GHR_WIDTH } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.GHR_WIDTH}] ${MODELPARAM_VALUE.GHR_WIDTH}
}


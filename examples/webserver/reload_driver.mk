# might be good to check that the elf file changes are ok.

ELFS := \
	timer_driver.elf \
	serial_driver.elf \
	eth_driver.elf \
	micropython.elf \
	nfs.elf \
	network_copy.elf \
	network_virt_rx.elf \
	network_virt_tx.elf \
	reloader.elf

all: copy

$(info driver is: $(driver))
$(info our dir is: ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk)

ifeq ($(driver),serial_driver)
	include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk
else ifeq ($(driver),timer_driver)
	include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
else ifeq ($(driver),ethernet_driver)
	include ${SDDF}/drivers/network/${NET_DRIV_DIR}/eth_driver.mk
else ifeq ($(driver),serial_virt_tx)
	include ${SDDF}/serial/components/serial_components.mk
else ifeq ($(driver),net_virt_tx)
	include ${SDDF}/network/components/network_components.mk
else ifeq ($(driver),net_virt_rx)
	include ${SDDF}/network/components/network_components.mk
else ifeq ($(driver),micropython_net_copier)
	include ${SDDF}/network/components/network_components.mk
else ifeq ($(driver),nfs_net_copier)
	include ${SDDF}/network/components/network_components.mk
else
	$(error Unknown driver '$(driver)')
endif

$(info parsing reload_driver.mk)
$(info parsing reload_driver.mk)
$(info parsing reload_driver.mk)
$(info parsing reload_driver.mk)
$(info parsing reload_driver.mk)

# .PHONY: copy
copy: $(ELFS)
	$(info we are running objcopy on the timer driver stuff now!)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_micropython_net_copier.data network_copy.elf network_copy_micropython.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_nfs_net_copier.data network_copy.elf network_copy_nfs.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_nfs.data nfs.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_nfs.data nfs.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .nfs_config=nfs_config.data nfs.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_nfs.data nfs.elf
	$(OBJCOPY) --update-section .lib_sddf_lwip_config=lib_sddf_lwip_config_micropython.data micropython.elf
	$(OBJCOPY) --update-section .reloading_dependencies=reloading_dependencies.data reloader.elf

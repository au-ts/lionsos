SUPPORTED_BOARDS := \
	rpi4b_1gb \
	maaxboard \
	qemu_virt_aarch64

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)), rpi4b_1gb)
  # BCM2711, 1 GiB of DRAM.
  SERIAL_DRIV_DIR ?= ns16550a
  CPU ?= cortex-a72
  PAGER_INIT_FRAMES ?= 40000
else ifeq ($(strip $(MICROKIT_BOARD)), maaxboard)

  SERIAL_DRIV_DIR ?= imx
  CPU ?= cortex-a53

  PAGER_INIT_FRAMES ?= 80000
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
  SERIAL_DRIV_DIR ?= arm
  CPU ?= cortex-a53

  PAGER_INIT_FRAMES ?= 40000
  QEMU ?= qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD '$(MICROKIT_BOARD)' -- supported boards are: $(SUPPORTED_BOARDS))
endif

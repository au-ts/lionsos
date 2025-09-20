################################################################################
# Makefile
################################################################################

#===========================================================
# Check
#===========================================================
ifndef FORCE
    EXP_INFO := sel4devkit-maaxboard-microkit-docker-dev-env 1 *
    CHK_PATH_FILE := /check.mk
    ifeq ($(wildcard ${CHK_PATH_FILE}),)
        HALT := TRUE
    else
        include ${CHK_PATH_FILE}
    endif
    ifdef HALT
        $(error Expected Environment Not Found: ${EXP_INFO})
    endif
endif

#===========================================================
# Layout
#===========================================================
OUT_PATH := out
LIB_PATH := sel4devkit-maaxboard-microkit

#===========================================================
# Usage
#===========================================================
.PHONY: usage
usage: 
	@echo "usage: make <target> [FORCE=TRUE]"
	@echo ""
	@echo "<target> is one off:"
	@echo "get"
	@echo "clean"
#===========================================================
# Target
#===========================================================

.PHONY: get
get: | ${OUT_PATH}
	git -C ${OUT_PATH} clone --branch "main" "git@github.com:sel4devkit/sel4devkit-maaxboard-microkit.git" ${LIB_PATH}

${OUT_PATH}:
	mkdir ${OUT_PATH}

.PHONY: clean
clean:
	sudo rm -rf ${OUT_PATH}
	sudo rm -rf ${LIB_PATH}

################################################################################
# End of file
################################################################################

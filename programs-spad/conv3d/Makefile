BENCHNAME := conv3d
default: $(BENCHNAME)

# run small sim for test
PROB_SIZE := 32 32 32
N_SPS ?= 64
EXTRA_FLAGS ?= -DVECTOR_LEN=4
HB_ARGS := --options="$(PROB_SIZE)"
# GEM5_ARGS ?= --remote-gdb-port=0 --debug-flags=IEW

# Build the kernel with trilliasm.
TRILLIASM_KERNEL := conv3d_kernel.c
include ../../trillium/trilliasm.mk

# Standard benchmark compilation.
include ../common/common.mk

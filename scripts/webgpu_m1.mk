# VOSC_DAWN_SDK points to the extracted, pinned Dawn Release SDK.
ifneq ($(strip $(VOSC_DAWN_SDK)),)
VOSC_DAWN_ARCHIVE := $(VOSC_DAWN_SDK)/lib64/libwebgpu_dawn.a
ifeq ($(wildcard $(VOSC_DAWN_ARCHIVE)),)
$(error WebGPU M1 Dawn archive missing: $(VOSC_DAWN_ARCHIVE))
endif
override PROJECT_CFLAGS += -DVOSC_WEBGPU_M1=1 -I$(VOSC_DAWN_SDK)/include
override PROJECT_LDFLAGS += $(VOSC_DAWN_ARCHIVE) -lcrypto -ldl -lrt
endif

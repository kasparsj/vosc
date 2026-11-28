VOSC_PROFILE ?= default
VOSC_OPERATOR_UI ?= 1

ifneq ($(VOSC_OPERATOR_UI),0)
ifneq ($(VOSC_OPERATOR_UI),1)
$(error VOSC_OPERATOR_UI must be 0 or 1)
endif
endif

ifneq ($(VOSC_PROFILE),default)
ifneq ($(VOSC_PROFILE),minimal)
$(error Unknown VOSC_PROFILE '$(VOSC_PROFILE)'; use default or minimal)
endif
endif

ifeq ($(VOSC_PROFILE),minimal)
ifneq ($(shell uname -s),Linux)
$(error VOSC_PROFILE=minimal uses the Linux openFrameworks Make build)
endif
include config.make
VOSC_PROFILE_MAKE := $(MAKE)

.PHONY: all Release
all: Release

Release:
	python3 scripts/build_linux_minimal.py --make "$(VOSC_PROFILE_MAKE)" --of-root "$(OF_ROOT)" $(if $(VOSC_DAWN_SDK),--dawn-sdk "$(VOSC_DAWN_SDK)")
else
ifeq ($(shell uname -s),Darwin)
XCODEBUILD ?= xcodebuild

.PHONY: all Debug Release CleanDebug CleanRelease clean run RunDebug RunRelease help
all: Release

Debug Release:
	python3 scripts/build_macos_profile.py --xcodebuild "$(XCODEBUILD)" --configuration "$@" --in-place -- VOSC_OPERATOR_UI=$(VOSC_OPERATOR_UI) $(if $(VOSC_DAWN_SDK),VOSC_WEBGPU_M1=1 "VOSC_DAWN_SDK=$(VOSC_DAWN_SDK)") $(XCODEBUILD_FLAGS)

clean: CleanDebug CleanRelease

CleanDebug CleanRelease:
	python3 scripts/build_macos_profile.py --xcodebuild "$(XCODEBUILD)" --configuration "$(@:Clean%=%)" --in-place --clean -- $(XCODEBUILD_FLAGS)

run RunRelease:
	cd bin && ./vosc.app/Contents/MacOS/vosc

RunDebug:
	cd bin && ./voscDebug.app/Contents/MacOS/voscDebug

help:
	@printf '%s\n' 'make Debug or make Release builds the corresponding Xcode scheme' 'make clean cleans both schemes' 'make RunDebug or make RunRelease launches a built app'
else
# Attempt to load a config.make file.
# If none is found, project defaults in config.project.make will be used.
ifneq ($(wildcard config.make),)
	include config.make
endif

OF_ROOT ?= ../../..

PROJECT_ROOT ?= .
ifneq ($(filter ReleaseABI DebugABI CleanReleaseABI CleanDebugABI,$(MAKECMDGOALS)),)
OF_ADDONS_PATH ?= $(OF_ROOT)/addons
# The Linux renderer disables HPV and HTML textures.
VOSC_LINUX_ADDONS := $(filter-out ofxHPVPlayer ofxUltralight,$(shell sed 's/[[:space:]]*\#.*//' "$(PROJECT_ROOT)/addons.make"))
override REQUESTED_PROJECT_ADDONS = $(call remove-dupes-func,$(PLATFORM_REQUIRED_ADDONS) $(VOSC_LINUX_ADDONS))
# OF's addon source search excludes paths with hidden components.
VOSC_RENDER_ADDONS := $(shell python3 scripts/stage_linux_render_addons.py "$(OF_ADDONS_PATH)" "$(abspath build/vosc-render-addons)")
ifeq ($(VOSC_RENDER_ADDONS),)
$(error Could not prepare VOSC render addon sources)
endif
override OF_ADDONS_PATH := $(VOSC_RENDER_ADDONS)
endif
override PROJECT_CFLAGS += -DVOSC_OPERATOR_UI=$(VOSC_OPERATOR_UI) -DOFXEXPR_NOGUI
override PROJECT_EXCLUSIONS += $(PROJECT_ROOT)/src/macos $(PROJECT_ROOT)/src/macos/%
ifeq ($(VOSC_OPERATOR_UI),1)
override PROJECT_EXCLUSIONS += $(PROJECT_ROOT)/src/operator/macos $(PROJECT_ROOT)/src/operator/macos/%
VOSC_RMLUI_DIR ?= $(abspath .vosc-linux/operator-ui/$(or $(ABI),native))
VOSC_RMLUI_FONT := $(VOSC_RMLUI_DIR)/generated/VoscOperatorFont.hpp
VOSC_RMLUI_LIBS := $(VOSC_RMLUI_DIR)/lib/libvosc_rmlui_gl3.a $(VOSC_RMLUI_DIR)/lib/librmlui.a
override PROJECT_CFLAGS += -I$(VOSC_RMLUI_DIR)/source/Include -I$(VOSC_RMLUI_DIR)/source/Backends -I$(VOSC_RMLUI_DIR)/generated
override PROJECT_LDFLAGS += $(VOSC_RMLUI_LIBS) -lfreetype -ldl
else
override PROJECT_EXCLUSIONS += $(PROJECT_ROOT)/src/operator $(PROJECT_ROOT)/src/operator/%
endif

include scripts/webgpu_m1.mk
include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk

ifeq ($(VOSC_OPERATOR_UI),1)
ifneq ($(filter ReleaseABI DebugABI,$(MAKECMDGOALS)),)
$(OF_PROJECT_OBJS): | $(VOSC_RMLUI_FONT)
$(TARGET): $(VOSC_RMLUI_LIBS)

$(VOSC_RMLUI_LIBS): | $(VOSC_RMLUI_FONT)
	@test -f "$@"

$(VOSC_RMLUI_FONT): force
	CC="$(CC)" CXX="$(CXX)" python3 scripts/build_rmlui.py --output "$(VOSC_RMLUI_DIR)"
endif
endif

.PHONY: vosc-stage-assets
ReleaseABI DebugABI: | vosc-stage-assets

vosc-stage-assets:
	python3 scripts/stage_assets.py bin/data --addon-sdk "$(OF_ROOT)"
endif
endif

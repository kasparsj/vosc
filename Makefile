VOSC_PROFILE ?= default

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
	python3 scripts/build_linux_minimal.py --make "$(VOSC_PROFILE_MAKE)" --of-root "$(OF_ROOT)"
else
ifeq ($(shell uname -s),Darwin)
XCODEBUILD ?= xcodebuild

.PHONY: all Debug Release CleanDebug CleanRelease clean run RunDebug RunRelease help
all: Release

Debug Release:
	python3 scripts/build_macos_profile.py --xcodebuild "$(XCODEBUILD)" --configuration "$@" --in-place -- $(XCODEBUILD_FLAGS)

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

# call the project makefile!
include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk
endif
endif

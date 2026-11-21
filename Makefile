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

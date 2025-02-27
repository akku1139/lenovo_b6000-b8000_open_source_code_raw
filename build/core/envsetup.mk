# Variables we check:
#     HOST_BUILD_TYPE = { release debug }
#     TARGET_BUILD_TYPE = { release debug }
# and we output a bunch of variables, see the case statement at
# the bottom for the full list
#     OUT_DIR is also set to "out" if it's not already set.
#         this allows you to set it to somewhere else if you like

# Set up version information.
include $(BUILD_SYSTEM)/version_defaults.mk

# ---------------------------------------------------------------
# If you update the build system such that the environment setup
# or buildspec.mk need to be updated, increment this number, and
# people who haven't re-run those will have to do so before they
# can build.  Make sure to also update the corresponding value in
# buildspec.mk.default and envsetup.sh.
CORRECT_BUILD_ENV_SEQUENCE_NUMBER := 10

# ---------------------------------------------------------------
# The product defaults to generic on hardware
# NOTE: This will be overridden in product_config.mk if make
# was invoked with a PRODUCT-xxx-yyy goal.
ifeq ($(TARGET_PRODUCT),)
TARGET_PRODUCT := full
endif


# the variant -- the set of files that are included for a build
ifeq ($(strip $(TARGET_BUILD_VARIANT)),)
TARGET_BUILD_VARIANT := eng
endif

# ---------------------------------------------------------------
# Set up configuration for host machine.  We don't do cross-
# compiles except for arm/mips, so the HOST is whatever we are
# running on

UNAME := $(shell uname -sm)

# HOST_OS
ifneq (,$(findstring Linux,$(UNAME)))
	HOST_OS := linux
endif
ifneq (,$(findstring Darwin,$(UNAME)))
	HOST_OS := darwin
endif
ifneq (,$(findstring Macintosh,$(UNAME)))
	HOST_OS := darwin
endif
ifneq (,$(findstring CYGWIN,$(UNAME)))
	HOST_OS := windows
endif

# BUILD_OS is the real host doing the build.
BUILD_OS := $(HOST_OS)

# Under Linux, if USE_MINGW is set, we change HOST_OS to Windows to build the
# Windows SDK. Only a subset of tools and SDK will manage to build properly.
ifeq ($(HOST_OS),linux)
ifneq ($(USE_MINGW),)
	HOST_OS := windows
endif
endif

ifeq ($(HOST_OS),)
$(error Unable to determine HOST_OS from uname -sm: $(UNAME)!)
endif


# HOST_ARCH
ifneq (,$(findstring 86,$(UNAME)))
	HOST_ARCH := x86
endif

ifneq (,$(findstring Power,$(UNAME)))
	HOST_ARCH := ppc
endif

BUILD_ARCH := $(HOST_ARCH)

ifeq ($(HOST_ARCH),)
$(error Unable to determine HOST_ARCH from uname -sm: $(UNAME)!)
endif

# the host build defaults to release, and it must be release or debug
ifeq ($(HOST_BUILD_TYPE),)
HOST_BUILD_TYPE := release
endif

ifneq ($(HOST_BUILD_TYPE),release)
ifneq ($(HOST_BUILD_TYPE),debug)
$(error HOST_BUILD_TYPE must be either release or debug, not '$(HOST_BUILD_TYPE)')
endif
endif

# This is the standard way to name a directory containing prebuilt host
# objects. E.g., prebuilt/$(HOST_PREBUILT_TAG)/cc
ifeq ($(HOST_OS),windows)
  HOST_PREBUILT_TAG := windows
else
  HOST_PREBUILT_TAG := $(HOST_OS)-$(HOST_ARCH)
endif

# TARGET_COPY_OUT_* are all relative to the staging directory, ie PRODUCT_OUT.
# Define them here so they can be used in product config files.
TARGET_COPY_OUT_SYSTEM := system
TARGET_COPY_OUT_DATA := data
TARGET_COPY_OUT_VENDOR := system/vendor
TARGET_COPY_OUT_ROOT := root
TARGET_COPY_OUT_RECOVERY := recovery

# Read the product specs so we an get TARGET_DEVICE and other
# variables that we need in order to locate the output files.
include $(BUILD_SYSTEM)/product_config.mk

build_variant := $(filter-out eng user userdebug,$(TARGET_BUILD_VARIANT))
ifneq ($(build_variant)-$(words $(TARGET_BUILD_VARIANT)),-1)
$(warning bad TARGET_BUILD_VARIANT: $(TARGET_BUILD_VARIANT))
$(error must be empty or one of: eng user userdebug)
endif

# ---------------------------------------------------------------
# Set up configuration for target machine.
# The following must be set:
# 		TARGET_OS = { linux }
# 		TARGET_ARCH = { arm | x86 | mips }

TARGET_OS := linux
# TARGET_ARCH should be set by BoardConfig.mk and will be checked later

# the target build type defaults to release
ifneq ($(TARGET_BUILD_TYPE),debug)
TARGET_BUILD_TYPE := release
endif

# ---------------------------------------------------------------
# figure out the output directories

ifeq (,$(strip $(OUT_DIR)))
ifeq (,$(strip $(OUT_DIR_COMMON_BASE)))
OUT_DIR := $(TOPDIR)out
else
OUT_DIR := $(OUT_DIR_COMMON_BASE)/$(notdir $(PWD))
endif
endif

DEBUG_OUT_DIR := $(OUT_DIR)/debug

# Move the host or target under the debug/ directory
# if necessary.
TARGET_OUT_ROOT_release := $(OUT_DIR)/target
TARGET_OUT_ROOT_debug := $(DEBUG_OUT_DIR)/target
TARGET_OUT_ROOT := $(TARGET_OUT_ROOT_$(TARGET_BUILD_TYPE))

HOST_OUT_ROOT_release := $(OUT_DIR)/host
HOST_OUT_ROOT_debug := $(DEBUG_OUT_DIR)/host
HOST_OUT_ROOT := $(HOST_OUT_ROOT_$(HOST_BUILD_TYPE))

HOST_OUT_release := $(HOST_OUT_ROOT_release)/$(HOST_OS)-$(HOST_ARCH)
HOST_OUT_debug := $(HOST_OUT_ROOT_debug)/$(HOST_OS)-$(HOST_ARCH)
HOST_OUT := $(HOST_OUT_$(HOST_BUILD_TYPE))

BUILD_OUT := $(OUT_DIR)/host/$(BUILD_OS)-$(BUILD_ARCH)

TARGET_PRODUCT_OUT_ROOT := $(TARGET_OUT_ROOT)/product

TARGET_COMMON_OUT_ROOT := $(TARGET_OUT_ROOT)/common
HOST_COMMON_OUT_ROOT := $(HOST_OUT_ROOT)/common

PRODUCT_OUT := $(TARGET_PRODUCT_OUT_ROOT)/$(TARGET_DEVICE)

OUT_DOCS := $(TARGET_COMMON_OUT_ROOT)/docs

BUILD_OUT_EXECUTABLES:= $(BUILD_OUT)/bin

HOST_OUT_EXECUTABLES:= $(HOST_OUT)/bin
HOST_OUT_SHARED_LIBRARIES:= $(HOST_OUT)/lib
HOST_OUT_JAVA_LIBRARIES:= $(HOST_OUT)/framework
HOST_OUT_SDK_ADDON := $(HOST_OUT)/sdk_addon

HOST_OUT_INTERMEDIATES := $(HOST_OUT)/obj
HOST_OUT_HEADERS:= $(HOST_OUT_INTERMEDIATES)/include
HOST_OUT_INTERMEDIATE_LIBRARIES := $(HOST_OUT_INTERMEDIATES)/lib
HOST_OUT_NOTICE_FILES:=$(HOST_OUT_INTERMEDIATES)/NOTICE_FILES
HOST_OUT_COMMON_INTERMEDIATES := $(HOST_COMMON_OUT_ROOT)/obj

TARGET_OUT_INTERMEDIATES := $(PRODUCT_OUT)/obj
TARGET_OUT_HEADERS:= $(TARGET_OUT_INTERMEDIATES)/include
TARGET_OUT_INTERMEDIATE_LIBRARIES := $(TARGET_OUT_INTERMEDIATES)/lib
TARGET_OUT_COMMON_INTERMEDIATES := $(TARGET_COMMON_OUT_ROOT)/obj

TARGET_OUT := $(PRODUCT_OUT)/$(TARGET_COPY_OUT_SYSTEM)
ifeq ($(BUILD_MTK_SYSTEM_INTO_RAMDISK),true)
TARGET_OUT := $(PRODUCT_OUT)/root/system
endif
TARGET_OUT_EXECUTABLES:= $(TARGET_OUT)/bin
TARGET_OUT_OPTIONAL_EXECUTABLES:= $(TARGET_OUT)/xbin
TARGET_OUT_SHARED_LIBRARIES:= $(TARGET_OUT)/lib
TARGET_OUT_JAVA_LIBRARIES:= $(TARGET_OUT)/framework
TARGET_OUT_APPS:= $(TARGET_OUT)/app
TARGET_OUT_APPS_PRIVILEGED := $(TARGET_OUT)/priv-app
TARGET_OUT_KEYLAYOUT := $(TARGET_OUT)/usr/keylayout
TARGET_OUT_KEYCHARS := $(TARGET_OUT)/usr/keychars
TARGET_OUT_ETC := $(TARGET_OUT)/etc
TARGET_OUT_NOTICE_FILES:=$(TARGET_OUT_INTERMEDIATES)/NOTICE_FILES
TARGET_OUT_FAKE := $(PRODUCT_OUT)/fake_packages

ifeq ($(LENOVO_ONE_IMAGE_SUPPORT),yes)
TARGET_OUT_FAC := $(PRODUCT_OUT)/fac
endif
TARGET_OUT_DATA := $(PRODUCT_OUT)/$(TARGET_COPY_OUT_DATA)
TARGET_OUT_DATA_EXECUTABLES:= $(TARGET_OUT_EXECUTABLES)
TARGET_OUT_DATA_SHARED_LIBRARIES:= $(TARGET_OUT_SHARED_LIBRARIES)
TARGET_OUT_DATA_JAVA_LIBRARIES:= $(TARGET_OUT_DATA)/framework
TARGET_OUT_DATA_APPS:= $(TARGET_OUT_DATA)/app
TARGET_OUT_DATA_KEYLAYOUT := $(TARGET_OUT_KEYLAYOUT)
TARGET_OUT_DATA_KEYCHARS := $(TARGET_OUT_KEYCHARS)
TARGET_OUT_DATA_ETC := $(TARGET_OUT_ETC)
TARGET_OUT_DATA_NATIVE_TESTS := $(TARGET_OUT_DATA)/nativetest
TARGET_OUT_DATA_FAKE := $(TARGET_OUT_DATA)/fake_packages

TARGET_OUT_CACHE := $(PRODUCT_OUT)/cache

TARGET_OUT_VENDOR := $(PRODUCT_OUT)/$(TARGET_COPY_OUT_VENDOR)
TARGET_OUT_VENDOR_EXECUTABLES:= $(TARGET_OUT_VENDOR)/bin
TARGET_OUT_VENDOR_OPTIONAL_EXECUTABLES:= $(TARGET_OUT_VENDOR)/xbin
TARGET_OUT_VENDOR_SHARED_LIBRARIES:= $(TARGET_OUT_VENDOR)/lib
TARGET_OUT_VENDOR_JAVA_LIBRARIES:= $(TARGET_OUT_VENDOR)/framework
TARGET_OUT_VENDOR_APPS:= $(TARGET_OUT_VENDOR)/app
TARGET_OUT_VENDOR_ETC := $(TARGET_OUT_VENDOR)/etc

TARGET_OUT_UNSTRIPPED := $(PRODUCT_OUT)/symbols
TARGET_OUT_EXECUTABLES_UNSTRIPPED := $(TARGET_OUT_UNSTRIPPED)/system/bin
TARGET_OUT_SHARED_LIBRARIES_UNSTRIPPED := $(TARGET_OUT_UNSTRIPPED)/system/lib
TARGET_ROOT_OUT_UNSTRIPPED := $(TARGET_OUT_UNSTRIPPED)
TARGET_ROOT_OUT_SBIN_UNSTRIPPED := $(TARGET_OUT_UNSTRIPPED)/sbin
TARGET_ROOT_OUT_BIN_UNSTRIPPED := $(TARGET_OUT_UNSTRIPPED)/bin
#begin 
#specify custom install folder
TARGET_CUSTOM_OUT := $(PRODUCT_OUT)/custom
#end
ifeq ($(LENOVO_ONE_IMAGE_SUPPORT),yes)
TARGET_FAC_OUT := $(PRODUCT_OUT)/fac
endif

TARGET_ROOT_OUT := $(PRODUCT_OUT)/$(TARGET_COPY_OUT_ROOT)
TARGET_ROOT_OUT_BIN := $(TARGET_ROOT_OUT)/bin
TARGET_ROOT_OUT_SBIN := $(TARGET_ROOT_OUT)/sbin
TARGET_ROOT_OUT_ETC := $(TARGET_ROOT_OUT)/etc
TARGET_ROOT_OUT_USR := $(TARGET_ROOT_OUT)/usr

TARGET_RECOVERY_OUT := $(PRODUCT_OUT)/$(TARGET_COPY_OUT_RECOVERY)
TARGET_RECOVERY_ROOT_OUT := $(TARGET_RECOVERY_OUT)/root

# Infinity {
TARGET_FACTORY_OUT := $(PRODUCT_OUT)/factory
TARGET_FACTORY_ROOT_OUT := $(TARGET_FACTORY_OUT)/root
# Infinity }

TARGET_SYSLOADER_OUT := $(PRODUCT_OUT)/sysloader
TARGET_SYSLOADER_ROOT_OUT := $(TARGET_SYSLOADER_OUT)/root
TARGET_SYSLOADER_SYSTEM_OUT := $(TARGET_SYSLOADER_OUT)/root/system

TARGET_INSTALLER_OUT := $(PRODUCT_OUT)/installer
TARGET_INSTALLER_DATA_OUT := $(TARGET_INSTALLER_OUT)/data
TARGET_INSTALLER_ROOT_OUT := $(TARGET_INSTALLER_OUT)/root
TARGET_INSTALLER_SYSTEM_OUT := $(TARGET_INSTALLER_OUT)/root/system

TARGET_FACTORY_RAMDISK_OUT := $(PRODUCT_OUT)/factory_ramdisk

COMMON_MODULE_CLASSES := TARGET-NOTICE_FILES HOST-NOTICE_FILES HOST-JAVA_LIBRARIES

ifeq (,$(strip $(DIST_DIR)))
  DIST_DIR := $(OUT_DIR)/dist
endif

ifeq ($(PRINT_BUILD_CONFIG),)
PRINT_BUILD_CONFIG := true
endif

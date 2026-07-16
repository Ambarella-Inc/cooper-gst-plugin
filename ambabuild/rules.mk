###############################################################################
## rules.mk
##
## History:
##    2023/12/01 - [Yang Yu] created file
##
## Copyright (C) 2022 Ambarella International LP
##
## This library is free software; you can redistribute it and/or
## modify it under the terms of the GNU Library General Public
## License as published by the Free Software Foundation; either
## version 2 of the License, or (at your option) any later version.
##
## This library is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## Library General Public License for more details.
##
## You should have received a copy of the GNU Library General Public
## License along with this library; if not, write to the
## Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
## Boston, MA 02110-1301, USA.
##
##################################################################################

include $(TOP_DIR)/ambabuild/config.mk


######################################################################
# Component defined variables
######################################################################
# for modules of apollo
SUBMODULES       ?=
# If Component has sub directories
SUBDIRS          ?=
# unit test dir
UNIT_TEST_DIR    ?=
# If component generates shared libraries
SHARED_LIB_NAMES ?=
# If component generates shared libraries for test only
COM_SHARED_LIB_NAMES ?=
# If component generates static libraries
STATIC_LIB_NAMES ?=
# If component generates executables
EXECUTABLE_FILES ?=
# Component's source files
COMPONENT_SRC    ?=
# Component's object files
COMPONENT_OBJ    ?=
# Component's specific includes
COMPONENT_INC    ?=
# Component's specific defines
COMPONENT_DEF    ?=
# Component's specific ld flags
COMPONENT_LDFLAG ?=
# CyberRT's specific ld flags
CYBER_LDFLAG ?=
# AMBACV lib specific ld flags
AMBACV_LDFLAG ?=
# lib version, defined by xx_version.cc
VER_MAJOR          ?=
VER_MINOR          ?=
VER_PATCH          ?=
VER_STRING         ?=
VER_STRING_SHORT   ?=

MKDIR = mkdir -p
RM = rm -rf
CP = cp -a
MV = mv
LN = ln -sf

INSTALL = install

ifeq ($(ENABLE_LYCHEE), y)
## use cflags in lychee
C_OPT_FLAGS := -fPIC $(shell rpm --eval %optflags)
#C_OPT_FLAGS := -fPIC -fasynchronous-unwind-tables -fdiagnostics-color=auto \
#    -mcpu=native -march=native -mtune=native \
#	-fstack-protector-strong -fstack-clash-protection
else
C_OPT_FLAGS := -fPIC -fasynchronous-unwind-tables -fdiagnostics-color=auto \
    -march=armv8-a+crypto -mlittle-endian -mcpu=cortex-a76+crypto -march=armv8.2-a+crypto+fp16+rcpc+dotprod \
    -mtune=cortex-a76 -fstack-protector-strong -fstack-clash-protection #-fsanitize=thread -fPIE
endif


CXX_ADDITIONAL_FLAGS := -fstrict-aliasing -Wstrict-aliasing -Wno-deprecated-declarations -fopenmp

CFLAGS   = $(C_OPT_FLAGS) $(COMPILE_FLAGS) $(COMPONENT_INC) $(COMPONENT_DEF)
CPPFLAGS = $(C_OPT_FLAGS) $(COMPILE_FLAGS) $(COMPONENT_INC) $(COMPONENT_DEF) $(CXX_ADDITIONAL_FLAGS)
LDFLAGS  = $(LINK_FLAGS) $(COMPONENT_LDFLAG) #-ltsan

TMPDIR := $(OBJ_DIR)/$(word 2, $(subst /$(UNIQUE_NAME_TAG)/, ,$(shell pwd)))

#for auto make
SUBMODULES_ALL = $(addprefix all_, $(SUBMODULES))
SUBDIRS_ALL   = $(addprefix all_, $(SUBDIRS))
OBJECTS_ALL   = $(addprefix $(TMPDIR)/, $(COMPONENT_OBJ))
UNIT_TEST_ALL   = $(addprefix all_, $(UNIT_TEST_DIR))

#for auto clean
SUBMODULES_CLEAN = $(addprefix clean_, $(SUBMODULES))
SUBDIRS_CLEAN = $(addprefix clean_, $(SUBDIRS))
UNIT_TEST_CLEAN   = $(addprefix clean_, $(UNIT_TEST_DIR))

#for auto install
SUBMODULES_INSTALL = $(addprefix install_, $(SUBMODULES))

.PHONY: $(SUBMODULES_ALL) $(SUBDIRS_ALL) $(UNIT_TEST_ALL) all cyber
.PHONY: $(SHARED_LIB_NAMES) $(STATIC_LIB_NAMES) $(EXECUTABLE_FILES)
.PHONY: $(SUBMODULES_CLEAN) $(SUBDIRS_CLEAN) $(UNIT_TEST_CLEAN) clean

.PHONY: $(addprefix clean_, $(SHARED_LIB_NAMES)) \
	$(addprefix clean_, $(STATIC_LIB_NAMES)) \
	$(addprefix clean_, $(EXECUTABLE_FILES))

######################################################################
# all
######################################################################
all: tmpdir $(SUBMODULES_ALL) $(SUBDIRS_ALL) $(UNIT_TEST_ALL) $(OBJECTS_ALL) $(STATIC_LIB_NAMES) \
	 $(SHARED_LIB_NAMES) $(COM_SHARED_LIB_NAMES) $(AMBA_COM_SHARED_LIB_NAMES) $(EXECUTABLE_FILES)

tmpdir:
	@$(MKDIR) $(TMPDIR)

ifneq ($(strip $(SUBMODULES_ALL)),)
$(SUBMODULES_ALL):
	@echo "    [Build modules/$(subst all_,,$@)]:"
	@$(MAKE) -C modules/$(subst all_,,$@) default --no-print-directory
	@$(MAKE) -C modules/$(subst all_,,$@) all_self_tests --no-print-directory
endif

ifneq ($(strip $(SUBDIRS_ALL)),)
$(SUBDIRS_ALL):
	@echo "    [Build $(subst all_,,$@)]:"
	@$(MAKE) -C $(subst all_,,$@) default --no-print-directory
endif

ifneq ($(strip $(UNIT_TEST_ALL)),)
$(UNIT_TEST_ALL): $(SUBDIRS_ALL)
	@echo "    [Build $(subst all_,,$@)]:"
	@$(MAKE) -C $(subst all_,,$@) default --no-print-directory
endif

ifneq ($(strip $(SHARED_LIB_NAMES)),)
$(SHARED_LIB_NAMES): \
	$(SUBDIRS_ALL) $(OBJECTS_ALL)
#	$(foreach n, $(addsuffix _obj, $(SHARED_LIB_NAMES)), \
#		$(addprefix $(TMPDIR)/, $($n)))
	@$(MKDIR) $(INTERNAL_LIB_DIR)/
	@$(CXX) -o $(INTERNAL_LIB_DIR)/lib$@.so$(VER_STRING) \
		$(addprefix $(TMPDIR)/, $($@_obj)) \
		-shared -Wl,-soname,lib$@.so$(VER_STRING_SHORT) $(LDFLAGS)
ifneq ($(strip $(VER_STRING)),)
	@cd $(INTERNAL_LIB_DIR) && \
	$(LN) lib$@.so$(VER_STRING) lib$@.so$(VER_STRING_SHORT) && \
	$(LN) lib$@.so$(VER_STRING_SHORT) lib$@.so
endif
#ifneq ($(BUILD_CONFIG_DEBUG), y)
#	@$(STRIP) --strip-unneeded \
#		$(INTERNAL_LIB_DIR)/lib$@.so
#endif
endif

ifneq ($(strip $(COM_SHARED_LIB_NAMES)),)
$(COM_SHARED_LIB_NAMES): \
	$(SUBDIRS_ALL) $(OBJECTS_ALL)
#	$(foreach n, $(addsuffix _obj, $(COM_SHARED_LIB_NAMES)), \
#		$(addprefix $(TMPDIR)/, $($n)))
	@$(MKDIR) $(INTERNAL_COM_LIB_DIR)/
	@$(CXX) -o $(INTERNAL_COM_LIB_DIR)/lib$@.so$(VER_STRING) \
		$(addprefix $(TMPDIR)/, $($@_obj)) \
		-shared -Wl,-soname,lib$@.so$(VER_STRING_SHORT) $(LDFLAGS)
ifneq ($(strip $(VER_STRING)),)
	@cd $(INTERNAL_COM_LIB_DIR) && \
	$(LN) lib$@.so$(VER_STRING) lib$@.so$(VER_STRING_SHORT) && \
	$(LN) lib$@.so$(VER_STRING_SHORT) lib$@.so
endif
#ifneq ($(BUILD_CONFIG_DEBUG), y)
#	@$(STRIP) --strip-unneeded \
#		$(INTERNAL_COM_LIB_DIR)/lib$@.so
#endif
endif

ifneq ($(strip $(AMBA_COM_SHARED_LIB_NAMES)),)
$(AMBA_COM_SHARED_LIB_NAMES): \
	$(SUBDIRS_ALL) $(OBJECTS_ALL)
#	$(foreach n, $(addsuffix _obj, $(AMBA_COM_SHARED_LIB_NAMES)), \
#		$(addprefix $(TMPDIR)/, $($n)))
	@$(MKDIR) $(INTERNAL_COM_LIB_DIR)/
	@$(CXX) -o $(INTERNAL_COM_LIB_DIR)/lib$@.so$(VER_STRING) \
		$(addprefix $(TMPDIR)/, $($@_obj)) \
		-shared -Wl,-soname,lib$@.so$(VER_STRING_SHORT) $(LDFLAGS) $(AMBACV_LDFLAG)
ifneq ($(strip $(VER_STRING)),)
	@cd $(INTERNAL_COM_LIB_DIR) && \
	$(LN) lib$@.so$(VER_STRING) lib$@.so$(VER_STRING_SHORT) && \
	$(LN) lib$@.so$(VER_STRING_SHORT) lib$@.so
endif
#ifneq ($(BUILD_CONFIG_DEBUG), y)
#	@$(STRIP) --strip-unneeded \
#		$(INTERNAL_COM_LIB_DIR)/lib$@.so
#endif
endif

ifneq ($(strip $(STATIC_LIB_NAMES)),)
$(STATIC_LIB_NAMES): \
	$(foreach n, $(addsuffix _obj, $(STATIC_LIB_NAMES)), \
		$(addprefix $(TMPDIR)/, $($n)))
	@$(MKDIR) $(INTERNAL_LIB_DIR)/
	@$(AR) rcus $(INTERNAL_LIB_DIR)/lib$@.a \
		$(addprefix $(TMPDIR)/, $($@_obj))
ifneq ($(BUILD_CONFIG_DEBUG), y)
	@$(STRIP) --strip-unneeded \
		$(INTERNAL_LIB_DIR)/lib$@.a
endif
endif

ifneq ($(strip $(STATIC_TAR_NAMES)),)
$(STATIC_TAR_NAMES): \
	$(foreach n, $(addsuffix _obj, $(STATIC_TAR_NAMES)), \
		$(addprefix $(TMPDIR)/, $($n)))
	@$(MKDIR) $(INTERNAL_TAR_DIR)/
	@$(AR) rcu $(INTERNAL_TAR_DIR)/lib$@.a \
		$(addprefix $(TMPDIR)/, $($@_obj))
endif

ifneq ($(strip $(EXECUTABLE_FILES)),)
$(EXECUTABLE_FILES): \
	$(foreach n, $(addsuffix _obj, $(EXECUTABLE_FILES)), \
		$(addprefix $(TMPDIR)/, $($n)))
	@$(MKDIR) $(INTERNAL_BIN_DIR)
	@$(CXX) -o $(INTERNAL_BIN_DIR)/$@ \
		$(addprefix $(TMPDIR)/, $($@_obj)) \
		$(LDFLAGS) $($@_ldflag)
ifneq ($(BUILD_CONFIG_DEBUG), y)
	@$(STRIP) --strip-unneeded $(INTERNAL_BIN_DIR)/$@
endif
endif

######################################################################
# clean
######################################################################
clean: $(SUBMODULES_CLEAN) $(SUBDIRS_CLEAN) $(UNIT_TEST_CLEAN) clean_all \
	$(addprefix clean_, $(SHARED_LIB_NAMES)) \
	$(addprefix clean_, $(STATIC_LIB_NAMES)) \
	$(addprefix clean_, $(EXECUTABLE_FILES))

ifneq ($(strip $(SUBMODULES_CLEAN)),)
$(SUBMODULES_CLEAN):
	@echo "    [Clean modules/$(subst clean_,,$@)]:"
	@$(MAKE) -C modules/$(subst clean_,,$@) clean --no-print-directory
endif

$(SUBDIRS_CLEAN):
	@echo "    [Clean $(subst clean_,,$@)]:"
	@$(MAKE) -C $(subst clean_,,$@) clean --no-print-directory

$(UNIT_TEST_CLEAN):
	@echo "    [Clean $(subst clean_,,$@)]:"
	@$(MAKE) -C $(subst clean_,,$@) clean --no-print-directory

ifneq ($(strip $(SHARED_LIB_NAMES)),)
$(addprefix clean_, $(SHARED_LIB_NAMES)):
	-@$(RM) $(INTERNAL_LIB_DIR)/lib$(word 2, $(subst _, ,$@)).so*
endif

ifneq ($(strip $(STATIC_LIB_NAMES)),)
$(addprefix clean_, $(STATIC_LIB_NAMES)):
	-@$(RM) $(INTERNAL_LIB_DIR)/lib$(word 2, $(subst _, ,$@)).a
endif

ifneq ($(strip $(EXECUTABLE_FILES)),)
$(addprefix clean_, $(EXECUTABLE_FILES)):
	-@$(RM) $(INTERNAL_BIN_DIR)/$(word 2, $(subst _, ,$@))
endif

clean_all:
	-@$(RM) $(TMPDIR)/*.o *.o $(TMPDIR)/*.d .*.d

######################################################################
# install
######################################################################
install: $(SUBMODULES_INSTALL)

ifneq ($(strip $(SUBMODULES_INSTALL)),)
$(SUBMODULES_INSTALL):
	@echo "    [Install modules/$(subst install_,,$@)]:"
	@$(MAKE) -C modules/$(subst install_,,$@) install --no-print-directory
endif

######################################################################
# compile
######################################################################
$(TMPDIR)/%.o: $(shell pwd)/%.cc
	@echo "      compiling $(shell basename $<)..."
	@$(MKDIR) $(dir $@)
	@$(CXX) $(CPPFLAGS) -c -MMD -o $@ $<

$(TMPDIR)/%.o: $(shell pwd)/%.cpp
	@echo "      compiling $(shell basename $<)..."
	@$(MKDIR) $(dir $@)
	@$(CXX) $(CPPFLAGS) -c -MMD -o $@ $<

$(TMPDIR)/%.o: $(shell pwd)/%.c
	@echo "      compiling $(shell basename $<)..."
	@$(GCC) $(CFLAGS) -c -MMD -o $@ $<

$(TMPDIR)/%.o: $(shell pwd)/%.S
	@echo "      compiling $(shell basename $<)..."
	@$(GCC) $(CFLAGS) -c -MMD -o $@ $<

-include $(addprefix $(TMPDIR)/, $(COMPONENT_OBJ:.o=.d))

###############################################################################
## rules.mk
##
## History:
##   2022/10/11 - [Zhi He] created file
##
## Copyright (c) 2022 Ambarella International LP
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
##
##################################################################################


######################################################################
# Component defined variables
######################################################################

# If Component has sub directories
SUBDIRS          ?=
# unit test dir
UNIT_TEST_DIR    ?=
# If component generates shared libraries
SHARED_LIB_NAMES ?=
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

MKDIR = mkdir -p
RM = rm -rf
CP = rsync -a
MV = mv
LN = ln

C_OPT_FLAGS := -fPIC -fasynchronous-unwind-tables -O3 -fdiagnostics-color=auto \
    -march=armv8-a+crypto -mlittle-endian -mcpu=cortex-a53+crypto \
    --param l1-cache-line-size=64 --param l1-cache-size=32 \
	-Wp,-D_FORTIFY_SOURCE=2 -fstack-protector-strong -fstack-clash-protection

CXX_ADDITIONAL_FLAGS := -fstrict-aliasing -Wstrict-aliasing -Wno-deprecated-declarations

CFLAGS   = $(C_OPT_FLAGS) $(COMPILE_FLAGS) $(COMPONENT_INC) $(COMPONENT_DEF)
CPPFLAGS = $(C_OPT_FLAGS) $(COMPILE_FLAGS) $(COMPONENT_INC) $(COMPONENT_DEF) $(CXX_ADDITIONAL_FLAGS)
LDFLAGS  = $(LINK_FLAGS) $(COMPONENT_LDFLAG)

TMPDIR := $(OBJ_DIR)/$(word 2, $(subst /$(UNIQUE_NAME_TAG)/, ,$(shell pwd)))

SUBDIRS_ALL   = $(addprefix all_, $(SUBDIRS))
OBJECTS_ALL   = $(addprefix $(TMPDIR)/, $(COMPONENT_OBJ))
SUBDIRS_CLEAN = $(addprefix clean_, $(SUBDIRS))

UNIT_TEST_ALL   = $(addprefix all_, $(UNIT_TEST_DIR))
UNIT_TEST_CLEAN   = $(addprefix clean_, $(UNIT_TEST_DIR))

.PHONY: $(SUBDIRS_ALL) $(UNIT_TEST_ALL) all
.PHONY: $(SHARED_LIB_NAMES) $(STATIC_LIB_NAMES) $(EXECUTABLE_FILES)
.PHONY: $(SUBDIRS_CLEAN) $(UNIT_TEST_CLEAN) clean
.PHONY: $(addprefix clean_, $(SHARED_LIB_NAMES)) \
	$(addprefix clean_, $(STATIC_LIB_NAMES)) \
	$(addprefix clean_, $(EXECUTABLE_FILES))

######################################################################
# all
######################################################################
all: tmpdir $(SUBDIRS_ALL) $(UNIT_TEST_ALL) $(OBJECTS_ALL) $(SHARED_LIB_NAMES) \
	$(STATIC_LIB_NAMES) $(EXECUTABLE_FILES)

tmpdir:
	@$(MKDIR) $(TMPDIR)

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
	$(foreach n, $(addsuffix _obj, $(SHARED_LIB_NAMES)), \
		$(addprefix $(TMPDIR)/, $($n)))
	@$(MKDIR) $(LIB_DIR)/
	@$(CXX) -o $(LIB_DIR)/lib$@.so -shared \
		-Wl,-soname,lib$@.so $(addprefix $(TMPDIR)/, $($@_obj)) \
		$(LDFLAGS)
ifneq ($(BUILD_CONFIG_DEBUG), y)
	@$(STRIP) --strip-unneeded \
		$(LIB_DIR)/lib$@.so
endif
endif

ifneq ($(strip $(STATIC_LIB_NAMES)),)
$(STATIC_LIB_NAMES): \
	$(foreach n, $(addsuffix _obj, $(STATIC_LIB_NAMES)), \
		$(addprefix $(TMPDIR)/, $($n)))
	@$(MKDIR) $(LIB_DIR)/
	@$(AR) rcus $(LIB_DIR)/lib$@.a \
		$(addprefix $(TMPDIR)/, $($@_obj))
ifneq ($(BUILD_CONFIG_DEBUG), y)
	@$(STRIP) --strip-unneeded \
		$(LIB_DIR)/lib$@.a
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
	@$(MKDIR) $(BIN_DIR)
	@$(CXX) -o $(BIN_DIR)/$@ \
		$(addprefix $(TMPDIR)/, $($@_obj)) \
		$(LDFLAGS) $($@_ldflag)
ifneq ($(BUILD_CONFIG_DEBUG), y)
	@$(STRIP) --strip-unneeded $(BIN_DIR)/$@
endif
endif

######################################################################
# clean
######################################################################
clean: $(SUBDIRS_CLEAN) $(UNIT_TEST_CLEAN) clean_all \
	$(addprefix clean_, $(SHARED_LIB_NAMES)) \
	$(addprefix clean_, $(STATIC_LIB_NAMES)) \
	$(addprefix clean_, $(EXECUTABLE_FILES))

$(SUBDIRS_CLEAN):
	@echo "    [Clean $(subst clean_,,$@)]:"
	@$(MAKE) -C $(subst clean_,,$@) clean --no-print-directory

$(UNIT_TEST_CLEAN):
	@echo "    [Clean $(subst clean_,,$@)]:"
	@$(MAKE) -C $(subst clean_,,$@) clean --no-print-directory

ifneq ($(strip $(SHARED_LIB_NAMES)),)
$(addprefix clean_, $(SHARED_LIB_NAMES)):
	-@$(RM) $(LIB_DIR)/lib$(word 2, $(subst _, ,$@)).so*
endif

ifneq ($(strip $(STATIC_LIB_NAMES)),)
$(addprefix clean_, $(STATIC_LIB_NAMES)):
	-@$(RM) $(LIB_DIR)/lib$(word 2, $(subst _, ,$@)).a
endif

ifneq ($(strip $(EXECUTABLE_FILES)),)
$(addprefix clean_, $(EXECUTABLE_FILES)):
	-@$(RM) $(BIN_DIR)/$(word 2, $(subst _, ,$@))
endif

clean_all:
	-@$(RM) $(TMPDIR)/*.o *.o $(TMPDIR)/*.d .*.d

######################################################################
# compile
######################################################################
$(TMPDIR)/%.o: $(shell pwd)/%.cc
	@echo "      compiling $(shell basename $<)..."
	@$(CXX) $(CPPFLAGS) -c -MMD -o $@ $<

$(TMPDIR)/%.o: $(shell pwd)/%.c
	@echo "      compiling $(shell basename $<)..."
	@$(GCC) $(CFLAGS) -c -MMD -o $@ $<

$(TMPDIR)/%.o: $(shell pwd)/%.S
	@echo "      compiling $(shell basename $<)..."
	@$(GCC) $(CFLAGS) -c -MMD -o $@ $<

-include $(addprefix $(TMPDIR)/, $(COMPONENT_OBJ:.o=.d))


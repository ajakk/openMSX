# $Id$
# Defines the building blocks of openMSX and their dependencies.

ifneq ($(PROBE_MAKE_INCLUDED),true)
$(error Include probe results before including "components.mk")
endif

CORE_LIBS:=PNG SDL SDL_IMAGE TCL XML ZLIB
ifneq ($(filter x,$(foreach LIB,$(CORE_LIBS),x$(HAVE_$(LIB)_LIB))),)
COMPONENT_CORE:=false
endif
ifneq ($(filter x,$(foreach LIB,$(CORE_LIBS),x$(HAVE_$(LIB)_H))),)
COMPONENT_CORE:=false
endif
COMPONENT_CORE?=true

ifeq ($(HAVE_GL_LIB),)
COMPONENT_GL:=false
endif
ifeq ($(HAVE_GL_H),)
ifeq ($(HAVE_GL_GL_H),)
COMPONENT_GL:=false
endif
endif
COMPONENT_GL?=true


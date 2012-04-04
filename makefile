##
## PIN tools
##

##############################################################
#
# Here are some things you might want to configure
#
##############################################################

LIBS = -lnuma


ifdef COMPRESS_STREAM
	LIBS += -lboost_iostreams
	CXXFLAGS += -DCOMPRESS_STREAM
endif

TARGET_COMPILER?=gnu
ifdef OS
    ifeq (${OS},Windows_NT)
        TARGET_COMPILER=ms
    endif
endif

##############################################################
#
# include *.config files
#
##############################################################

ifeq ($(TARGET_COMPILER),gnu)
    include ../makefile.gnu.config
    CXXFLAGS ?= -O3 -Wall -Wno-unknown-pragmas $(DBG) $(OPT) -MMD
	BIGBINARYFLAGS ?= 
endif

SPECIALRUN = 
NORMALRUN = numatrace directMemRatio
TOOL_ROOTS = $(SPECIALRUN) $(NORMALRUN)

TOOLS = $(TOOL_ROOTS:%=$(OBJDIR)%$(PINTOOL_SUFFIX))

SANITY_TOOLS = 

all: tools pageReadWriteSummary
tools: $(OBJDIR) $(TOOLS) 
test: $(OBJDIR) $(TOOL_ROOTS:%=%.test)
#tests-sanity: $(OBJDIR) $(SANITY_TOOLS:%=%.test)

## special testing rules

## analysis tools

%: %.cpp
	$(CXX) $(CXXFLAGS) -std=c++0x -o $@ $<

## build rules
$(OBJDIR):
	mkdir -p $(OBJDIR)


$(OBJDIR)%.o : %.cpp 
	$(CXX) ${COPT} $(CXXFLAGS) $(PIN_CXXFLAGS) ${OUTOPT}$@ $<
$(TOOLS): $(PIN_LIBNAMES)
$(TOOLS): %$(PINTOOL_SUFFIX) : %.o
	${PIN_LD} $(PIN_LDFLAGS) $(LINK_DEBUG) ${LINK_OUT}$@ $< ${PIN_LPATHS} $(LIBS) $(PIN_LIBS) $(DBG)

## cleaning
clean:
	-rm -rf $(OBJDIR) *.out *.tested *.failed

tidy:
	-rm -rf $(OBJDIR) *.tested *.failed

-include *.d

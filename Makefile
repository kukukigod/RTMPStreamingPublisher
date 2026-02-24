#Makefile wrote by YC
PLATFORM_INFO=./platform.info

SRC_DIR     = $(shell /bin/pwd)
TOP_DIR     = $(SRC_DIR)
ifeq ("$(PLATFORM)", "x86")
PLATFORM_NUM=0x86
else ifeq ("$(PLATFORM)", "qcs610")
PLATFORM_NUM=0x610
else ifeq ("$(PLATFORM)", "")
PLATFORM=x86
PLATFORM_NUM=0x86
endif
include $(TOP_DIR)/makecfg.$(PLATFORM)

#copy for config.mk
SDK_ROOT=./
SDK_SAMPLE_PATH=$(SDK_ROOT)

CPP=$(CXX)
AR=$(CROSS_COMPILE)ar
STRIP=$(CROSS_COMPILE)strip

PROJ_DIR = $(SDK_ROOT)
IFLAGS += -I. \
	-I$(SRC_DIR)/include \
	-I$(SRC_DIR)/include/nlohmann \
	-I$(PKG_CONFIG_SYSROOT_DIR)/usr/include/gstreamer-1.0 \
    -I$(PKG_CONFIG_SYSROOT_DIR)/usr/include/gstreamer-1.0/gst \
    -I$(PKG_CONFIG_SYSROOT_DIR)/usr/include/glib-2.0 \
    -I$(PKG_CONFIG_SYSROOT_DIR)/usr/lib/glib-2.0/include/ \


LIB_PATH += $(LFLAGS) -L./../CommonSrc -L./lib/$(PLATFORM)
SHARED_LIBS += -lmosquitto -lpthread
SHARED_LIBS += -lgstapp-1.0 -lavformat -lavcodec -lavutil -lz -lm -lpthread
CFLAGS+=
USE_MEMWATCH=n

export LANG=C

# target source
C_SRCS = $(wildcard *.c)
CPP_SRCS = $(wildcard *.cpp)
PLATFORM_SRC = $(notdir $(wildcard ./RecHandlerSrc/$(PLATFORM)/*.cpp))
CPP_SRCS += $(PLATFORM_SRC)
CPP_SRCS += $(notdir $(wildcard ./muxer/*.cpp))
DFLAGS+=-DPLATFORM_NUM=$(PLATFORM_NUM)
TARGET := RtmpPublisher

.PHONY : x86 clean install

#default Makefile compiler variable
VPATH=./RecHandlerSrc/$(PLATFORM)/:./muxer/
#################################

OBJ_PATH=$(SRC_DIR)/obj
C_OBJS :=$(C_SRCS:%.c=$(OBJ_PATH)/%.o)
CPP_OBJS :=$(CPP_SRCS:%.cpp=$(OBJ_PATH)/%.o)
OBJS= $(C_OBJS) $(CPP_OBJS)

$(OBJ_PATH)/%.o : %.c
	@echo compiler $(notdir $<)
	@[ -e $(OBJ_PATH) ] || mkdir -p $(OBJ_PATH)
	@$(CC) -c $(CFLAGS) $(IFLAGS) $(DFLAGS) -o $@ $<

$(OBJ_PATH)/%.o : %.cpp
	@echo compiler $(notdir $<)
	@[ -e $(OBJ_PATH) ] || mkdir -p $(OBJ_PATH)
	@$(CXX) -std=c++17 -c $(CPPFLAGS) $(CFLAGS) -std=c++17 $(IFLAGS) $(DFLAGS) -o $@ $<

$(TARGET): $(OBJS)
	@echo CPP_SRC=$(CPP_SRCS)
	@$(CXX) $(DFLAGS) $(CFLAGS) $(CPPFLAGS) -o $(TARGET)_$(PLATFORM) $(OBJS) $(LIB_PATH) -Wl,-Bdynamic $(SHARED_LIBS) -O2

x86 qcs610:
	@echo $@ > $(PLATFORM_INFO)
	PLATFORM=$@ make $(TARGET) $(MFLAGS)

install:
	cp -rf $(TARGET)_$(PLATFORM) /SharedFolder/$(TARGET)

clean:
	@rm -rf $(TARGET)_* $(OBJS)
	@rm -rf $(OBJ_PATH)
	@rm -rf $(PLATFORM_INFO)

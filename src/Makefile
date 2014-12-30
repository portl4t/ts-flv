.SUFFIXES: .cc .o .lo

COMPILE = $(CXX) -Wall -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -g -O
INC_PATH =
LIB_PATH = -L/usr/local/lib

SHARED_OBJS = flv.lo flv_common.lo

ALL_OBJS = $(SHARED_OBJS)

ALL_PRGS = 
ALL_LIBS = libtsflv.so

all: $(ALL_OBJS) $(ALL_PRGS) $(ALL_LIBS)

libtsflv.so:
	$(COMPILE) -o $@ $< -shared $(SHARED_OBJS) $(LIB_PATH)
.cc:
	$(COMPILE) -o $@ $< $(SHARED_OBJS) $(LIB_PATH) $(INC_PATH)
.cc.o:
	$(COMPILE) -c -o $@ $<  $(INC_PATH)
.cc.lo:
	$(COMPILE) -c -fPIC -o $@ $<  $(INC_PATH)
install:
	/bin/cp -f $(ALL_LIBS) /usr/lib64/trafficserver/plugins/
clean:
	rm -rf $(ALL_OBJS) $(ALL_PRGS) $(ALL_LIBS) build


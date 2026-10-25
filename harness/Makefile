# Desktop harness: the patched UQM tree + port3ds/ built for Linux with
# -D__3DS__ against harness/3ds.h, so the port's screen layout, input mapping,
# threads and text entry can be exercised before touching hardware.
#   make -C harness UQM=../uqm-0.8.0      -> harness/uqm3ds-harness
UQM   ?= ../uqm-0.8.0
PORT  := ../port3ds
include $(PORT)/sources.mk

OBJDIR := obj
SRCS   := $(addprefix $(UQM)/src/,$(UQM_SOURCES)) $(PORT)/port3ds.c $(PORT)/threads3ds.c \
          $(PORT)/context3ds.c fake3ds.c
OBJS   := $(patsubst %.c,$(OBJDIR)/%.o,$(subst ../,,$(SRCS)))

CFLAGS := -O2 -g -w -D__3DS__ -DUQM3DS_HARNESS -DNDEBUG -DGFXMODULE_SDL -DSDL_DIR=SDL \
          -DUSE_INTERNAL_MIKMOD -DHAVE_ZIP=1 -DTHREADLIB_SDL \
          -I. -I$(PORT) -I$(UQM)/src -I$(UQM)/src/regex $(shell sdl-config --cflags)
LIBS   := $(shell sdl-config --libs) -lvorbisfile -lvorbis -logg -lpng -lz -lm -lpthread

uqm3ds-harness: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

$(OBJDIR)/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) uqm3ds-harness

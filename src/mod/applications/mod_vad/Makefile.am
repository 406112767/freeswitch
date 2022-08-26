include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_vad

mod_LTLIBRARIES = mod_vad.la
mod_vad_la_SOURCES  = mod_vad.c
mod_vad_la_CFLAGS   = $(AM_CFLAGS)
mod_vad_la_CPPFLAGS = $(AM_CPPFLAGS)
mod_vad_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_vad_la_LDFLAGS  = -avoid-version -module -no-undefined -shared

# 当简单到包含fvad时，需要在CFLAGS里指定-DSWITCH_HAVE_FVAD，这样.c文件中的#ifdef才会生效，同时映入fvad的方法
if HAVE_FVAD
mod_vad_la_CFLAGS += -DSWITCH_HAVE_FVAD $(LIBFVAD_CFLAGS) $(FVAD_CFLAGS)
mod_vad_la_LIBADD += $(FVAD_LIBS)
endif


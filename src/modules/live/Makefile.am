AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux \
	-I$(top_srcdir)/src/modules/kvs

libfluxdir = $(libdir)/flux
fluxmoddir = $(libfluxdir)/modules

#
# Comms module
#
fluxmod_LTLIBRARIES = live.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -module -avoid-version -Wl,--version-script=../module.map

live_la_SOURCES = live.c
live_la_LDFLAGS = $(fluxmod_ldflags)
live_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
libflux_LTLIBRARIES = \
	libflux-live.la

libflux_live_la_SOURCES = \
	liblive.c

libflux_live_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

libflux_live_la_LDFLAGS = -Wl,--version-script=flux_live_version.map
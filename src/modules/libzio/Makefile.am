AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxlib_LTLIBRARIES = \
	libzio.la

libzio_la_SOURCES = \
	zio.c \
	zio.h

check_PROGRAMS = \
	flux-zio-test

libzio_deps = \
	$(top_builddir)/src/modules/libzio/libzio.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la

LDADD = $(libzio_deps) \
	$(JSON_LIBS) $(ZMQ_LIBS) $(LIBMUNGE) \
	$(LIBPTHREAD) $(LIBDL)

$(libzio_deps):
	@(cd `dirname $@` && $(MAKE) `basename $@`)

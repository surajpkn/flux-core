MAN1_FILES = $(MAN1_FILES_PRIMARY) $(MAN1_FILES_SECONDARY)


MAN1_FILES_PRIMARY = \
	flux.1 \
	flux-broker.1 \
	flux-kvs.1 \
	flux-keygen.1 \
	flux-logger.1 \
	flux-up.1 \
	flux-topo.1 \
	flux-snoop.1 \
	flux-ping.1 \
	flux-wreckrun.1 \
	flux-start.1 \
	flux-config.1 \
	flux-module.1 \
	flux-exec.1 \
	flux-env.1 \
	flux-list-instances.1 \
	flux-ps.1 \
	flux-getattr.1 \
	flux-dmesg.1

# These files are generated as roff .so includes of a primary page.
# A2X handles this automatically if mentioned in NAME section
MAN1_FILES_SECONDARY = \
	flux-setattr.1 \
	flux-lsattr.1


ADOC_FILES  = $(MAN1_FILES_PRIMARY:%.1=%.adoc)
XML_FILES   = $(MAN1_FILES_PRIMARY:%.1=%.xml)

if HAVE_A2X
dist_man_MANS = $(MAN1_FILES)
$(MAN1_FILES): COPYRIGHT.adoc
endif

SUFFIXES = .adoc .1

flux-setattr.1: flux-getattr.1
flux-lsattr.1: flux-getattr.1

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = 2>/dev/null

.adoc.1:
	$(AM_V_GEN)$(A2X) --attribute mansource=$(META_NAME) \
	    --attribute manversion=$(META_VERSION) \
	    --attribute manmanual="Flux Command Reference" \
	    --destination-dir=$(builddir) \
	    --doctype manpage --format manpage $< $(STDERR_DEVNULL)

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc

CLEANFILES = $(MAN1_FILES) $(XML_FILES)

#
# Makefile for a Video Disk Recorder plugin
#
# $Id$

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.

PLUGIN = vompserver

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' $(PLUGIN).c | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The directory environment:

# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell pkg-config --variable=$(1) vdr || pkg-config --variable=$(1) ../../../vdr.pc))
LIBDIR = $(DESTDIR)$(call PKGCFG,libdir)
LOCDIR = $(DESTDIR)$(call PKGCFG,locdir)
PLGCFG = $(call PKGCFG,plgcfg)
#
TMPDIR ?= /tmp

### The compiler options:

export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

### The version number of VDR's plugin API:

APIVERSION = $(call PKGCFG,apiversion)

### Allow user defined options to overwrite defaults:

-include $(PLGCFG)

# VOMP-INSERT
-include .standalone
# END-VOMP-INSERT

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:

SOFILE = libvdr-$(PLUGIN).so

### Includes and Defines (add further entries here):

INCLUDES +=

DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"'

# VOMP-INSERT
DEFINES += -DVOMPSERVER
# END-VOMP-INSERT

### The object files (add further files here):

OBJS = $(PLUGIN).o

# VOMP-INSERT
OBJS += dsock.o mvpserver.o udpreplier.o bootpd.o tftpd.o i18n.o vompclient.o tcp.o \
                   ringbuffer.o mvprelay.o vompclientrrproc.o \
                   config.o log.o thread.o tftpclient.o \
                   media.o responsepacket.o \
                   mediafile.o mediaplayer.o servermediafile.o serialize.o medialauncher.o

OBJS2 = recplayer.o mvpreceiver.o
# END-VOMP-INSERT

### The main target:

# VOMP-INSERT
all: allbase $(SOFILE) # i18n
standalone: standalonebase vompserver-standalone
# END-VOMP-INSERT

### Implicit rules:

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

### Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

### Internationalization (I18N):

PODIR     = po
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmo    = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Nmsgs  = $(addprefix $(LOCDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.c)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --package-name=vdr-$(PLUGIN) --package-version=$(VERSION) --msgid-bugs-address='<see README>' -o $@ `ls $^`

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q -N $@ $<
	@touch $@

$(I18Nmsgs): $(LOCDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	install -D -m644 $< $@

.PHONY: i18n
i18n: $(I18Nmo) $(I18Npot)

install-i18n: $(I18Nmsgs)

### Targets:

# rest of file modified for vomp

objectsstandalone: $(OBJS)
objects: $(OBJS) $(OBJS2)

allbase:
	( if [ -f .standalone ] ; then ( rm -f .standalone; make clean ; make objects ) ; else exit 0 ;fi )
standalonebase:
	( if [ ! -f .standalone ] ; then ( make clean; echo "DEFINES+=-DVOMPSTANDALONE" > .standalone; make objectsstandalone ) ; else exit 0 ;fi )


$(SOFILE): objects
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(OBJS) $(OBJS2) -o $@

vompserver-standalone: objectsstandalone
	$(CXX) $(CXXFLAGS) $(OBJS) -lpthread -o $@
	chmod u+x $@

install-lib: $(SOFILE)
	install -D $^ $(LIBDIR)/$^.$(APIVERSION)

install: install-lib install-i18n

dist: $(I18Npo) clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(OBJS2) $(DEPFILE) *.so *.tgz core* *~ .standalone vompserver-standalone

PROG = mos-tools

SCONS_FLAGS=-j 4

# How to install

INSTALL_PROG = install -m 755

# rpm variables

CWP = $(shell pwd)
BIN = $(shell basename $(CWP))

rpmsourcedir = /tmp/$(shell whoami)/rpmbuild

INSTALL_TARGET = /usr/bin

# The rules

all release: 
	scons-3 $(SCONS_FLAGS)
debug: 
	scons-3 $(SCONS_FLAGS) --debug-build

clean:
	scons-3 -c ; scons-3 --debug-build -c ; rm -f *~ source/*~ include/*~

rpm:    clean
	mkdir -p $(rpmsourcedir) ; \
        if [ -a $(PROG).spec ]; \
        then \
          tar -C ../ --exclude .svn \
                   -cf $(rpmsourcedir)/$(PROG).tar $(PROG) ; \
          gzip -f $(rpmsourcedir)/$(PROG).tar ; \
          rpmbuild -ta $(rpmsourcedir)/$(PROG).tar.gz ; \
          rm -f $(rpmsourcedir)/$(LIB).tar.gz ; \
        else \
          echo $(rpmerr); \
        fi;

install:
	mkdir -p $(bindir)
	$(INSTALL_PROG) build/release/mosse $(bindir)
	$(INSTALL_PROG) main/mos_importer.py $(bindir)
	$(INSTALL_PROG) main/mos_factor_loader.py $(bindir)

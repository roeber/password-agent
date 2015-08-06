SUBDIRS=src scripts systemd dracut

.PHONY: all clean install

all clean install:
	for d in $(SUBDIRS); do $(MAKE) -C $$d $@; done

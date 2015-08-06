FILES=module-setup.sh

.PHONY: all clean install

all:

clean:
	rm -f *~

install:
	install -d $(DESTDIR)/usr/lib/dracut/modules.d/88blink1-password-agent
	install -t $(DESTDIR)/usr/lib/dracut/modules.d/88blink1-password-agent $(FILES)

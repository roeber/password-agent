UNITS=password-agent-blink1.path password-agent-blink1.service blink1.path blink1.target

.PHONY: all clean install

all:

clean:
	rm -f *~

install:
	install -d $(DESTDIR)/etc/systemd/system
	install -t $(DESTDIR)/etc/systemd/system -m 0644 $(UNITS)

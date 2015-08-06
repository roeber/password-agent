CC=cc
CFLAGS=-c -Wall
LDFLAGS=-lrt
SOURCES=pa-util.c pa-parent.c pa-child.c
OBJECTS=$(SOURCES:.c=.o)

.PHONY: all clean install

all: password-agent

password-agent: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

$(OBJECTS): pa.h

clean:
	rm -f *~ password-agent $(OBJECTS)

install:
	install -d $(DESTDIR)/usr/libexec/password-agent
	install -t $(DESTDIR)/usr/libexec/password-agent password-agent 

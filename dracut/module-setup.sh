#!/bin/bash

check() {
    return 0
}

depends() {
    echo systemd
    return 0
}

installkernel() {
     instmods hid_thingm uas usb_storage
}

install() {
  inst_multiple \
    /usr/libexec/password-agent/password-agent \
    /usr/libexec/password-agent/blink1 \
    /etc/systemd/system/password-agent-blink1.path \
    /etc/systemd/system/password-agent-blink1.service \
    /etc/systemd/system/blink1.path \
    /etc/systemd/system/blink1.target

  inst_symlink /etc/systemd/system/sysinit.target.wants/password-agent-blink1.path
}

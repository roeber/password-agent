Name:           password-agent-blink1
Version:        1
Release:        1%{?dist}
Summary:        A systemd password agent which will flash a blink(1) and read keys from a usb drive

License:        MPL
Source:         %{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:       systemd dracut

%description


%prep
%setup


%build
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
/etc/systemd/system/blink1.path
/etc/systemd/system/blink1.target
/etc/systemd/system/password-agent-blink1.path
/etc/systemd/system/password-agent-blink1.service
/usr/lib/dracut/modules.d/88blink1-password-agent
/usr/libexec/password-agent

%changelog

%post
systemctl enable password-agent-blink1.path

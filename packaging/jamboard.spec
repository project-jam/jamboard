%global appname    jamboard
%global appversion 4.2

Name:           %{appname}
Version:        %{appversion}
Release:        1%{?dist}
Summary:        A soundboard for streamers

License:        MIT
URL:            https://github.com/project-jam/jamboard
Source0:        %{appname}-%{appversion}-linux-x86_64.tar.gz

BuildArch:      x86_64

Requires:       yt-dlp
Requires:       mesa-libGL
Requires:       libglfw
Requires:       libX11
Requires:       libXrandr
Requires:       libXinerama
Requires:       libXcursor
Requires:       libXi
Requires:       ffmpeg-libs
Requires:       openssl

Recommends:     zenity
Recommends:     pavucontrol

%description
JamBoard is a cross platform soundboard. It plays audio clips to your
speakers and to a virtual microphone so they show up in Discord, OBS or
any other voice chat application.

It includes a text to speech engine based on Microsoft Edge neural
voices, a URL importer that uses yt-dlp, an effects rack, and hardware
routing for speaker, microphone and virtual device.

%prep
%autosetup -n %{appname}-%{appversion}

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/scalable/apps
install -p -m 0755 %{appname} %{buildroot}%{_bindir}/%{appname}
install -p -m 0644 jamboard.desktop %{buildroot}%{_datadir}/applications/%{appname}.desktop
install -p -m 0644 jamboard.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/%{appname}.svg
install -Dm644 LICENSE %{buildroot}%{_datadir}/licenses/%{appname}/LICENSE
install -Dm644 zpix.ttf %{buildroot}%{_datadir}/jamboard/zpix.ttf

%files
%license %{_datadir}/licenses/%{appname}/LICENSE
%{_datadir}/jamboard/zpix.ttf
%{_bindir}/%{appname}
%{_datadir}/applications/%{appname}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{appname}.svg

%changelog
* Wed Oct 21 2026 Project Jam <project-jam@users.noreply.github.com> - 4.2-1
- Initial Linux RPM packaging
- Text to speech now uses the Sec-MS-GEC v2 token

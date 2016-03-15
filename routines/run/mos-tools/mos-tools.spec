%define distnum %(/usr/lib/rpm/redhat/dist.sh --distnum)

%define PACKAGENAME mos-tools
Name:           %{PACKAGENAME}
Version:        16.3.15
Release:        1.el7.fmi
Summary:        Tools for mos
Group:          Applications/System
License:        FMI
URL:            http://www.fmi.fi
Source0: 	%{name}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires:  libfmigrib-devel >= 15.8.21
BuildRequires:  libfmidb-devel >= 16.2.12
BuildRequires:  grib_api-devel >= 1.14.0
BuildRequires:  boost-devel >= 1.55
BuildRequires:  scons
BuildRequires:  libfmidb-devel
BuildRequires:  gcc-c++ >= 4.8.3 
BuildRequires:  libsmartmet-newbase-devel >= 16.2.4
Requires:	libfmidb >= 16.2.12
Requires:       jasper-libs
Requires:       libpqxx
Requires:	grib_api >= 1.14.0
Requires:       libsmartmet-newbase >= 16.2.4
Provides:	mosse

AutoReqProv:	no

%description
mos-tools

%prep
%setup -q -n "%{PACKAGENAME}"

%build
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install bindir=$RPM_BUILD_ROOT/%{_bindir}

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,0755)
%{_bindir}/mosse

%changelog
* Tue Mar 15 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.15-1.fmi
- Fix for ECGLO0100
* Wed Mar 10 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.10-1.fmi
- Another fix for declination
* Wed Mar  9 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.9-1.fmi
- Fix for declination
* Fri Mar  4 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.4-1.fmi
- Remove himan-calculated fields since they are a bit different than what mos needs
* Thu Mar  3 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.3-1.fmi
- Improved logic for fetching data
* Wed Mar  2 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.2-1.fmi
- Geometries ECEUR0100 and ECGLO0100
* Fri Feb 12 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.2.12-1.fmi
- New newbase and fmidb
* Wed Sep  2 2015 Mikko Partio <mikko.partio@fmi.fi> - 15.9.2-1.fmi
- grib_api 1.14
- fmidb api change
* Mon Aug 24 2015 Mikko Partio <mikko.partio@fmi.fi> - 15.8.24-1.fmi
- fmigrib api change
* Thu Aug 13 2015 Mikko Partio <mikko.partio@fmi.fi> - 15.8.13-1.fmi
- Minor tweaks to trace output
* Mon Aug 10 2015 Mikko Partio <mikko.partio@fmi.fi> - 15.8.10-1.fmi
- Initial build

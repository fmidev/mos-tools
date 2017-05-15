%define distnum %(/usr/lib/rpm/redhat/dist.sh --distnum)

%define PACKAGENAME mos-tools
Name:           %{PACKAGENAME}
Version:        17.5.15
Release:        1.el7.fmi
Summary:        Tools for mos
Group:          Applications/System
License:        FMI
URL:            http://www.fmi.fi
Source0: 	%{name}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires:  libfmigrib-devel
BuildRequires:  libfmidb-devel
BuildRequires:  eccodes-devel
BuildRequires:  boost-devel >= 1.55
BuildRequires:  scons
BuildRequires:  libfmidb-devel
BuildRequires:  gcc-c++ >= 4.8.3 
BuildRequires:  smartmet-library-newbase-devel
Requires:	libfmidb >= 17.4.6
Requires:	libfmigrib >= 17.4.6
Requires:       jasper-libs
Requires:       libpqxx
Requires:	eccodes
Requires:       smartmet-library-newbase
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
%{_bindir}/mos_importer.py
%{_bindir}/mos_factor_loader.py

%changelog
* Mon May 15 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.4.15-1.fmi
- New CSV format
* Thu Apr  6 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.4.6-1.fmi
- New newbase, fmigrib
* Thu Mar 16 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.3.16-1.fmi
- Fix previous time step for step 144
* Tue Mar  7 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.3.7-1.fmi
- Preparing next mos version
- Passwords to env variables
* Mon Feb 13 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.2.13-1.fmi
- New newbase
* Mon Jan 23 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.1.23-1.fmi
- Newbase RPM name change
* Thu Dec  1 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.12.1-1.fmi
- Fixed query fetching period id
* Tue Nov  8 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.11.8-1.fmi
- New fmigrib
* Thu Oct 20 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.10.20-1.fmi
- Replacing grib_api with eccodes
* Thu Sep  8 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.9.8-1.fmi
- New release
* Tue Aug 23 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.8.23-1.fmi
- New release
* Mon Aug 15 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.8.15-1.fmi
- New fmigrib
* Mon Jun 13 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.6.13-1.fmi
- New fmigrib
* Mon Jun  6 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.6.6-1.fmi
- New fmidb
* Wed Jun  1 2016 Mikko Aalto <mikko.aalto@fmi.fi> - 16.6.1-1.fmi
- New grib_api 1.15
* Thu May 26 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.5.26-1.fmi
- fmidb header change
* Mon May  9 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.5.9-1.fmi
- New newbase
* Fri Apr 22 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.4.22-1.fmi
- New periods 2&4
* Thu Mar 17 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.17-1.fmi
- Write source value to trace even if weight=0
- Add mos_importer.py
* Tue Mar 15 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.15-1.fmi
- Fix for ECGLO0100
* Thu Mar 10 2016 Mikko Partio <mikko.partio@fmi.fi> - 16.3.10-1.fmi
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

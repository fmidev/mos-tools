%define distnum %(/usr/lib/rpm/redhat/dist.sh --distnum)

%define PACKAGENAME mos-tools
Name:           %{PACKAGENAME}
Version:        21.2.23
Release:        1.el7.fmi
Summary:        Tools for FMI mos
Group:          Applications/System
License:        FMI
URL:            http://www.fmi.fi
Source0: 	%{name}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires:  libfmigrib-devel
BuildRequires:  libfmidb-devel >= 20.8.17
BuildRequires:  eccodes-devel
BuildRequires:  boost169-devel
BuildRequires:  scons
BuildRequires:  libfmidb-devel
BuildRequires:  gcc-c++ >= 4.8.3 
BuildRequires:  smartmet-library-newbase-devel >= 21.2.20
BuildRequires:  smartmet-library-gis-devel
Requires:	libfmidb >= 20.8.17
Requires:	libfmigrib >= 19.9.20
Requires:       jasper-libs
Requires:       libpqxx
Requires:	eccodes
Requires:       smartmet-library-newbase >= 21.2.20
Requires:       smartmet-library-gis
Requires:       smartmet-library-macgyver
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
* Tue Feb 23 2021 Mikko Aalto <mikko.aalto@fmi.fi> - 21.2.23-1.fmi
- New newbase
* Wed Aug 26 2020 Mikko Aalto <mikko.aalto@fmi.fi> - 20.10.14-1.fmi
- Changes in maxt/mint parameter
* Wed Aug 26 2020 Mikko Aalto <mikko.aalto@fmi.fi> - 20.8.26-1.fmi
- py3 changes
* Mon Aug 17 2020 Mikko Partio <mikko.partio@fmi.fi> - 20.8.17-1.fmi
- New fmidb
* Wed Jul  8 2020 Mikko Partio <mikko.partio@fmi.fi> - 20.7.8-1.fmi
- New fmidb
* Mon Apr 20 2020 Mikko Partio <mikko.partio@fmi.fi> - 20.4.20-1.fmi
- boost 1.69
* Mon Apr  6 2020 Mikko Partio <mikko.partio@fmi.fi> - 20.4.6-1.fmi
- fmidb ABI change
* Wed Oct 23 2019 Mikko Partio <mikko.partio@fmi.fi> - 19.10.23-1.fmi
- Support byte_offset&byte_length
* Mon Oct  7 2019 Mikko Partio <mikko.partio@fmi.fi> - 19.10.7-1.fmi
- fmigrib ABI change
* Wed Apr 10 2019 Mikko Partio <mikko.partio@fmi.fi> - 19.4.10-1.fmi
- Fix for T-MEAN-K leadtime 147/153
* Tue Apr  9 2019 Mikko Partio <mikko.partio@fmi.fi> - 19.4.9-1.fmi
- Support fetching data from previous forecast
* Wed Apr 11 2018 Mikko Partio <mikko.partio@fmi.fi> - 18.4.11-1.fmi
- New boost
* Tue Feb 27 2018 Elmeri Nurmi <elmeri.nurmi@fmi.fi> - 18.2.27-1.fmi
- Check DB hostname from env variable
* Tue Feb 20 2018 Mikko Partio <mikko.partio@fmi.fi> - 18.2.20-1.fmi
- fmigrib api change
* Wed Jan 24 2018 Mikko Partio <mikko.partio@fmi.fi> - 18.1.24-1.fmi
- fmigrib api change
* Mon Dec  5 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.12.5-1.fmi
- Adjustment for job distribution
* Mon Dec  4 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.12.4-1.fmi
- Improved job distribution for threads
* Wed Nov 22 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.11.22-1.fmi
- Support TD-K as target parameter
* Wed Oct 25 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.10.25-1.fmi
- New fmigrib
* Tue Aug 29 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.8.29-1.fmi
- New boost, newbase
* Thu Aug  3 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.8.3-1.fmi
- Source database switched from neons to radon
* Mon Jul 31 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.7.31-1.fmi
- TMAX & TMIN name change
* Mon Jun  5 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.6.5-1.fmi
- Support multiple parameter executions
* Tue May 16 2017 Mikko Partio <mikko.partio@fmi.fi> - 17.4.16-1.fmi
- dewpoint parameter name change
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

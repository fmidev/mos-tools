%define distnum %(/usr/lib/rpm/redhat/dist.sh --distnum)

%define PACKAGENAME mos-tools
Name:           %{PACKAGENAME}
Version:        15.8.13
Release:        1%{?dist}.fmi
Summary:        Tools for mos
Group:          Applications/System
License:        FMI
URL:            http://www.fmi.fi
Source0: 	%{name}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires:  libfmigrib-devel >= 15.5.18
BuildRequires:  libfmidb-devel >= 15.8.10
BuildRequires:  grib_api-devel >= 1.13.0
BuildRequires:  boost-devel >= 1.54
BuildRequires:  scons
BuildRequires:  libfmidb-devel
BuildRequires:  gcc-c++ >= 4.8.3 
Requires:	libfmidb >= 15.8.10
Requires:       jasper-libs
Requires:       libpqxx
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
* Thu Aug 13 2015 Mikko Partio <mikko.partio@fmi.fi> - 15.8.13-1.fmi
- Minor tweaks to trace output
* Mon Aug 10 2015 Mikko Partio <mikko.partio@fmi.fi> - 15.8.10-1.fmi
- Initial build
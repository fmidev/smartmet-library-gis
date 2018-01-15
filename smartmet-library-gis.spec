%define DIRNAME gis
%define LIBNAME smartmet-%{DIRNAME}
%define SPECNAME smartmet-library-%{DIRNAME}
Summary: gis library
Name: %{SPECNAME}
Version: 18.1.15
Release: 1%{?dist}.fmi
License: MIT
Group: Development/Libraries
URL: https://github.com/fmidev/smartmet-library-gis
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot-%(%{__id_u} -n)
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: boost-devel
BuildRequires: fmt-devel
BuildRequires: gdal-devel
BuildRequires: geos-devel
BuildRequires: smartmet-library-macgyver-devel >= 17.8.28
Requires: fmt
Requires: gdal >= 1.11.4
Requires: geos >= 3.5.0
Requires: smartmet-library-macgyver >= 17.8.28
%if 0%{rhel} >= 7
Requires: postgis
%else
Requires: postgis
%endif
Provides: %{LIBNAME}
Obsoletes: libsmartmet-gis < 16.12.20
Obsoletes: libsmartmet-gis-debuginfo < 16.12.20

%description
FMI GIS library

%prep
rm -rf $RPM_BUILD_ROOT

%setup -q -n %{SPECNAME}
 
%build
make %{_smp_mflags}

%install
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0775,root,root,0775)
%{_libdir}/libsmartmet-%{DIRNAME}.so

%package -n %{SPECNAME}-devel
Summary: FMI GIS library development files
Provides: %{SPECNAME}-devel
Requires: geos-devel
Requires: %{SPECNAME}
Obsoletes: libsmartmet-gis-devel < 16.2.20

%description -n %{SPECNAME}-devel
FMI GIS library development files

%files -n %{SPECNAME}-devel
%defattr(0664,root,root,0775)
%{_includedir}/smartmet/%{DIRNAME}

%changelog
* Mon Jan 15 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.1.15-1.fmi
- Updated postgresql dependency to version 9.5

* Wed Nov 22 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.11.22-1.fmi
- Added OGR::reverseWindingOrder

* Tue Oct 31 2017 Anssi Reponen <anssi.reponen@fmi.fi> - 17.10.31-1.fmi
- boost::posix_time:ptime type added to Attribute's data type list

* Tue Sep 12 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.9.12-1.fmi
- Added OGR::exportToWkt for OGRSpatialReference

* Sun Aug 27 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.8.28-1.fmi
- Upgrade to boost 1.65

* Tue Mar 14 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.3.14-1.fmi
- Switched to using macgyver StringConversion tools

* Wed Jan 18 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.18-1.fmi
- Upgrade from cppformat-library to fmt

* Fri Jan 13 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.13-1.fmi
- Fixed devel-rpm requirements to refer to the changed name of the main rpm

* Tue Dec 20 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.12.20-1.fmi
- Switched to open source naming conventions

* Wed May 25 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.5.25-1.fmi
- Rectangle clipping now works regardless of input winding rules (backport from GEOS)

* Mon May 9 2016 Anssi Reponen <anssi.reponen@fmi.fi> - 16.5.9-1.fmi
- Error handling corrected in expandGeometry-fuction
- Test cases added for expandGeometry-fuction

* Fri May  6 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.5.6-1.fmi
- Rewrote SVG generation to use strings and cppformat instead of string streams

* Wed May  4 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.5.4-1.fmi
- DEM now allows requested maximum resolution to be zero to imply no limits

* Wed Apr 13 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.4.13-1.fmi
- Fixed PostGIS::read to clone the given spatial reference since no lifetime guarantees are known

* Tue Apr 12 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.4.12-1.fmi
- Fixed PostGIS::read to assign the requested spatial reference to the projected output geometry

* Fri Apr  1 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.4.1-1.fmi
- Despeckle geographies using km^2 units instead of degrees^2

* Thu Mar 31 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.3.31-1.fmi
- Fixed debuginfo packaging

* Thu Nov 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.11.26-1.fmi
- API and types used by API redesigned

* Tue Nov 24 2015 Anssi Reponen <anssi.reponen@fmi.fi> - 15.11.24-1.fmi
- Data structure of return value of getShapeWithAttributes-function modified

* Mon Nov 23 2015 Anssi Reponen <anssi.reponen@fmi.fi> - 15.11.23-2.fmi
- getShapeWithAttributes-function added

* Fri Nov 13 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.11.13-1.fmi
- Fixed a bug in rectangle clipping

* Wed Oct 28 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.10.28-1.fmi
- Replaced deprecated number_cast and lexical_cast commands with to_string etc

* Fri Oct 09 2015 Anssi Reponen <anssi.reponen@fmi.fi> - 15.10.9-1.fmi
- Error in polygon clipping corrected (jira:BRAINSTORM-494)

* Tue Sep 22 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.9.22-1.fmi
- Added support for picking a DEM height from suitable resolution data

* Wed May 13 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.5.13-1.fmi
- Keep the original spatial reference when clipping geometries

* Wed Apr 29 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.29-1.fmi
- Added LandCover class for obtaining global information on land cover types

* Mon Mar 23 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.3.23-1.fmi
- DEM-data is now mapped read-only (due to read-only mounts)

* Fri Feb 27 2015 Anssi Reponen <anssi.reponen@fmi.fi> - 15.2.27-3.fmi
- OGRGeometry returned by expandGeometry function is simplified only if it contains more than 1000 points

* Fri Feb 27 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.2.27-2.fmi
- Fixed the dynamic library to be an executable

* Fri Feb 27 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.2.27-1.fmi
- Fixed rounding of the DEM coordinates, added a new test for Alanya

* Thu Feb 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.2.26-1.fmi
- Fixed elevation retrieval at longitude 180

* Wed Feb 25 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.2.25-1.fmi
- Added DEM class for obtaining height globally for any coordinate

* Thu Feb 19 2015 Anssi Reponen <anssi.reponen@fmi.fi> - 15.2.19-1.fmi
- Added functions to construct and expand OGRGeometry

* Wed Jan  7 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.1.7-1.fmi
- Fixed a bug in formatting number 2999.9499999999998 with one decimal

* Thu Aug 28 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.8.28-1.fmi
- Fixed a bug when clipping a hole in a shape surrounding the rectangle

* Wed Aug 20 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.8.20-1.fmi
- Fixed memory leak, PostGis::read created coordinate transformations which were never destroyed

* Fri Aug 15 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.8.15-1.fmi
- Fixed memory leaks caused by using OGRFree instead of delete

* Fri Aug  8 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.8.8-1.fmi
- PostGIS API now uses an optional for the where clause
- PostGIS API no longer supports simplification at the server side

* Thu Aug  7 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.8.7-3.fmi
- Changed despeckling units from m^2 to km^2

* Thu Aug  7 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.8.7-2.fmi
- Added despeckling of geometries

* Thu Aug  7 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.8.7-1.fmi
- Added optional geometry simplification to the postgis reader

* Fri Jul 25 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.7.25-1.fmi
- Recompiled with the latest GDAL

* Thu Jun 26 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.26-1.fmi
- Fixed OGR::exportToSVG to work properly for integer precision

* Mon Jun 16 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.16-1.fmi
- Added OGR::inside for testing whether a point is inside a geometry

* Thu Jun 12 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.12-1.fmi
- Added Box::itransform to convert pixel coordinates to world coordinates
- Added Box::width and Box::height accessors

* Mon Jun  9 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.9-1.fmi
- Optimized OGR::exportToSvg for speed

* Sat Jun  7 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.7-1.fmi
- Optimized Box::transform for speed

* Fri Jun  6 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.6-3.fmi
- Optimized inner loops of exportToSvg

* Fri Jun  6 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.6-2.fmi
- Fixed bugs in pretty printing SVG paths

* Fri Jun  6 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.6-1.fmi
- Optimized SVG export for speed

* Wed Jun  4 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.4-1.fmi
- First release collects methods from other packages and adds clipping



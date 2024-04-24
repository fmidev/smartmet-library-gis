%bcond_with tests
%define DIRNAME gis
%define LIBNAME smartmet-%{DIRNAME}
%define SPECNAME smartmet-library-%{DIRNAME}
Summary: gis library
Name: %{SPECNAME}
Version: 24.4.24
Release: 1%{?dist}.fmi
License: MIT
Group: Development/Libraries
URL: https://github.com/fmidev/smartmet-library-gis
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot-%(%{__id_u} -n)

%if %{defined el7}
BuildRequires: devtoolset-7-gcc-c++
BuildRequires: proj72-devel
%else
BuildRequires: proj90-devel
%endif

%if 0%{?rhel} && 0%{rhel} < 9
%define smartmet_boost boost169
%define smartmet_sfcgal smartmet-SFCGAL
%else
%define smartmet_boost boost
%define smartmet_sfcgal SFCGAL
%endif

%define smartmet_fmt_min 8.1.1
%define smartmet_fmt_max 8.2.0

BuildRequires: %{smartmet_boost}-devel
BuildRequires: fmt-devel >= %{smartmet_fmt_min}, fmt-devel < %{smartmet_fmt_max}
BuildRequires: gcc-c++
BuildRequires: gdal35-devel
BuildRequires: geos311-devel
BuildRequires: make
BuildRequires: rpm-build
BuildRequires: double-conversion-devel
BuildRequires: libcurl-devel >= 7.61.0
BuildRequires: smartmet-library-macgyver-devel >= 24.1.17
BuildRequires: %{smartmet_sfcgal} >= 1.3.1
%if %{with tests}
BuildRequires: smartmet-library-regression
BuildRequires: smartmet-test-data
%endif
%if 0%{?rhel} && 0%{rhel} <= 7
BuildRequires: sqlite33-devel
%else
BuildRequires: sqlite-devel
%endif
BuildRequires: sqlite3pp-devel
BuildRequires: libcurl-devel
Obsoletes: libsmartmet-gis < 16.12.20
Obsoletes: libsmartmet-gis-debuginfo < 16.12.20
Provides: %{LIBNAME}
Requires: %{smartmet_boost}-filesystem
Requires: %{smartmet_boost}-thread
Requires: fmt >= %{smartmet_fmt_min}, fmt < %{smartmet_fmt_max}
Requires: double-conversion
Requires: gdal35-libs
Requires: geos311
Requires: libcurl >= 7.61.0
Requires: smartmet-library-macgyver >= 24.1.17
#TestRequires: %{smartmet_boost}-devel
#TestRequires: fmt-devel
#TestRequires: gcc-c++
#TestRequires: gdal35-devel
#TestRequires: geos311-devel
#TestRequires: make
#TestRequires: smartmet-library-macgyver-devel
#TestRequires: smartmet-library-macgyver
#TestRequires: smartmet-library-regression
#TestRequires: smartmet-test-data

%description
FMI GIS library

%prep
rm -rf $RPM_BUILD_ROOT

%setup -q -n %{SPECNAME}

%build
make %{_smp_mflags}
%if %{with tests}
make test %{_smp_mflags} CI=Y
%endif

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
Requires: %{SPECNAME} = %{version}-%{release}
Requires: geos311-devel
Requires: %{smartmet_boost}-devel
Requires: fmt-devel >= 7.1.3
Requires: gcc-c++
Requires: gdal35-devel
Requires: smartmet-library-macgyver-devel >= 24.1.17
Obsoletes: libsmartmet-gis-devel < 16.2.20

%description -n %{SPECNAME}-devel
FMI GIS library development files

%files -n %{SPECNAME}-devel
%defattr(0664,root,root,0775)
%{_includedir}/smartmet/%{DIRNAME}

%changelog
* Wed Apr 24 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.4.24-1.fmi
- Added GeometrySmoother

* Mon Mar 25 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.3.25-1.fmi
- Fixed potential nullptr dereference

* Wed Jan  3 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.1.3-1.fmi
- Added Box::areaFactor for doing px^2 to km^2 conversions

* Tue Nov 21 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.11.21-1.fmi
- Repackaged since MappedFile ABI changed

* Tue Sep 12 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.9.12-1.fmi
- Remove unnecessary dependency on PostGIS package

* Wed Aug 30 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.8.30-2.fmi
- Added nullptr checks into getEPSG to avoid crashes for unorthodox spatial references

* Wed Aug 30 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.8.30-1.fmi
- Fixed getEPSG to be more accurate and not to return GEOGCS EPSG for a PROJCS

* Mon Aug 28 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.8.28-2.fmi
- Added sqlite3pp-devel build dependency

* Mon Aug 28 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.8.28-1.fmi
- Added SpatialReference::getEPSG
- Added EPSGInfo for retrieving EPSG information from PROJ library database
- Moved BBox from GIS-engine 
- Fixed caches containing GDAL/OGR objects to use a private singleton to ensure correct destruction order

* Mon Aug 21 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.8.21-1.fmi
- SrtmTile: use Fmi::MappedFile

* Fri Aug  4 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.8.4-1.fmi
- Fix memory leaks

* Fri Jul 28 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.7.28-1.fmi
- Repackage due to bulk ABI changes in macgyver/newbase/spine

* Mon Jul 10 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.7.10-1.fmi
- Use postgresql 15, gdal 3.5, geos 3.11 and proj-9.0

* Wed Jun  7 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.6.7-1.fmi
- Faster access to DEM/landcover data by using the Double Checked Locking pattern

* Tue Mar 14 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.3.14-2.fmi
- Fix more memory leaks

* Tue Mar 14 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.3.14-1.fmi
- Fix memory leaks

* Thu Jan  5 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.1.5-1.fmi
- Added ProjInfo::erase for removing projection settings

* Wed Dec 21 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.12.21-1.fmi
- Fixed issues discovered by CodeChecker

* Wed Dec 14 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.12.14-1.fmi
- Fixed PROJ parser to allow south/west/east/north suffixes

* Wed Sep 28 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.9.28-1.fmi
- Fixed inverse proj string for ob_tran projections

* Thu Sep  1 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.9.1-2.fmi
- PROJ 7.2 has a potential segfault issue in projection destructors, disabled some explicit calls

* Thu Sep  1 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.9.1-1.fmi
- Avoid valgrind memory leak reports by destructing globals properly

* Wed Jul 27 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.7.27-1.fmi
- Repackaged since macgyver CacheStats ABI changed

* Thu Jun 16 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.6.16-1.fmi
- Add support of HEL9, upgrade to libpqxx-7.7.0 (rhel8+) and fmt-8.1.1

* Tue Jun  7 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.6.7-1.fmi
- Fixed nsper circle cut angle

* Wed Jun  1 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.6.1-1.fmi
- Added OGR::exportToSimpleWkt for spatial references

* Wed May  4 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.5.4-1.fmi
- Fixed BoolMatrix bbox calculation to handle ends of rows correctly
- Do not use std::vector<bool> for much better speed
- Fixes to clipping/cutting geometries with a shape
- Added BoolMatrix::hashValue for caching purposes

* Wed Apr  6 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.4.6-1.fmi
- Bug fix to clipping polygons with holes touching the exterior

* Mon Jan 24 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.1.24-1.fmi
- Obsolete postgis31_13 to avoid conflict with required postgis32_13

* Fri Jan 21 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.1.21-1.fmi
- Repackage due to upgrade of packages from PGDG repo: gdal-3.4, geos-3.10, proj-8.2

* Tue Dec  7 2021 Andris Pavēnis <andris.pavenis@fmi.fi> 21.12.7-1.fmi
- Update to postgresql 13 and gdal 3.3

* Fri Sep 24 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.9.24-1.fmi
- Fixed OGR::exportToSvg to properly handle failures and trailing zeros

* Mon Sep 13 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.9.13-2.fmi
- Repackaged due to Fmi::Cache statistics fixes

* Mon Sep 13 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.9.13-1.fmi
- Avoid exceptions in ProjInfo parser

* Thu Sep  2 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.9.2-1.fmi
- Small improvement to error messages

* Mon Aug 30 2021 Anssi Reponen <anssi.reponen@fmi.fi> - 21.8.30-1.fmi
- Cache counters added (BRAINSTORM-1005)

* Tue Aug  3 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.8.3-1.fmi
- Preserve +type setting when parsing PROJ strings

* Tue Jul 27 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.7.27-1.fmi
- Silenced several CodeChecker warnings

* Fri Jun 18 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.6.18-1.fmi
- Fixed reconnectLines bug causing a segfault

* Wed Jun 16 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.6.16-1.fmi
- Use Fmi::Exception

* Mon Jun  7 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.6.7-1.fmi
- Fixed possible reference to freed memory

* Mon May 24 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.24-1.fmi
- Disabled incorrect tmerc interrupt geometry

* Thu May 20 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.20-3.fmi
- Fixed to shape clipping algorithm

* Thu May 20 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.20-2.fmi
- Repackaged with improved hashing functions

* Thu May 20 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.20-1.fmi
- Use Fmi hash functions, boost::combine_hash produces too many collisions

* Wed May  5 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.5-1.fmi
- Fixed several projection interrupts

* Mon May  3 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.4-1.fmi
- Added circle clipping code, refactored common parts with rectangle clipping

* Tue Apr 13 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.4.13-1.fmi
- Fixed clipping of very narrow spikes just barely visiting the box

* Mon Mar 29 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.3.29-1.fmi
- Fixed CoordinateTransformation::getTargetCS and getSourceCS not to create temporaries

* Tue Mar 23 2021 Andris Pavenis <andris.pavenis@fmi.fi> - 21.3.23-1.fmi
- Remove explicit RPM dependency on proj72
- Add missing dependencies for devel package

* Fri Feb 26 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.26-1.fmi
- CoordinateTransformation now returns SpatialReference instead of OGRSpatialReference
- SpatialReference now caches EPSGTreatsAsLatlong on construction

* Thu Feb 25 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.25-1.fmi
- Added interrupt of igh_o projection (ocean oriented interrupted Goode homolosine)

* Wed Feb 24 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.24-4.fmi
- Added an interrupt of the central meridian for general oblique transformations. Not sufficient when lon_0 is not zero.

* Wed Feb 24 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.24-3.fmi
- Fixed o_wrap to lon_wrap in projection interrupt generation

* Wed Feb 24 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.24-2.fmi
- Fixed rounding issues in exportToSvg

* Wed Feb 24 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.24-1.fmi
- Fixed polyclip to handle a special case encountered in HIRLAM data

* Sat Feb 20 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.20-1.fmi
- Fixed lon_wrap handling in projection interrupt generation

* Thu Feb 11 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.11-1.fmi
- Always use std::shared_ptr for OGRGeometryPtr

* Wed Feb 10 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.10-1.fmi
- Initialize SpatialReference IsGeographic and IsAxisSwapped booleans on construction for thread safety
- Added SpatialiteReference construction from a shared pointer to OGRSpatialReference to simplify external code

* Mon Feb  1 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.1-1.fmi
- Strip lon_0 when extracting the ellipsoid for projections
- Updated definition of WGS84 to use +datum=WGS84 or tests fail with PROJ 7.9

* Fri Jan 22 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.22-1.fmi
- Fixed PostGIS to handle the new OFTInteger64 type

* Thu Jan 14 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.14-1.fmi
- Repackaged smartmet to resolve debuginfo issues

* Tue Jan 12 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.12-1.fmi
- Removed obsolete proj-epsg dependency

* Thu Jan  7 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.7-1.fmi
- Fixed OGR::gridNorth based on the WGS84 branch version

* Tue Jan  5 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.5-2.fmi
- Do not show password in stack trace on failure to connect to database

* Tue Jan  5 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.5-1.fmi
- Upgrade to fmt 7.1.3

* Mon Jan  4 2021 Andris Pavenis <andris.pavenis@fmi.fi> - 21.1.4-1.fmi
- Rebuild due to PGDG repository change: gdal-3.2 uses geos-3.9 instead of geos-3.8

* Thu Dec 31 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.12.31-1.fmi
- Require devtoolset-7-gcc-c++ to be able to use clang++ -std=c++17
- Upgraded gdal/geos/proj dependencies

* Tue Dec 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.12.15-2.fmi
- Upgrade to pgsg12

* Tue Dec 15 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.12.15-1.fmi
- Require exactly same version in binary and devel RPM
- proj-epsg tilalle EPEL 8 on proj: use it in case of RHEL 8

* Thu Dec 10 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.12.10-1.fmi
- Adapt to makefile.inc changes

* Wed Oct 28 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.10.28-1.fmi
- Rebuild due to fmt upgrade

* Mon Oct  5 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.10.5-1.fmi
- Rebuild due to smartmet-library-macgyver makefile.inc changes

* Fri Oct  2 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.10.2-1.fmi
- Build update (use makefile.inc from smartmet-library-macgyver)
- Revert change to link always with GCC (no more needed)

* Fri Aug 21 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.21-1.fmi
- Upgrade to fmt 6.2

* Thu Aug 20 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.20-1.fmi
- Optimized exportToSvg to minimize string allocations

* Wed Aug 12 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.12-1.fmi
- Added OGRSpatialReferenceFactory
- Added OGRCoordinateTransformationFactory

* Sat Apr 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.18-1.fmi
- Removed macgyver dependency
- Upgrade to Boost 1.69

* Tue Feb 18 2020 Anssi Reponen <anssi.reponen@fmi.fi> - 20.2.18-1.fmi
- Fixed expanding of MULTILINRSTRING geometry (BRAINSTORM-1757)

* Wed Feb  5 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.2.5-1.fmi
- Fixed gridNorth method to work with GDAL versions > 1

* Wed Jan 29 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.1.29-1.fmi
- Added OGR::transform to scale the geometry to pixel coordinates

* Wed Dec  4 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.12.4-1.fmi
- Use -fno-omit-frame-pointer for a better profiling and debugging experience
- Fixed dependency to be on gdal-libs instead of gdal

* Thu Sep 26 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.9.26-1.fmi
- Added support for GDAL 2
- Avoid regex use to avoid locale locks

* Thu Mar 14 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.3.14-1.fmi
- Added exportToPrettyWkt and exportToProj functions for spatial references

* Tue Mar  5 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.3.5-1.fmi
- Added explicit dependencies on PROJ.4 since GDAL may not require it

* Thu Feb 21 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.2.21-1.fmi
- Despeckling now applies to closed linestrings too to help remove small closed pressure curves

* Mon Dec 10 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.12.10-1.fmi
- Fixed polygon clipping to handle sliver polygons whose intersection coordinates are equal
- Fixed polygon clipping to handle more special cases

* Mon Sep 17 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.17-1.fmi
- Fixed PostGIS API

* Sun Sep 16 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.16-1.fmi
- Silenced several CodeChecker warnings

* Tue Sep 11 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.11-1.fmi
- Silenced several CodeChecker warnings

* Wed Aug 15 2018 Anssi Reponen <anssi.reponen@fmi.fi> - 18.8.15-1.fmi
- Added createFromWkt-function for creating OGRGeometry* from WKT-string
- Added some documentation for existing functions

* Thu Aug  2 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.2-1.fmi
- Deprecated old OGR exportToSvg API immediately to avoid overload ambiguities

* Wed Aug  1 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.1-2.fmi
- Enabled fractional precision when converting OGR geometries to SVG
- Fixed code never to close linestrings even if rounding would close them

* Wed Aug  1 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.1-1.fmi
- Use C++11 for-loops instead of BOOST_FOREACH

* Wed Jul 25 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.7.25-1.fmi
- Prefer nullptr over NULL

* Mon Jul 23 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.7.23-1.fmi
- Avoid integer division in float math to avoid CodeChecker warnings

* Sat Apr  7 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.4.7-1.fmi
- Upgrade to boost 1.66

* Fri Apr  6 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.4.6-1.fmi
- Added OGR::gridNorth for calculating true north azimuths for spatial references

* Tue Apr  3 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.4.3-1.fmi
- Use libfmt where possible instead of string streams

* Wed Mar 7 2018 Anssi Reponen <anssi.reponen@fmi.fi> - 18.3.7-1.fmi
- Use UTC timezone when reading data from PostGIS database (BRAINSTORM-1049)

* Thu Feb  8 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.2.8-1.fmi
- Added explicit postgis version dependency to avoid pgdg problems

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



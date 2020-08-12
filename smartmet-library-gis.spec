%define DIRNAME gis
%define LIBNAME smartmet-%{DIRNAME}
%define SPECNAME smartmet-library-%{DIRNAME}
Summary: gis library
Name: %{SPECNAME}
Version: 20.8.12
Release: 1%{?dist}.fmi
License: MIT
Group: Development/Libraries
URL: https://github.com/fmidev/smartmet-library-gis
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot-%(%{__id_u} -n)
BuildRequires: rpm-build
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: boost169-devel
BuildRequires: fmt-devel >= 5.2.0
BuildRequires: gdal30-devel
BuildRequires: geos38-devel
BuildRequires: smartmet-library-macgyver-devel >= 20.7.31
Requires: fmt >= 5.2.0
Requires: gdal30-libs
Requires: geos38
Requires: postgis
Requires: boost169-filesystem
Requires: boost169-thread
Requires: smartmet-library-macgyver >= 20.7.31
Provides: %{LIBNAME}
Obsoletes: libsmartmet-gis < 16.12.20
Obsoletes: libsmartmet-gis-debuginfo < 16.12.20
#TestRequires: make
#TestRequires: gcc-c++
#TestRequires: smartmet-library-regression
#TestRequires: boost169-devel
#TestRequires: gdal-devel
#TestRequires: fmt-devel
#TestRequires: smartmet-test-data

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
Requires: geos38-devel
Requires: %{SPECNAME}
Obsoletes: libsmartmet-gis-devel < 16.2.20

%description -n %{SPECNAME}-devel
FMI GIS library development files

%files -n %{SPECNAME}-devel
%defattr(0664,root,root,0775)
%{_includedir}/smartmet/%{DIRNAME}

%changelog
* Wed Aug 12 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.12-1.fmi
- Added OGRCoordinateTransformationFactory::Create methods for EPSG numbers

* Thu Aug  6 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.7.6-1.fmi
- Added OGRSpatialReferenceFactory
- Added OGRCoordinateTransformationFactory

* Thu Jul  2 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.7.2-1.fmi
- Added Box constructor with identity transformation properties
- CoordinateTransformation now clones the input SpatialReference objects

* Wed Jul  1 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.7.1-2.fmi
- Optimized CoordinateTransformation::transformGeometry for speed for input/output latlon cases

* Wed Jul  1 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.7.1-1.fmi
- Use shared_ptr for Interrupt objects to enable copy construction

* Mon Jun 29 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.6.29-1.fmi
- Improved spherical interrupts of map data

* Thu May 28 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.5.28-1.fmi
- Added circle cuts for stereographic etc projections

* Fri May 22 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.5.22-1.fmi
- Added class ProjInfo for extracting information from PROJ.4 strings

* Mon May 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.5.18-1.fmi
- Added CoordinateTransformation::transformGeometry for handling antimeridians and simular problems

* Tue Apr 28 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.28-1.fmi
- Fixed SpatialReference::isAxisSwapped to check the axis mapping strategy

* Sun Apr 26 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.26-1.fmi
- Assign WGS84 CRS to shapes read from PostGIS which do not have spatial references

* Thu Apr 23 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.23-1.fmi
- Fixed PostGIS::read to handle attributes of type Integer64
- Added OGR::normalizeWindingOrder and OGR::renormalizeWindingOrder

* Wed Apr 22 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.20-1.fmi
- Hide implementation details of SpatialReference and CoordinateTransformation

* Sat Apr 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.18-1.fmi
- Removed macgyver dependency
- Upgrade to Boost 1.69

* Wed Apr 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.15-1.fmi
- Added SpatialReference::hashValue()

* Sun Apr 12 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.12-1.fmi
- Added CoordinateMatrix
- Added BoolMatrix
- Added CoordinateMatrixAnalysis
- Improved use of same naming conventions

* Wed Apr  8 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.8-1.fmi
- Force traditional GIS axis order strategy (lon,lat and east,north)

* Tue Apr  7 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.7-1.fmi
- Force longlat order for PostGIS geometries

* Mon Apr  6 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.6-1.fmi
- Added SpatialReference and CoordinateTransformation
- Removed macgyver dependency

* Thu Mar 26 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.3.26-1.fmi
- Repackaged after GDAL/GEOS updates

* Tue Feb 18 2020 Anssi Reponen <anssi.reponen@fmi.fi> - 20.2.18-1.fmi
- Fixed expanding of MULTILINRSTRING geometry (BRAINSTORM-1757)

* Fri Feb 14 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.2.14-1.fmi
- Fixed order of linked to directories to get GDAL 3.0 correctly

* Thu Feb 13 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.2.13-1.fmi
- Hide GDAL dependencies in OGR.h to avoid dependency escalation

* Wed Feb  5 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.2.5-1.fmi
- Fixed gridNorth to work with GDAL 3

* Wed Jan 29 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.1.29-1.fmi
- Added OGR::transform to scale the geometry to pixel coordinates

* Tue Dec 10 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.12.10-1.fmi
- Switch to GDAL 3.0 and GEOS 3.8

* Wed Dec  4 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.12.4-1.fmi
- Use -fno-omit-frame-pointer for a better profiling and debugging experience                                                                             - Fixed dependency to be on gdal-libs instead of gdal        
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



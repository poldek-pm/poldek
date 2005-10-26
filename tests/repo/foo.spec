# $Revision$, $Date$
Summary:	foo
Name:		foo
Version:	1.0
Release:    1
License:	GPL
Group:		Base
BuildRoot:	%{tmpdir}/%{name}-%{version}-root-%(id -u -n)
Requires:   foo-lib = %{version}

%description
foo

%package lib
Summary:	foo lib
Group:		Base

%description lib

%prep

%pre 

%files
%defattr(644,root,root,755)

%files lib
%defattr(644,root,root,755)

%clean
rm -rf $RPM_BUILD_ROOT

%define date	%(echo `LC_ALL="C" date +"%a %b %d %Y"`)
%changelog
* %{date} foo

$Log$
Revision 1.1  2005/10/26 22:43:24  mis
- and spec


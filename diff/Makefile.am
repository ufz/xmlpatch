#
# This is part of an XML diff library.
#
# Copyright (C) 2005 Nokia Corporation.
#
# Contact: Jari Urpalainen <jari.urpalainen@nokia.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# $Id:$
#
# ----------------------------------------------------------------------
# General options

INCLUDES = $(DEPS_XML_CFLAGS) $(DEPS_GLIB_CFLAGS)

lib_LTLIBRARIES = libxmldiff.la

libxmldiff_la_SOURCES = xml_diff.c
libxmldiff_la_LIBADD = $(top_builddir)/src/libxmlpatch.la $(DEPS_XML_LIBS) $(DEPS_GLIB_LIBS)

libxmldiff_includedir = $(includedir)/libxmldiff
libxmldiff_include_HEADERS = xml_diff.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libxmldiff.pc



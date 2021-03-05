#
# This is part of an XML patch library.
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

#INCLUDES = $(DEPS_XML_CFLAGS) $(DEPS_GLIB_CFLAGS)

lib_LTLIBRARIES = libxmlpatch.la

libxmlpatch_la_CFLAGS = $(DEPS_XML_CFLAGS)
libxmlpatch_la_SOURCES = xml_patch.c
libxmlpatch_la_LIBADD = $(DEPS_XML_LIBS)

libxmlpatch_includedir = $(includedir)/libxmlpatch
libxmlpatch_include_HEADERS = xml_patch.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libxmlpatch.pc




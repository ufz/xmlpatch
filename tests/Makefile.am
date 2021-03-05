TESTS = test-diff

test_PROGRAMS = test-diff
test_diff_SOURCES = test_diff.c
test_diff_CFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/diff $(DEPS_XML_CFLAGS) $(DEPS_GLIB_CFLAGS)
test_diff_LDADD = $(top_builddir)/src/libxmlpatch.la $(top_builddir)/diff/libxmldiff.la $(DEPS_XML_LIBS) $(DEPS_GLIB_LIBS)
testdir = $(libdir)/tests/xml_diff

bin_PROGRAMS = xml-patch xml-diff

xml_patch_CFLAGS = -I$(top_srcdir)/src $(DEPS_XML_CFLAGS)
xml_patch_SOURCES = xml_patch_main.c
xml_patch_LDADD = $(top_builddir)/src/libxmlpatch.la $(DEPS_XML_LIBS)

xml_diff_CFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/diff $(DEPS_XML_CFLAGS) $(DEPS_GLIB_CFLAGS)
xml_diff_SOURCES = xml_diff_main.c
xml_diff_LDADD = $(top_builddir)/src/libxmlpatch.la $(top_builddir)/diff/libxmldiff.la $(DEPS_XML_LIBS) $(DEPS_GLIB_LIBS)

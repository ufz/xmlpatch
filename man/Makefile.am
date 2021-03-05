man_MANS = xml-patch.1 xml-diff.1
EXTRA_DIST = $(man_MANS)
MAINTAINERCLEANFILES = $(man_MANS)

xml-patch.1: $(top_srcdir)/tests/xml_patch_main.c
	$(HELP2MAN) --name="XML Patch tool" ../tests/xml-patch -o $@

xml-diff.1: $(top_srcdir)/tests/xml_diff_main.c
	$(HELP2MAN) --name="XML Diff tool" ../tests/xml-diff -o $@

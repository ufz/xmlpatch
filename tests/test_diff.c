/**
 * This is part of an XML patch library.
 *
 * Copyright (C) 2010 Nokia Corporation.
 *
 * Contact: Jari Urpalainen <jari@urpalainen.fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>

#include <glib.h>

#include <libxml/tree.h>

#include "xml_diff.h"
#include "xml_patch.h"

static struct state_s {
    char file_from[PATH_MAX];
    char file_to[PATH_MAX];
    const char *directory;
    xmlDoc *from, *to, *diff;
    int dump, opt;
} state[1];

/** patching test, don't care about request namespaces */
static int patch(xmlDoc *doc, xmlNode *node)
{
    int rc = 0;

    if (!strcmp((char *) node->name, "add"))
	rc = xml_patch_add(doc, node);
    else if (!strcmp((char *) node->name, "replace"))
	rc = xml_patch_replace(doc, node);
    else if (!strcmp((char *) node->name, "remove"))
	rc = xml_patch_remove(doc, node);
    else
	rc = -1;

    return rc;
}

static void open_files(struct state_s *state, const char *sz1, const char *sz2)
{
    snprintf(state->file_from, sizeof(state->file_from), "%s/%s",
				state->directory, sz1);
    snprintf(state->file_to, sizeof(state->file_to), "%s/%s",
				state->directory, sz2);

    state->from = xmlParseFile(state->file_from);
    state->to = xmlParseFile(state->file_to);
}

static void setup(void)
{
    state->diff = xmlNewDoc((const xmlChar *) "1.0");

    state->diff->children = xmlNewDocNode(state->diff, NULL,
						(const xmlChar *) "changes",
						NULL);

    xmlSetNs(state->diff->children,
		xmlNewNs(state->diff->children,
			(const xmlChar *) "urn:xml-changes",
			(const xmlChar *) "x"));
}

static void teardown(void)
{
    xmlFreeDoc(state->from);
    xmlFreeDoc(state->to);
    xmlFreeDoc(state->diff);

    xmlCleanupParser();
}

static int patch_test(struct state_s *state)
{
    xmlNode *node = xmlDocGetRootElement(state->diff);
    int rc;

    for (node = node ? node->children : NULL; node; node = node->next) {
	if (node->type == XML_ELEMENT_NODE) {
	    rc = patch(state->from, node);

	    if (rc) {
		fprintf(stderr, "Patch could not be applied with the "
				"produced diff file !\n");
		return -1;
	    }
	}
    }

    rc = xml_exec_diff(state->from, state->to, state->opt,
			state->diff ? state->diff->children : NULL);

    if (rc)
	fprintf(stderr, "Additional patching test FAILED !\n");
    else if (state->dump)
	fprintf(stdout, "Additional patching test succeeded\n");

    return rc;
}

static void diff_test(struct state_s *state, const char *fn,
                      const char *sz1, const char *sz2)
{
    int rc;

    if (state->dump)
	printf("*** starting %s\n", fn);

    setup();
    open_files(state, sz1, sz2);

    /* generating diff */
    rc = xml_exec_diff(state->from, state->to, state->opt,
			state->diff ? state->diff->children : NULL);
    g_assert(rc);

    /* successful diff means rc > 0, checking that patch to original
     * produces equal document with destination */
    if (rc > 0) {
	if (state->dump) {
	    fprintf(stdout, "OLD doc:\n");
	    xmlDocFormatDump(stdout, state->from, 1);
	    fprintf(stdout, "\nNEW doc:\n");
	    xmlDocFormatDump(stdout, state->to, 1);
	    fprintf(stdout, "\nCHANGES:\n");
	    xmlDocDump(stdout, state->diff);
	}

	g_assert(patch_test(state) == 0);
    }

    if (state->dump)
	printf("*** %s ended\n", fn);

    teardown();
}

static void test_12(void)
{
    diff_test(state, __func__, "base1.xml", "base2.xml");
}

static void test_13(void)
{
    diff_test(state, __func__, "base1.xml", "base3.xml");
}

static void test_14(void)
{
    diff_test(state, __func__, "base1.xml", "base4.xml");
}

static void test_15(void)
{
    diff_test(state, __func__, "base1.xml", "base5.xml");
}

static void test_21(void)
{
    diff_test(state, __func__, "base2.xml", "base1.xml");
}

static void test_31(void)
{
    diff_test(state, __func__, "base3.xml", "base1.xml");
}

static void test_41(void)
{
    diff_test(state, __func__, "base4.xml", "base1.xml");
}

static void test_51(void)
{
    diff_test(state, __func__, "base5.xml", "base1.xml");
}

static void test_from_to(void)
{
    diff_test(state, __func__, "from.xml", "to.xml");
}

static void test_to_from(void)
{
    diff_test(state, __func__, "to.xml", "from.xml");
}

static void test_n01(void)
{
    diff_test(state, __func__, "basen.xml", "basen1.xml");
}

static void test_n10(void)
{
    diff_test(state, __func__, "basen1.xml", "basen.xml");
}

static void test_n02(void)
{
    diff_test(state, __func__, "basen.xml", "basen2.xml");
}

static void test_n20(void)
{
    diff_test(state, __func__, "basen2.xml", "basen.xml");
}

int main (int argc, char *argv[])
{
    int ret;

    static struct option const opt_tbl[] =
    {
	{ "directory",	required_argument, NULL, 'd' },
	{ "no_ws",	no_argument, NULL, 'w' },
	{ "short",	no_argument, NULL, 's' },
	{ "debug",	no_argument, NULL, 'g' },
	{ "help",	no_argument, NULL, 'h' },
	{ NULL, 0, NULL, 0 }
    };

    g_test_init(&argc, &argv, NULL);

    state->directory = "../diff/tests";

    while (ret = getopt_long(argc, argv, "hogwd:", opt_tbl, NULL), ret != -1)
    switch (ret) {
    case 'g':
	state->dump = TRUE;
	break;

    case 'd':
	state->directory = optarg;
	break;

    case 'w':
	state->opt = XML_DIFF_NO_WHITESPACE;
	break;

    case 'o':
	state->opt = XML_DIFF_SIZE_OPTIMIZE;
	break;

    case 'h':
    default:
	printf("test-diff [OPTIONS]\n\n"
		"OPTIONS:\n"
		"  -g dump documents\n"
		"  -l dump available test names, no actual runs\n"
		"  -d (--directory) where all test files exist (%s)\n"
		"  --help lists g_test help\n", state->directory
	       );
	return EXIT_SUCCESS;
    }

    g_test_add_func("/patch/test_1_to_2", test_12);
    g_test_add_func("/patch/test_1_to_3", test_13);
    g_test_add_func("/patch/test_1_to_4", test_14);
    g_test_add_func("/patch/test_1_to_5", test_15);
    g_test_add_func("/patch/test_2_to_1", test_21);
    g_test_add_func("/patch/test_3_to_1", test_31);
    g_test_add_func("/patch/test_4_to_1", test_41);
    g_test_add_func("/patch/test_5_to_1", test_51);
    g_test_add_func("/patch/test_from_to", test_from_to);
    g_test_add_func("/patch/test_to_from", test_to_from);
    g_test_add_func("/patch/test_n_0_to_1", test_n01);
    g_test_add_func("/patch/test_n_0_to_2", test_n02);
    g_test_add_func("/patch/test_n_2_to_0", test_n10);
    g_test_add_func("/patch/test_n_1_to_0", test_n20);

    ret = g_test_run();

    return ret;
}


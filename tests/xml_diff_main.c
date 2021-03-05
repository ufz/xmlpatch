/**
 * This is part of an XML patch/diff library.
 *
 * Copyright (C) 2005 Nokia Corporation.
 *
 * Contact: Jari Urpalainen <jari.urpalainen@nokia.com>
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

#include "config.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <getopt.h>
#include <ctype.h>
#include <unistd.h>
#include <sysexits.h>

#include <glib.h>

#include <libxml/tree.h>

#include "xml_diff.h"
#include "xml_patch.h"


static void usage()
{
    printf("xml-diff [OPTIONS] -f <from> -t <to>\n\n"
	   "OPTIONS:\n"
	   "  -v == verbose output\n"
	   "  -s == try to shorten requests\n"
	   "  -b == bug testing with patch\n" \
	   "  -o <file> == dumb diff output to file\n\n"
	   "returns 0 if documents don't differ, 1 if documents differ and "
	   "76 if diff generation failed\n");
}

/** patching, don't care about request namespaces */
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

int main(int argc, char *argv[])
{
    int opt, rc = EXIT_SUCCESS, dump = 0, optimize = 0, bugs = 0;
    const char *doc_from = NULL, *doc_to = NULL, *output = NULL;
    xmlDoc *from, *to, *diff;

    static const struct option opts[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 1 },
	{ NULL, 0, NULL, 0 } };

    while ((opt = getopt_long(argc, argv, "vsbf:t:o:h", opts, NULL)) != -1) {
	switch (opt) {
	case 1:
	    printf("xml-diff version: %s\n", VERSION);
	    return EXIT_SUCCESS;

	case 'h':
	    usage();
	    return EXIT_SUCCESS;

	case '?':
	    usage();
	    return EXIT_FAILURE;

	case 'v':
	    dump = 1;
	    break;

	case 's':
	    optimize = 1;
	    break;

	case 'b':
	    bugs = 1;
	    break;

	case 'f':
	    doc_from = optarg;
	    break;

	case 't':
	    doc_to = optarg;
	    break;

	case 'o':
	    output = optarg;
	    break;
	}
    }
    if (doc_to == NULL || doc_from == NULL) {
	usage();
	return EXIT_FAILURE;
    }
    diff = xmlNewDoc((const xmlChar*) "1.0");

    diff->children = xmlNewDocNode(diff, NULL,
					(const xmlChar*)"changes", NULL);
    xmlSetNs(diff->children,
	     xmlNewNs(diff->children,
			(const xmlChar*) "urn:xml-changes",
			(const xmlChar*) "x"));

    from = xmlParseFile(doc_from);
    to = xmlParseFile(doc_to);

    rc = xml_exec_diff(from, to, optimize, diff->children);

    if (dump && rc >= 0) {
	fprintf(stdout, "OLD doc:\n");
	xmlDocFormatDump(stdout, from, 1);

	if (rc > 0) {
	    fprintf(stdout, "\nNEW doc:\n");
	    xmlDocFormatDump(stdout, to, 1);
	    fprintf(stdout, "\nCHANGES:\n");
	    xmlDocDump(stdout, diff);
	} else if (rc == 0) {
	    fprintf(stdout, "\nEqual documents within xml-diff scope\n");
	}
    }
    if (rc < 0)
	fprintf(stderr, "Diff could not be generated\n");

    if (output && rc > 0)
	xmlSaveFile(output, diff);

    if (rc > 0 && bugs) {
	xmlNode *node = xmlDocGetRootElement(diff);

	for (node = node ? node->children : NULL;
		node; node = node->next) {
	    if (node->type != XML_ELEMENT_NODE)
		continue;

	    rc = patch(from, node);

	    if (rc) {
		fprintf(stderr, "Patch could not be applied with the "
					"produced diff file !\n");
		break;
	    }
	}

	/* check still that patched from == to */
	if (!rc) {
	    rc = xml_exec_diff(from, to, optimize,
				diff ? diff->children : NULL);

	    if (rc)
		fprintf(stderr, "Additional patching test FAILED !\n");
	    else
		fprintf(stdout, "Additional patching test succeeded\n");
	}
    }
    xmlFreeDoc(from);
    xmlFreeDoc(to);
    xmlFreeDoc(diff);

    xmlCleanupParser();

    return rc == 0 ? EXIT_SUCCESS : rc > 0 ? EXIT_FAILURE : EX_PROTOCOL;
}

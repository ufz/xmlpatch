/**
 * This is part of an XML patch library.
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
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>

#include <libxml/tree.h>

#include "xml_patch.h"

static void usage()
{
    fprintf(stdout, "xml-patch [OPTIONS] -f <doc> -p <patch>\n\n"
		"OPTIONS:\n"
		"  -v == verbose output\n"
		"  -o <file> == dumb output to file\n\n"
		"returns 0 if succeeds, 1 for an error\n");
}

/** patching, don't care about request namespaces */
static int patch_doc(xmlDoc *doc, xmlNode *node)
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

/** main for xml-patching */
int main(int argc, char *argv[])
{
    int opt, rc = -1, dump = 0, i = 1;
    const char *doc_orig = NULL, *doc_patch = NULL, *output = NULL;
    xmlDoc *doc, *patch;
    xmlNode *node;

    static const struct option opts[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 1 },
	{ NULL, 0, NULL, 0 } };

    while ((opt = getopt_long(argc, argv, "vf:p:o:h", opts, NULL)) != -1) {
	switch (opt) {
	case 1:
	    printf("xml-patch version: %s\n", VERSION);
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

	case 'f':
	    doc_orig = optarg;
	    break;

	case 'p':
	    doc_patch = optarg;
	    break;

	case 'o':
	    output = optarg;
	    break;
	}
    }
    if (doc_orig == NULL || doc_patch == NULL) {
	usage();
	return EXIT_FAILURE;
    }

    /* do not prevent the creation of cdata nodes */
    doc = xmlParseFile(doc_orig);
    patch = xmlParseFile(doc_patch);

    node = xmlDocGetRootElement(patch);

    for (node = node ? node->children : NULL;
	 node; node = node->next) {

	if (node->type != XML_ELEMENT_NODE)
	    continue;

	if (dump) {
	    fprintf (stdout, "%d. Patching document:\n", i);

	    xmlDocDump(stdout, doc);
	    fprintf(stdout, "\n%d. patch:\n", i++);
	    xmlElemDump(stdout, patch, node);
	}

	rc = patch_doc(doc, node);

	if (dump) {
	    fprintf(stdout, "\n%s.\n", rc ? "failed" : "succeeded");
	    if (rc)
		break;

	    fprintf(stdout, "\nUpdated document:\n");
	    xmlDocDump(stdout, doc);
	    fprintf(stdout, "\n");
	}
	if (rc)
	    break;
    }

    if (output && rc == 0)
	xmlSaveFile(output, doc);

    xmlFreeDoc(doc);
    xmlFreeDoc(patch);

    xmlCleanupParser();

    if (dump)
	fprintf(stdout, "Patching %s\n", !rc ? "successful" : "failed");
    else if (rc != 0)
	fprintf(stderr, "Patching failed\n");

    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}


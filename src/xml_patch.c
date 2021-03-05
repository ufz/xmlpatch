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

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "xml_patch.h"

#define STRCMP(x, y) \
  strcmp((char *) x, (char *) y)

#define STRNCMP(x, y, z) \
  strncmp((char *) x, (char *) y, z)

/** is condition numeric ? */
static int is_numeric(const char *pcsz, const char *p)
{
    if (p == pcsz)
	return 0;

    for ( ; pcsz < p; pcsz++)
	if (!isdigit(pcsz[0]))
	    return 0;

    return 1;
}

/** is xpath function, very simple heuristics here ... */
static int is_function(const char *pcsz)
{
    const char *p = pcsz;

    for ( ; p[0] && strchr("/][ ", *p) == NULL; p++)
	if (p[0] == '(' && p != pcsz)
	    return 1;

    return 0;
}

static int str_alloc_cat(char **ppb, int *pcb, const char *fmt, ...)
			__attribute__((__format__ (__printf__, 3, 4)));

/** reallocates and appends to a string with printf format va-list */
static int str_alloc_cat(char **ppb, int *pcb, const char *fmt, ...)
{
    int cb, init = *ppb ? strlen (*ppb) : 0;
    char buf[300];
    va_list list;

    va_start(list, fmt);
    cb = vsnprintf(buf, sizeof (buf), fmt, list);

    if (*ppb == NULL || cb + init >= *pcb) {
	*pcb = cb + 100 + init;
	*ppb = realloc(*ppb, *pcb);
    }

    if (cb >= sizeof(buf))
	vsnprintf(*ppb + init, cb + 1, fmt, list);
    else
	memcpy(*ppb + init, buf, cb + 1);
    va_end(list);

    return cb + init;
}

/**
 * change query string by adding *[namespace-uri()='urn:...'][local-name()='...']
 * for elements attached with the default namespace
 *
 * this won't work correctly with the full XPath 1.0 specification,
 * instead only with this very limited XPath feature set
 */
static char *get_xpath_with_default_ns(const char *def_uri, /* default ns uri */
                                       const char *pcsz)    /* orig xpath string */
{
    const char *p;
    char *pch = NULL;
    int cb = 0, cond = 0;

    if (!pcsz)
	return NULL;

    for ( ; *pcsz; pcsz = p) {
	while (*pcsz == '/') {
	    str_alloc_cat(&pch, &cb, "/");
	    pcsz++;
	}
	for (p = pcsz; *p && strchr("/:[]=()| ", *p) == NULL; p++)
	    ;

	if (*p == ':') {
	    if (p[1] == ':') {
		p += 2;

		if (!strncmp(pcsz, "attribute", 9) ||
			!strncmp(pcsz, "namespace", 9)) {
		    for ( ; *p && strchr("/[", *p) == NULL; p++)
			;
		}
	    }
	    else {
		for (p++; *p && strchr("/[ ", *p) == NULL; p++)
		    ;
	    }
	    str_alloc_cat(&pch, &cb, "%.*s", (int) (p - pcsz), pcsz);
	}
	else {
	    if (cond && (!strncmp(pcsz, "and ", 4) ||
			 !strncmp(pcsz, "or ", 3))) {
		str_alloc_cat(&pch, &cb, "%.*s", (int) (p - pcsz), pcsz);
	    }
	    else if (*p == '(' && pcsz == p) {
		;
	    }
	    else if (is_function(pcsz)) {
		for (p++; *p && strchr(")", *p) == NULL; p++)
		    ;
		str_alloc_cat(&pch, &cb, "%.*s", (int)(p - pcsz), pcsz);
	    }
	    else if (pcsz[0] == '@' ||
		     pcsz[0] == '*' ||
		     (pcsz[0] == '.' && p == pcsz + 1) ||
		     (pcsz[0] == ' ' && p == pcsz) ||
		     is_numeric(pcsz, p) ||
		     !strncmp(pcsz, "..", 2)) {
		str_alloc_cat(&pch, &cb, "%.*s", (int) (p - pcsz), pcsz);
	    }
	    else if (p != pcsz) {
		if (def_uri)
		    str_alloc_cat(&pch, &cb, "*[local-name()=\"%.*s\"]"
					     "[namespace-uri()=\"%s\"]",
					     (int) (p - pcsz), pcsz, def_uri);
		else
		    str_alloc_cat(&pch, &cb, "%.*s", (int) (p - pcsz), pcsz);
	    }
	    if (*p == '[')
		cond++;
	    else if (*p == ']' && cond)
		cond--;

	    if (!p[0])
		break;
	    pcsz = p++;

	    if (pcsz[0] == '=') {
		if (*p == ' ') {
		    str_alloc_cat(&pch, &cb, "= ");
		    p++;
		    while(*p == ' ') {
			str_alloc_cat(&pch, &cb, " ");
			p++;
		    }
		    pcsz = p;
		}
		for ( ; *p && strchr("] ", *p) == NULL; p++)
		    ;
		if (*p == ']') {
		    p++;
		    if (cond)
			cond--;
		}
	    }
	    str_alloc_cat(&pch, &cb, "%.*s", (int) (p - pcsz), pcsz);
	}
    }

    return pch;
}

/**
 * remove positional constraint from the text-node patch
 * this is because CDATA and text-nodes are separated in libxml2,
 * but not in XPath data model
 * an alternative would be to parse without cdatas, but it is then
 * ~impossible to store the intended CDATA back to the document
 */
static char *reset_pos_constraint(char *pch, int *p)
{
    char *s = strrchr(pch, '/');

    if (!s)
	s = pch;
    else
	s++;

    if (!s) {
	*p = -1;
	return NULL;
    }

    if (!strcmp(s, "text()")) {
	*p = 0;
	return s + 6;
    }

    if (!strncmp(s, "text()[", 7) &&
		strspn(s + 7, "0123456789]") == strlen(s + 7)) {
	s += 6;
	*s = 0;
	*p = atoi(s + 1);
	return s;
    }

    *p = -1;
    return NULL;
}

/**
 * locate the text or cdata node combinations
 * and check that no several combinations exist
 */
static int rem_text_nodes(xmlNode **pp, int c, int indText)
{
    int i, j;

#define SKIP(s)					\
	{					\
	  int skip = s;				\
	  for (i = j; i + skip < c; i++)	\
	    pp[i] = pp[i + skip];		\
	  c -= skip;				\
	}

    for (j = 0; j < c; ) {
	for (i = ++j; i < c; i++) {
	    if (pp[i - 1]->next != pp[i])
		break;
	}
	if (i == j)
	    continue;

	SKIP (i - j);
    }
    if (!indText)
	return c;

    for (j = 0; j < c; ) {
	xmlNode *parent = pp[j]->parent;

	for (i = j + 1; i < c; i++)
	    if (pp[i]->parent != parent)
		break;
	i -= j;

	if (indText <= i) {
	    int e = i - indText;

	    SKIP(indText - 1);
	    j++;
	    SKIP(e);
	}
	else {
	    SKIP(i);
	}
    }

    return c;
}

/** simple full xpath query over doc */
static xmlNode *process_xpath(xmlDoc *doc, xmlNode *node,
                              const char *pcsz, const char *multi,
                              xmlNode ***ppp, int *pc)
{
    xmlXPathCompExpr *comp;
    xmlNode *rc = NULL, **pp = NULL;
    xmlNs *ns, **nslist;
    int i = 0, text = -1;

    *ppp = NULL;
    *pc = 0;

    if (multi) {
	if (pcsz)
	    return rc;

	pcsz = multi;
    }

    nslist = xmlGetNsList(node->doc, node);
    for (i = 0; (ns = nslist ? nslist[i] : NULL); i++)
	if (ns->type == XML_NAMESPACE_DECL && !ns->prefix)
	    break;

    /* if default namespace is being used, change the request so that elements
     * without prefixes are qualified with this default namespace uri */
    if (ns && ns->href && ns->href[0]) {
	char *pch = get_xpath_with_default_ns((char *) ns->href, pcsz);

	reset_pos_constraint(pch, &text);
	comp = xmlXPathCompile((const xmlChar *) pch);
	free(pch);
    }
    else {
	reset_pos_constraint((char *) pcsz, &text);
	comp = xmlXPathCompile((const xmlChar *) pcsz);
    }

    if (comp) {
	xmlXPathObject *res = NULL;
	xmlXPathContext *ctxt = NULL;

	ctxt = xmlXPathNewContext(doc);
	ctxt->node = (xmlNode *) doc;

	for (i = 0; (ns = nslist ? nslist[i] : NULL); i++)
	    if (ns->type == XML_NAMESPACE_DECL && ns->prefix)
		xmlXPathRegisterNs (ctxt, ns->prefix, ns->href);

	res = xmlXPathCompiledEval(comp, ctxt);
	xmlXPathFreeCompExpr(comp);

	if (res && res->nodesetval) {
	    *ppp = pp = calloc(*pc = res->nodesetval->nodeNr, sizeof(*pp));

	    for (i = 0; i < *pc; i++) {
		pp[i] = res->nodesetval->nodeTab[i];

		if (pp[i]->type == XML_NAMESPACE_DECL) {
		    /* the content of the in-scope namespace declaration will
		     * disappear after freeing XPath objects */
		    ns = xmlMalloc(sizeof(*ns));
		    memset(ns, 0, sizeof(*ns));
		    ns->type = XML_NAMESPACE_DECL;
		    ns->prefix = xmlStrdup(((xmlNs *) pp[i])->prefix);
		    ns->href = xmlStrdup(((xmlNs *) pp[i])->href);
		    ns->next = ((xmlNs *) pp[i])->next;
		    pp[i] = (xmlNode *) ns;
		}
	    }
	    if (text >= 0)
		*pc = rem_text_nodes(pp, *pc, text);
	}
	if (*pc == 1 || (multi && *pc >= 1))
	    rc = pp[0];

	xmlXPathFreeContext(ctxt);
	if (res)
	    xmlXPathFreeObject(res);
    }

    xmlFree(nslist);

    return rc;
}

/**
 * check that only single possible ns declaration exists
 * if same prefix exists, select it. Otherwise only single possibility approved,
 * for attributes it means skipping default namespace declaration
 */
static xmlNs *find_ns(xmlNs **nslist, xmlNs *ns, int attr)
{
    xmlNs *ret = NULL, *t;
    int i = 0, j = 0;

    for (; (t = nslist ? nslist[i] : NULL); i++) {
	if (t->type == XML_NAMESPACE_DECL && !STRCMP(t->href, ns->href)) {
	    if (!attr || t->prefix) {
		j++;
		ret = t;
	    }
	    if ((!t->prefix && !ns->prefix && !attr) ||
			(t->prefix && ns->prefix &&
				!STRCMP(t->prefix, ns->prefix)))
		return t;
	}
    }

    return j == 1 ? ret : NULL;
}

/**
 * select ns declaration from parent or otherwise it has to be a local declaration
 * within the added new node(s)
 */
static xmlNs *select_ns(xmlNs **nslist, xmlNs *ns,
                        xmlNode *child, xmlNode *add, int attr)
{
    xmlNs *ret;

    if (!(ret = find_ns(nslist, ns, attr))) {
	/* ns must be a local declaration */
	xmlNode *node;

	for (node = child; node && node != add->parent; node = node->parent) {
	    xmlNs *t = node->nsDef;

	    for ( ; t; t = t->next)
		if (t == ns)
		    return ns;
	}
    }

    return ret;
}

static int ignore_local_nsdef(xmlNs *ns, xmlNs **nslist)
{
    xmlNs **t = nslist;

    if (ns == NULL)
	return 1;

    for ( ; t && *t; t++)
	if (*t == ns)
	    return 1;

    return 0;
}

/** replace recursively ns to match those declarations in the document */
static int set_recursive_ns(xmlNode *child, xmlNode *add, xmlNs **nslist)
{
    xmlAttr *attr;
    int rc = 0;
    xmlNs **ns_child_list = xmlGetNsList(NULL, child);

    for ( ; child && !rc; child = child->next) {
	if (child->type != XML_ELEMENT_NODE)
	    continue;

	for (attr = child->properties; attr; attr = attr->next) {
	    if (ignore_local_nsdef(attr->ns, ns_child_list) == 0 &&
			attr->ns->href &&
			STRCMP(attr->ns->href,
				"http://www.w3.org/XML/1998/namespace")) {

		if (!(attr->ns = select_ns(nslist, attr->ns, child, add, 1))) {
		    rc = -1;
		    break;
		}
	    }
	}
	if (ignore_local_nsdef(child->ns, ns_child_list) == 0 &&
		child->ns->href &&
		STRCMP(child->ns->href,
			"http://www.w3.org/XML/1998/namespace")) {

	    if (!(child->ns = select_ns(nslist, child->ns, child, add, 0))) {
		rc = -1;
		break;
	    }
	}
	if (child->children)
	    return set_recursive_ns(child->children, add, nslist);
    }
    xmlFree(ns_child_list);

    return rc;
}

/** set namespaces to match the document */
static int set_ns(xmlDoc *doc, xmlNode *parent, xmlNode *add)
{
    int rc = 0;
    xmlNs **nslist, *ns;
    xmlNode *add_parent = add->parent;

    if (add->type != XML_ELEMENT_NODE)
	return rc;

    add->parent = NULL;

    nslist = xmlGetNsList(doc, parent);

    if (!add->ns && add->type == XML_ELEMENT_NODE &&
		parent->type == XML_ELEMENT_NODE &&
		parent->ns && !parent->ns->prefix) {
	/*
	 * parent has default namespace, but child element
	 * may have reseted it, otherwise error occured
	 */
	for (ns = add->nsDef; ns; ns = ns->next)
	    if (!ns->prefix && ns->href && !ns->href[0])
		break;
	if (ns == NULL)
	    rc = -1;
    }
    if (!rc)
	rc = set_recursive_ns(add, add, nslist);

    add->parent = add_parent;

    xmlFree(nslist);

    return rc;
}

/** append text content */
static xmlNode *append_content(xmlNode *old, xmlNode *n)
{
    xmlChar *pch = xmlNodeGetContent(n);
    xmlNodeAddContent(old, pch);
    xmlFree(pch);
    xmlFreeNode(n);

    return old;
}

/** prepend */
static xmlNode *prepend_content(xmlNode *old, xmlNode *n)
{
    xmlChar *pch = xmlNodeGetContent(old);
    xmlNodeSetContent(old, n->content ? n->content : (const xmlChar *) "");
    xmlNodeAddContent(old, pch);
    xmlFree(pch);
    xmlFreeNode(n);

    return old;
}

/** add child node */
static int add_node(xmlNode *node, xmlNode *n,
                    xmlNode * (fn)(xmlNode *node, xmlNode *n),
                    xmlNode **pnext)
{
    xmlNode *next = *pnext = n->next;

    if (set_ns(node->doc, node, n))
	return -1;

    xmlUnlinkNode(n);

    if (n->type == XML_CDATA_SECTION_NODE || n->type == XML_TEXT_NODE) {
	if (fn == xmlAddPrevSibling) {
	    if (!next && node->type == n->type)
		/* prepend */
		prepend_content(node, n);
	    else if (node->prev && node->prev->type == n->type)
		/* append content */
		append_content(node->prev, n);
	    else
		fn(node, n);
	}
	else if (fn == xmlAddNextSibling) {
	    if (node->type == n->type)
		append_content(node, n);
	    else if (!next && node->next && node->next->type == n->type)
		prepend_content(node->next, n);
	    else
		fn(node, n);
	}
	else {
	    xmlNode *child = xmlGetLastChild(node);

	    if (child && child->type == n->type)
		append_content(child, n);
	    else
		fn(node, n);
	}
    }
    else {
	fn(node, n);
    }

    return 0;
}

/** add previous sibling node */
static int add_prev_sibling(xmlNode *node, xmlNode *n, xmlNode **next)
{
    return add_node(node, n, xmlAddPrevSibling, next);
}

/** add next sibling node */
static int add_next_sibling(xmlNode *node, xmlNode *n, xmlNode **next)
{
    return add_node(node, n, xmlAddNextSibling, next);
}

/** add child node */
static int add_child(xmlNode *node, xmlNode *n, xmlNode **next)
{
    return add_node(node, n, xmlAddChild, next);
}

/** adding elements, texts, processing inst, comments */
static int add_nodes(xmlNode *node, const char *pos, xmlNode *add)
{
    xmlNode *n = NULL, *next;

    /* if not added content -> error */
    if (!add->children)
	return -1;

    /* position check */
    if (pos == NULL || !STRCMP(pos, "to")) {
	/* the most typical mode: add child node(s) */
	for (n = add->children; n; n = next)
	    if (add_child(node, n, &next))
		return -1;
    }
    else if (!STRCMP(pos, "prepend") || !STRCMP(pos, "p")) {
	xmlNode *child = node->children;
	int i = 0;

	/* add multiple siblings as the first child(s) */
	for (n = add->children; n; n = next, i++) {
	    if (!node->children) {
		if (add_child(node, n, &next))
		    return -1;
		child = node->children;
	    }
	    else if (i) {
		if (add_next_sibling(child, n, &next))
		    return -1;
		child = n;
	    }
	    else {
		if (add_prev_sibling(child, n, &next))
		    return -1;
		child = n;
	    }
	}
    }
    else if (!STRCMP(pos, "before") || !STRCMP(pos, "b")) {
	/* add multiple preceding siblings */
	for (n = add->children; n; n = next)
	    if (add_prev_sibling (node, n, &next))
		return -1;
    }
    else if (!STRCMP(pos, "after") || !STRCMP(pos, "a")) {
	/* add multiple following siblings */
	for (n = add->children; n; n = next) {
	    int text = (node->type == n->type && n->type == XML_TEXT_NODE);

	    if (add_next_sibling(node, n, &next))
		return -1;

	    node = text ? node : n;
	}
    }
    else {
	return -1;
    }

    return 0;
}

/** adding sibling node(s) to the document */
int xml_patch_add(xmlDoc *doc, xmlNode *node_add)
{
    xmlNode *node = node_add;
    xmlChar *multi = xmlGetProp(node, (const xmlChar *) "msel"); /* unspecified extension, allows multiselect */
    xmlChar *sel = xmlGetProp(node, (const xmlChar *) "sel");
    xmlChar *type = xmlGetProp(node, (const xmlChar *) "type");
    xmlChar *pos = xmlGetProp(node, (const xmlChar *) "pos");
    xmlNode **ppnodes = NULL;
    int cnodes;
    xmlNode *s = process_xpath(doc, node, (const char *) sel,
				(const char *) multi, &ppnodes, &cnodes);
    int rc = s ? 0 : -1, i;

    for (i = 0; rc == 0 && i < cnodes; i++) {
	s = ppnodes[i];

	if (multi && cnodes > 1) {
	    node = node_add;
	    node_add = xmlCopyNode(node_add, 1);
	}

	if ((s->type != XML_ELEMENT_NODE && (!pos || !STRCMP(pos, "to"))) ||
	    (pos &&
	     STRCMP(pos, "b") && STRCMP(pos, "before") &&
	     STRCMP(pos, "a") && STRCMP(pos, "after") &&
	     STRCMP(pos, "p") && STRCMP(pos, "prepend") &&
	     s->type != XML_ELEMENT_NODE &&
	     s->type != XML_PI_NODE &&
	     s->type != XML_COMMENT_NODE &&
	     s->type != XML_TEXT_NODE &&
	     s->type != XML_CDATA_SECTION_NODE)) {
	    rc = -1;
	}
	else {
	    /* add elements etc.. */
	    if (!type || !STRCMP(type, "node()")) {
		rc = add_nodes(s, (const char *) pos, node);
	    }
	    else if (type[0] == '@' && (!pos || !STRCMP(pos, "to"))) {
		/* adding an attribute */
		xmlNs *ns = NULL;
		char *p;
		xmlChar *pchCont;

		if ((p = strchr((char *) type, ':'))) {
		    *p = 0;
		    ns = xmlSearchNs(node->doc, node, type + 1);
		    if (!ns)
			rc = -1;
		}
		else {
		    p = (char *) type;
		}

		if (!rc && ns) {
		    xmlNs **nslist = xmlGetNsList(doc, s);
		    ns = find_ns(nslist, ns, 1);
		    if (!ns)
			rc = -1;
		    xmlFree(nslist);
		}
		if (!rc) {
		    if (!xmlSetNsProp(s, ns, (xmlChar *) p + 1,
					pchCont = xmlNodeGetContent(node)))
			rc = -1;
		    xmlFree(pchCont);
		}
	    }
	    else if (!STRNCMP(type, "namespace::", 11) &&
		     (!pos || !STRCMP(pos, "to")) ) {
		/* adding namespace declaration */
		xmlChar *pchCont;
		if (!xmlNewNs(s, pchCont = xmlNodeGetContent(node), type + 11))
		    rc = -1;
		xmlFree(pchCont);
	    } else {
		rc = -1;
	    }
	}
	if (rc < 0) {
	    if (multi && cnodes > 1)
		xmlFreeNode(node_add);
	    break;
	}
    }
    free(ppnodes);

    xmlFree(multi);
    xmlFree(sel);
    xmlFree(type);
    xmlFree(pos);

    return rc;
}

/** is a nested node in multi-select case */
static int is_nested_node(xmlNode *node, xmlNode **ppnodes, int cnodes)
{
    for (node = node ? node->parent : NULL;
		node && node->type != XML_DOCUMENT_NODE;
		node = node->parent) {
	int i;

	for (i = 0; i < cnodes; i++)
	    if (node == ppnodes[i])
		return 1;
    }

    return 0;
}

/** remove nested nodes in multi-select replace && delete */
static int remove_nested_nodes(xmlNode **ppnodes, int cnodes)
{
    /* removed nested nodes from the set */
    int i;

    for (i = 0; i < cnodes; ) {
	if (is_nested_node(ppnodes[i], ppnodes, cnodes)) {
	    int j = i;

	    for ( ; j + 1 < cnodes; j++)
		ppnodes[j] = ppnodes[j + 1];
	    cnodes--;
	}
	else {
	    i++;
	}
    }

    return cnodes;
}

/** remove sibling text & cdata nodes from the parsed tree */
static void remove_nested_text_cdata(xmlNode *s)
{
    xmlNode *next, *n = s;

    for ( ; n; n = next) {
	if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE) {
	    next = n->next;
	    xmlUnlinkNode(n);
	    xmlFreeNode(n);
	}
	else {
	    break;
	}
    }
}

/** replacing single nodes from the document */
int xml_patch_replace(xmlDoc *doc, xmlNode *node_replace)
{
    xmlNode **ppnodes = NULL, *node = node_replace;
    xmlChar *sel = xmlGetProp(node, (const xmlChar*) "sel");
    xmlChar *multi = xmlGetProp(node, (const xmlChar*) "msel"); /* unspecified extension, allows multiselect */
    int cnodes;
    xmlNode *s = process_xpath(doc, node, (char *) sel, (char *) multi,
					&ppnodes, &cnodes);
    int rc = s ? 0 : -1, i;

    if (multi && !rc)
	cnodes = remove_nested_nodes(ppnodes, cnodes);
    /* the other option would be to fail the request */

    for (i = 0; rc == 0 && i < cnodes; i++) {
	s = ppnodes[i];

	if (multi && cnodes > 1) {
	    node = node_replace;
	    node_replace = xmlCopyNode(node, 1);
	}

	switch(s->type) {
	case XML_ELEMENT_NODE:
	    if (node->children &&
		node->children->type == XML_ELEMENT_NODE &&
			node->children->next == NULL) {
		if (set_ns(doc, s->parent, node->children) < 0) {
		    free(ppnodes);
		    xmlFree(sel);
		    xmlFree(multi);
		    return -1;
		}
		xmlReplaceNode(s, node->children);
		xmlFreeNode(s);
	    }
	    else {
		rc = -1;
	    }
	    break;

	case XML_COMMENT_NODE:
	case XML_PI_NODE:
	    if (node->children &&
		node->children->type == s->type &&
			node->children->next == NULL) {
		xmlReplaceNode(s, node->children);
		xmlFreeNode(s);
	    }
	    else {
		rc = -1;
	    }
	    break;

	case XML_ATTRIBUTE_NODE:
	    {
		xmlChar *pchCont = xmlNodeGetContent(node);
		xmlNodeSetContent(s, pchCont);
		xmlFree(pchCont);
	    }
	    break;

	case XML_CDATA_SECTION_NODE:
	case XML_TEXT_NODE:
	    if (s->next &&
		(s->next->type == XML_CDATA_SECTION_NODE ||
			s->next->type == XML_TEXT_NODE))
		remove_nested_text_cdata(s->next);
	    {
		xmlNode *n, *next;
		int i = 0;

		for (n = node->children; n; n = next) {
		    if (n->type != XML_CDATA_SECTION_NODE &&
				n->type != XML_TEXT_NODE) {
			rc = -1;
			break;
		    }
		    if (!i) {
			next = n->next;
			xmlReplaceNode(s, n);
			xmlFreeNode(s);
			i++;
		    }
		    else {
			add_next_sibling(s, n, &next);
		    }
		    s = n;
		}
		/* replace with an empty node? */
		if (!i && !rc) {
		    xmlUnlinkNode(s);
		    xmlFreeNode(s);
		}
	    }
	    break;

	case XML_NAMESPACE_DECL:
	    {
		xmlNs *ns = (xmlNs *) s, *def;

		/* find the declaration and replace the uri */
		s = (xmlNode *) ns->next;
		for (def = s->nsDef; def && STRCMP(def->prefix, ns->prefix);
				def = def->next)
		    ;

		if (def) {
		    xmlFree((xmlChar *) def->href);
		    def->href = xmlNodeGetContent(node);
		}
		else {
		    rc = -1;
		}
		xmlFreeNs(ns);
	    }
	    break;

	default:
	    rc = -1;
	    break;
	}

	if (rc < 0) {
	    if (multi && cnodes > 1)
		xmlFreeNode(node_replace);
	    break;
	}
    }
    free(ppnodes);
    xmlFree(sel);
    xmlFree(multi);

    return rc;
}

/** remove whitespace text node */
static int delete_ws(xmlNode *ws)
{
    if (ws && xmlIsBlankNode(ws)) {
	xmlUnlinkNode(ws);
	xmlFreeNode(ws);
	return 0;
    }

    return -1;
}

/** testing whether ns def is in use */
static int ns_in_use(xmlNode *node, xmlNs *ns)
{
    xmlAttr *attr;

    if (node->ns == ns)
	return 0;

    for (attr = node->properties; attr; attr = attr->next)
	if (attr->ns == ns)
	    return 0;

    if (node->children)
	return ns_in_use(node->children, ns);

    return -1;
}

/** remove nodes from the document */
int xml_patch_remove(xmlDoc *doc, xmlNode *node)
{
    xmlChar *multi = xmlGetProp(node, (const xmlChar *) "msel"); /* unspecified extension, allows multiselect */
    xmlChar *sel = xmlGetProp(node, (const xmlChar *) "sel");
    xmlChar *ws = xmlGetProp(node, (const xmlChar *) "ws");
    xmlNode **ppnodes = NULL;
    int cnodes;
    xmlNode *s = process_xpath(doc, node, (char *) sel, (char *) multi,
					&ppnodes, &cnodes);
    int rc = s ? 0 : -1, i;

    if (multi && !rc)
	cnodes = remove_nested_nodes(ppnodes, cnodes);

    for (i = 0; rc == 0 && i < cnodes; i++) {
	s = ppnodes[i];

	switch (s->type) {
	case XML_ELEMENT_NODE:
	    /* not allowed to remove the document root element */
	    if (s->parent->type == XML_DOCUMENT_NODE) {
		rc = -1;
		break;
	    }

	case XML_COMMENT_NODE:
	case XML_PI_NODE:
	    if (ws) {
		if (!STRCMP(ws, "before") || !STRCMP(ws, "b") ||
			!STRCMP(ws, "both"))
		    rc = delete_ws(s->prev);

		if (!rc && (!STRCMP(ws, "after") || !STRCMP(ws, "a") ||
				!STRCMP(ws, "both")))
		    rc = delete_ws(s->next);
	    }
	    if (!rc) {
		xmlUnlinkNode(s);
		xmlFreeNode(s);
	    }
	    break;

	case XML_TEXT_NODE:
	case XML_CDATA_SECTION_NODE:
	    remove_nested_text_cdata(s);
	    break;

	case XML_ATTRIBUTE_NODE:
	    xmlUnlinkNode(s);
	    xmlFreeNode(s);
	    break;

	case XML_NAMESPACE_DECL:
	    {
		xmlNs *ns = (xmlNs *) s, **pp;
		rc = -1;

		/* find the declaration */
		s = (xmlNode *) ns->next;
		for (pp = &s->nsDef; *pp && STRCMP(pp[0]->prefix, ns->prefix);
							pp = &(*pp)->next)
		    ;
		if (*pp) {
		    /* remove declaration if ns is not referenced anywhere */
		    ns = *pp;
		    if (!ns_in_use(s, ns))
			break;
		    *pp = pp[0]->next;
		    xmlFreeNs(ns);
		    rc = 0;
		}
		xmlFreeNs((xmlNs *) ppnodes[i]);
		break;
	    }
	    break;

	default:
	    rc = -1;
	    break;
	}
    }
    free(ppnodes);
    xmlFree(multi);
    xmlFree(sel);
    xmlFree(ws);

    return rc;
}

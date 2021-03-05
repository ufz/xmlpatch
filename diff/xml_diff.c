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

#include <ctype.h>
#include <unistd.h>

#include <glib.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "xml_diff.h"

#define STRCMP(x, y) \
  strcmp((char *) x, (char *) y)

#define STRNCMP(x, y, z) \
  strncmp((char *) x, (char *) y, z)

#if DEBUG
 #define TRACE(fmt, args...)		fprintf(stdout, fmt "\n", ##args)
 #define TRACE_ERROR(fmt, args...)	fprintf(stderr, fmt "\n", ##args)
#else
 #define TRACE(fmt, args...)		do {} while (0)
 #define TRACE_ERROR(fmt, args...)	do {} while (0)
#endif

typedef enum
{
    ADD = 0,
    REPLACE,
    DELETE,
    SKIP
} method_e;

typedef enum
{
    INIT = 0,
    BEFORE = 1 << 1,
    AFTER = 1 << 2,
    PREPEND = 1 << 3
} ws_e;

typedef struct _diff_request_s
{
    struct _diff_request_s *next_add;	/* sibling add */

    method_e method;			/* add, remove, replace */
    const xmlNode *node_located;	/* the node to be located */
    const xmlNode *node;		/* added element or attribute */
    const xmlNode *node_deleted;	/* replaced or deleted element */

    xmlElementType type;
    ws_e ws, add_pos;
    int index;
    gchar *pch;
} diff_request_t;

typedef struct _diff_level_s
{
    int *pc;
    int calloc;
} diff_level_t;

typedef enum
{
    NONE   = 0,
    NAME   = 1 << 1,
    VALUE  = 1 << 2,
    NS     = 1 << 3,
    ATTR   = 1 << 4,
    CHILD  = 1 << 5,
    NS_DEC = 1 << 6,
    NS_DEF = 1 << 7,
    TYPE   = 1 << 8,
    MVALUE = 1 << 9
} diff_e;

/** add a new patch request to the glib slist */
static diff_request_t *diff_add_request(method_e method,
                                        const xmlNode *node_located,
                                        ws_e add,
                                        int index,
                                        const void *node,
                                        const void *node_deleted,
                                        const char *pch,
                                        GSList **ppreq)
{
    diff_request_t *p;

    if (ppreq == NULL)
	return NULL;

    p = g_new0(diff_request_t, 1);

    /* if add method and adjacent sibling exists,
     * add a reference to prev patch */
    if (method == ADD) {
	GSList *last = g_slist_last(*ppreq);

	if (last) {
	    diff_request_t *l = last->data;

	    if (l->method == ADD && ((xmlNode *) node)->prev == l->node)
		l->next_add = p;
	}
    }
    p->index = index;
    p->ws = INIT;
    p->add_pos = add;
    p->next_add = NULL;
    p->method = method;
    p->node_located = node_located;
    p->node = (xmlNode *) node;
    p->type = p->node->type;
    p->node_deleted = (xmlNode *) node_deleted;
    p->pch = (gchar *) pch;

    *ppreq = g_slist_append(*ppreq, p);

    return p;
}

/** store elem index at some tree level */
static void add_level(diff_level_t *p, int index, int c)
{
    if (p->calloc <= index) {
	p->calloc = index + 10;
	p->pc = realloc(p->pc, p->calloc * sizeof(int));
    }
    p->pc[index] = c;
}

/** free level */
static void free_levels(diff_level_t *p)
{
    free(p->pc);
    p->pc = NULL;
}

/** get next sibling, skip combinations of text & cdata nodes */
static const xmlNode *get_next(const xmlNode *a)
{
    if (!a)
	return a;

    if (a->type == XML_TEXT_NODE || a->type == XML_CDATA_SECTION_NODE) {
	for (a = a->next; a; a = a->next)
	    if (a->type != XML_TEXT_NODE &&
			a->type != XML_CDATA_SECTION_NODE)
		break;

	return a;
    }
    else {
	return a->next;
    }
}

#if 0
/** get prev sibling, skip combinations of text & cdata nodes*/
static xmlNode *get_prev(xmlNode *a)
{
    if (!a)
	return a;

    if (a->type == XML_TEXT_NODE || a->type == XML_CDATA_SECTION_NODE) {
	for (a = a->prev; a; a = a->prev)
	    if (a->type != XML_TEXT_NODE && a->type != XML_CDATA_SECTION_NODE)
		break;

	return a;
    }
    else {
	return a->prev;
    }
}
#endif

/** last node of this type ? */
static int last_of_this_type(const xmlNode *n)
{
    const xmlNode *next = NULL;

    for (next = get_next(n); next; next = next->next)
	if (next->type == n->type ||
	    (n->type == XML_TEXT_NODE &&
		next->type == XML_CDATA_SECTION_NODE) ||
	    (next->type == XML_TEXT_NODE &&
		n->type == XML_CDATA_SECTION_NODE))
	    break;

    return next ? FALSE : TRUE;
}

/** get positional selector value */
static gchar *get_sel(const xmlNode *parent, diff_level_t *p, int index,
                      const xmlNode *n, int last)
{
    int i = 0, *pc = p->pc ? &p->pc[1] : NULL, j, count = index;

    const xmlNode **nodes = g_malloc(count * sizeof(*nodes));
    const xmlNode *node = parent;
    gchar *t = NULL, *pch = NULL;

    for (i = 0; node && node->type != XML_DOCUMENT_NODE; node = node->parent) {
	if (i >= count) {
	    count += 10;
	    nodes = g_realloc(nodes, count * sizeof(*nodes));
	}

	nodes[i++] = node;
    }
    if (i != index)
	return NULL;

    for (j = i - 1, i = 0; i < index; i++, j--) {
	if (pc[i] == 1 && last_of_this_type(nodes[j]))
	    pc[i] = 0;

	t = pch;
	pch = pc[i] ? g_strdup_printf("%s/*[%d]", t ? t : "", pc[i]) :
		      g_strdup_printf("%s%s*", t ? t : "", t ? "/" : "");
	g_free(t);
    }

    if (n && last != 0) {
	t = pch;

	if (last == 1 && last_of_this_type(n))
	    last = 0;

	if (n->type == XML_ELEMENT_NODE)
	    pch = last ? g_strdup_printf("%s/*[%d]", t ? t : "", last) :
			 g_strdup_printf("%s%s*", t ? t : "", t ? "/" : "");
	else if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE)
	    pch = last ? g_strdup_printf("%s/text()[%d]", t ? t : "", last) :
			 g_strdup_printf("%s/text()", t ? t : "");
	else if (n->type == XML_COMMENT_NODE)
	    pch = last ? g_strdup_printf("%s/comment()[%d]", t ? t : "", last) :
			 g_strdup_printf("%s/comment()", t ? t : "");
	else if (n->type == XML_PI_NODE)
	    pch = last ? g_strdup_printf("%s/processing-instruction('%s')[%d]",
					 t ? t : "", n->name, last) :
			 g_strdup_printf("%s/processing-instruction('%s')",
					 t ? t : "", n->name);
	else
	    t = NULL;
	g_free(t);
    }
    g_free(nodes);

    return pch;
}

#define CMP_STR(a, b)			\
    if (a == NULL && b == NULL) {	\
	return 0;			\
    } else if (a && b == NULL) {	\
	return -1;			\
    } else if (a == NULL && b) {	\
	return 1;			\
    } else {				\
	int rc;				\
	if ((rc = STRCMP(a, b)))	\
	    return rc;			\
    }

static int diff_cmp_nodes(const xmlNode *a, const xmlNode *b, diff_e *diff);

/** compare attributes & values, unordered comparison */
static int diff_cmp_attr(const xmlAttr *attr_a, const xmlAttr *attr_b,
                         const xmlNode *located, int c, int ind,
                         diff_level_t *level, GSList **ppreq)
{
    const xmlAttr *a, *b;
    int rc = 0, rc_single;
    GSList *list = NULL;

    for (b = attr_b; b; b = b->next ) {
	diff_e d;

	for (a = attr_a; a; a = a->next) {
	    rc_single = diff_cmp_nodes((const xmlNode *) a,
				       (const xmlNode *) b, &d);

	    if (!rc_single) {
		list = g_slist_append(list, (gpointer) a);
		break;
	    }
	    else if (d == VALUE) {
		list = g_slist_append(list, (gpointer) a);

		if (located)
		    diff_add_request(REPLACE, located, INIT, c, b, a,
				     get_sel(located, level, ind, NULL, 0),
				     ppreq);
		rc++;
		break;
	    }
	}
	if (a == NULL) {
	    rc++;

	    if (located)
		diff_add_request(ADD, located, INIT, c, b, NULL,
				 get_sel(located, level, ind, NULL, 0),
				 ppreq);
	}
    }
    for (a = attr_a; a; a = a->next)
	if (!g_slist_find(list, a)) {
	    rc++;

	    if (located) /* DELETE attr_a */
		diff_add_request(DELETE, located, INIT, c, a, a,
				 get_sel(located, level, ind, NULL, 0),
				 ppreq);
	}

    g_slist_free(list);

    return rc;
}

/** ns declarations, unordered collection */
static int diff_cmp_ns(const xmlNs *ns_a, const xmlNs *ns_b,
                       const xmlNode *located, int c,
                       int ind, diff_level_t *level, GSList **ppreq)
{
    const xmlNs *a, *b;
    int rc = 0;
    GSList *list = NULL;

    for (b = ns_b; b; b = b->next ) {
	for (a = ns_a; a; a = a->next) {
	    if ((!a->prefix && !b->prefix) ||
		(a->prefix && b->prefix && !STRCMP(a->prefix, b->prefix))) {
		if (a->href && b->href && !STRCMP(a->href, b->href)) {
		    list = g_slist_append(list, (gpointer) a);
		    break;
		}
		else {
		    list = g_slist_append(list, (gpointer) a);

		    if (located)
			diff_add_request(REPLACE, located, INIT, c, b, a,
					 get_sel(located, level, ind, NULL, 0),
					 ppreq);
		    rc++;
		    break;
		}
	    }
	}
	if (!a) {
	    rc++;

	    if (located)
		diff_add_request(ADD, located, INIT, c, b, NULL,
				 get_sel(located, level, ind, NULL, 0),
				 ppreq);
	}
    }
    for (a = ns_a; a; a = a->next)
	if (!g_slist_find(list, a)) {
	    rc++;

	    if (located) /* DELETE ns_a */
		diff_add_request(DELETE, located, INIT, c, a, a,
				 get_sel(located, level, ind, NULL, 0),
				 ppreq);
	}

    g_slist_free(list);

    return rc;
}

/** simple node comparison */
static int diff_cmp_nodes(const xmlNode *a, const xmlNode *b, diff_e *diff)
{
    int rc, rc_ns;
    const xmlChar *pa, *pb;

    *diff = NONE;

    if (a && b == NULL)
	return -1;
    else if (a == NULL && b)
	return 1;
    else if (a == NULL && b == NULL)
	return 0;

    if (a->type != b->type) {
	*diff = TYPE;
	return 1;
    }

#define CMP(x, y, d)			\
    if (!x && !y) {			\
	;				\
    }					\
    else if (x && !y) {			\
	*diff = d;			\
	return -1;			\
    }					\
    else if (!x && y) {			\
	*diff = d;			\
	return 1;			\
    }					\
    else if ((rc = STRCMP(x, y))) {	\
	*diff = d;			\
	return rc;			\
    }

    CMP(a->name, b->name, NAME);
    pa = a->ns ? a->ns->prefix : NULL;
    pb = b->ns ? b->ns->prefix : NULL;
    CMP(pa, pb, NS);

    if (a->type == XML_ATTRIBUTE_NODE) {
	const unsigned char *ca = a->children ? a->children->content : NULL;
	const unsigned char *cb = b->children ? b->children->content : NULL;

	CMP(ca, cb, VALUE);
	return 0;
    }
    else if (a->type == XML_TEXT_NODE || a->type == XML_CDATA_SECTION_NODE) {
	const xmlNode *na, *nb;

	CMP(a->content, b->content, VALUE);

	for (na = a->next, nb = b->next; na || nb;
		na = na->next, nb = nb->next) {

	    if ((na == NULL || (na->type != XML_CDATA_SECTION_NODE &&
					na->type != XML_TEXT_NODE)) &&
		 (nb == NULL || (nb->type != XML_CDATA_SECTION_NODE &&
					nb->type != XML_TEXT_NODE)))
		return 0;

	    if (na == NULL || nb == NULL || na->type != nb->type) {
		*diff = MVALUE;
		return 1;
	    }

	    CMP(na->content, nb->content, MVALUE);
	}

	return 0;
    }
    else if (a->type == XML_COMMENT_NODE || a->type == XML_PI_NODE) {
	CMP(a->content, b->content, VALUE);
	return 0;
    }

    rc = diff_cmp_attr(a->properties, b->properties, 0, 0, 0, NULL, NULL);
    if (rc)
	*diff |= ATTR;

    rc_ns = diff_cmp_ns(a->nsDef, b->nsDef, 0, 0, 0, NULL, NULL);
    if (rc_ns)
	*diff |= NS_DEC;

    return rc + rc_ns;
}
#undef CMP

/** remove list entries */
static void remove_list_end(GSList *last, GSList **ppreq)
{
    GSList *l = last ? last->next : *ppreq;

    for ( ; l; l = l->next) {
	diff_request_t *p = l->data;

	g_free(p->pch);
	g_free(p);
    }

    if (last) {
	g_slist_free(last->next);
	last->next = NULL;
    }
    else {
	g_slist_free(*ppreq);
	*ppreq = NULL;
    }
}

#define COUNT(a)							\
    a->type == XML_ELEMENT_NODE ? elem_count :				\
    (a->type == XML_TEXT_NODE || a->type == XML_CDATA_SECTION_NODE) ?	\
			text_count :					\
    a->type == XML_COMMENT_NODE ? comm_count :				\
    a->type == XML_PI_NODE ? pi_count : 0

#define UPDATE(b)			\
  text_node = FALSE;			\
  if (b) {				\
    switch (b->type) {			\
    case XML_ELEMENT_NODE:		\
	elem_count++;			\
	break;				\
					\
    case XML_TEXT_NODE:			\
    case XML_CDATA_SECTION_NODE:	\
	text_node = TRUE;		\
	text_count++;			\
	break;				\
					\
    case XML_PI_NODE:			\
	pi_count++;			\
	break;				\
					\
    case XML_COMMENT_NODE:		\
	comm_count++;			\
	break;				\
					\
    default:				\
	break;				\
    }					\
  }

/** very simple and stupid comparison logic of the tree */
static int diff_cmp_steps_children(int optimize,
                                   const xmlNode *from,
                                   const xmlNode *to,
                                   diff_e *diff,
                                   int ind,
                                   diff_level_t *level,
                                   GSList **ppreq,
                                   int *changes)
{
    int rc;
    const xmlNode *a, *b;
    int elem_count, pi_count, comm_count, text_count, text_node = FALSE;
    GSList *last = optimize == XML_DIFF_SIZE_OPTIMIZE ?
		g_slist_last(*ppreq) : NULL;
    int init_count = optimize == XML_DIFF_SIZE_OPTIMIZE ?
		g_slist_length(*ppreq) : 0;

    elem_count = pi_count = comm_count = text_count = 1;

    add_level(level, ++ind, elem_count);

    for (a = from->children, b = to->children; a || b; ) {
	diff_e d = { 0 };

	rc = diff_cmp_nodes(a, b, &d);

	if (rc) {
	    *diff |= CHILD;

	    if (a && b && d == VALUE) {
		gchar *pch = get_sel(a->parent, level, ind - 1, a, COUNT(a));

		/* never an element type */
		if (a->type == XML_TEXT_NODE && b->type == XML_TEXT_NODE &&
			!STRNCMP(a->content, b->content,
				 strlen((char *) a->content)))
		    diff_add_request(ADD, a, AFTER, COUNT(a), b, a,
				     pch, ppreq);
		else
		    /* replace node_a content with node_b content */
		    diff_add_request(REPLACE, a, INIT, COUNT(a), b, a,
				     pch, ppreq);

		UPDATE(b)
	    }
	    else if (a && b && d == MVALUE) {
		text_node = TRUE;
		/* never an element type
		 * replace node_a content with node_b content */
		diff_add_request(REPLACE, a, INIT, text_count, b, a,
				 get_sel(a->parent, level, ind - 1,
					 a, text_count), ppreq);
		text_count++;
	    }
	    else if (a && b && (((d == ATTR && (rc == 1 || ind <= 1)) ||
				d == NS_DEC || d == (ATTR | NS_DEC)) &&
				((a->children && b->children) ||
				 (!a->children && !b->children)))) {
		GSList *last_attr = optimize == XML_DIFF_SIZE_OPTIMIZE ?
				g_slist_last(last ? last : *ppreq) : NULL;
		text_node = FALSE;

		add_level(level, ind, elem_count);
		/* attribute changes, a & b elements */
		if ((d & ATTR) == ATTR)
		    diff_cmp_attr(a->properties, b->properties, a, COUNT(a),
				  ind, level, ppreq);
		else /* ns declaration changes */
		    diff_cmp_ns(a->nsDef, b->nsDef, a, COUNT(a),
				ind, level, ppreq);

		rc = diff_cmp_steps_children(optimize, a, b, &d,
					     ind, level, ppreq, changes);
		if (rc)
		    *diff |= CHILD;

		if (*changes) {
		    remove_list_end(last_attr, ppreq);

		    diff_add_request(REPLACE, a, INIT, COUNT(a), b, a,
				     get_sel(a->parent, level,
					     ind - 1, a, COUNT(a)),
				     ppreq);
		}
		elem_count++;
	    }
	    else {
		int i = 0, fadd = b ? TRUE : FALSE;
		const xmlNode *n;

		/* update in the root elements ? */
		if (a && a->parent->type == XML_DOCUMENT_NODE && b) {
		    diff_add_request(REPLACE, a, INIT, COUNT(a), b, a,
				     get_sel(a->parent, level,
					     ind - 1, a, COUNT(a)),
				     ppreq);
		    UPDATE(b)
		    a = get_next(a);
		    b = get_next(b);
		    continue;
		}
		for (n = get_next(a); n; n = get_next(n)) {
		    if (i < 6 && !diff_cmp_nodes(n, b, &d)) {
			fadd = FALSE;
			break;
		    }
		    i++;
		}
#if 0
		if (REPLACE CASE) {
		    diff_add_request(REPLACE, a, INIT, COUNT(a), b, a,
				     get_sel(a->parent, level,
					     ind - 1, a, COUNT(a)),
				     ppreq);
		    UPDATE(b)
		    a = get_next(a);
		    b = get_next(b);
		}
#endif
		if (fadd) {
		    /* adding b */

		    if (a)
			diff_add_request(ADD, a, BEFORE, COUNT(a), b, NULL,
					 get_sel(a->parent, level,
						 ind - 1, a, COUNT(a)),
					 ppreq);
		    else
			diff_add_request(ADD, from, INIT, 0, b, NULL,
					 get_sel(from, level, ind - 1, b, 0),
					 ppreq);
		    UPDATE(b)
		    b = get_next(b);
		}
		else if (a) {
		    /**
		     * delete node A
		     * nasty things will happen if incorrect order with delete,
		     * i.e. text-nodes combine if a node existing betweeen of
		     * them, is being deleted
		     */
		    if (text_node && a->next &&
				(a->next->type == XML_TEXT_NODE ||
				 a->next->type == XML_CDATA_SECTION_NODE)) {
			if (xmlIsBlankNode(a->next)) {
			    diff_add_request(DELETE, a, INIT, COUNT(a), a, a,
					     get_sel(a->parent, level,
						     ind - 1, a, COUNT(a)),
					     ppreq)->ws = AFTER;
			    a = get_next(a);
			}
			else {
			    const xmlNode *s = a;

			    a = get_next(a);
			    diff_add_request(DELETE, a, INIT, COUNT(a), a, a,
					     get_sel(a->parent, level,
						     ind - 1, a, COUNT(a)),
					     ppreq);

			    diff_add_request(DELETE, s, INIT, COUNT(s), s, s,
					     get_sel(s->parent, level,
						     ind - 1, s, COUNT(s)),
					     ppreq);
			}
		    }
		    else {
			diff_add_request(DELETE, a, INIT, COUNT(a), a, a,
					 get_sel(a->parent, level,
						 ind - 1, a, COUNT(a)),
					 ppreq);
		    }
		    a = get_next(a);
		}
		continue;
	    }
	}
	else if (a && a->type == XML_ELEMENT_NODE &&
			b && b->type == XML_ELEMENT_NODE) {
	    text_node = FALSE;

	    add_level(level, ind, elem_count);
	    rc = diff_cmp_steps_children(optimize, a, b, &d,
					 ind, level, ppreq, changes);
	    if (rc)
		*diff |= CHILD;

	    if (*changes)
		diff_add_request(REPLACE, a, INIT, COUNT(a), b, a,
				 get_sel(a->parent, level,
					 ind - 1, a, COUNT(a)),
				 ppreq);
	    elem_count++;
	}
	else if (!rc) {
	    UPDATE(b)
	}

	a = get_next(a);
	b = get_next(b);
    }

    /* try size optimization, this is ugly but works */
    *changes = FALSE;

    if (optimize == XML_DIFF_SIZE_OPTIMIZE) {
	init_count = g_slist_length(*ppreq) - init_count;

	if (ind > 1 && init_count > 1) {
	    int cb = 0;
	    GSList *l = last ? last->next : *ppreq;
	    xmlBufferPtr buf;

	    for ( ; l; l = l->next) {
		diff_request_t *p = l->data;

		if (p->method == ADD || p->method == REPLACE) {
		    buf = xmlBufferCreate();
		    xmlNodeDump(buf, p->node->doc, (xmlNode *) p->node, 0, 0);
		    cb += buf->use + strlen(p->pch);
		    xmlBufferFree(buf);
		}
		else if (p->method == DELETE) {
		    cb += strlen(p->pch);
		}
	    }

	    buf = xmlBufferCreate();
	    xmlNodeDump(buf, to->doc, (xmlNode *) to, 0, 0);

	    if (cb > buf->use) {
		remove_list_end(last, ppreq);
		*changes = TRUE;
	    }
	    xmlBufferFree(buf);
	}
    }

    return *diff == NONE ? 0 : 1;
}

/** if last step has a qualified attribute, check ns declaration existence */
static xmlNs *attr_has_ns(xmlAttr *attr, xmlNode *changes)
{
    xmlNs *ns, **nslist;
    int i;

    if (attr->ns == NULL)
	return NULL;

    nslist = xmlGetNsList(changes->doc, changes);

    for (i = 0; (ns = nslist ? nslist[i] : NULL); i++)
	if (!STRCMP(ns->href, attr->ns->href) && ns->prefix)
	    break;

    if (ns == NULL) {
	int j;
	char *pch = NULL;

	for (j = 0; ; j++) {
	    int k;

	    pch = j ? g_strdup_printf("%s%d", attr->ns->prefix, j - 1) :
		      g_strdup((char *) attr->ns->prefix);

	    /* prefixes within the in-scope nslist */
	    for (k = 0; (ns = nslist ? nslist[k] : NULL); k++)
		if (ns->prefix && !STRCMP(pch, ns->prefix))
		    break;

	    if (ns) {
		g_free(pch);
		continue;
	    }
	    break;
	}
	ns = xmlNewNs(changes, attr->ns->href, (xmlChar *) pch);
	g_free(pch);
    }
    xmlFree(nslist);

    return ns;
}

/** replaces old ns with new for node and all of it's children */
static void set_ns_recursively(xmlNode *node,
                               const xmlNs *old, xmlNs *ns)
{
    for ( ; node; node = node->next) {
	if (node->type == XML_ELEMENT_NODE) {
	    if (node->ns == old)
		node->ns = ns;

	    if (node->children)
		set_ns_recursively(node->children, old, ns);

	    if (node->properties)
		set_ns_recursively((xmlNode *) node->properties, old, ns);
	}
	else if (node->type == XML_ATTRIBUTE_NODE) {
	    xmlAttr *attr = (xmlAttr *) node;

	    if (attr->ns == old)
		attr->ns = ns;
	}
    }
}

/** find's all ns declarations */
static GSList *find_all_ns(xmlNode *node, GSList *list)
{
    for ( ; node; node = node->next) {
	if (node->type == XML_ELEMENT_NODE) {
	    xmlNs *ns = node->nsDef;

	    for ( ; ns; ns = ns->next)
		list = g_slist_append(list, ns);

	    list = find_all_ns(node, list);
	}
    }

    return list;
}

/** change prefix to unused one */
static void change_prefix(xmlNode *node, xmlNs *change)
{
    GSList *t, *list = find_all_ns(node, NULL);
    gchar *pch;
    int i;

    /* change prefix of the diff "body" */
    for (i = 'a'; i < 'z'; i++) {
	pch = g_strdup_printf("%c", i);

	for (t = list; t; t = t->next) {
	    xmlNs *ns = t->data;

	    if (ns->prefix && !STRCMP(ns->prefix, pch))
		break;
	}
	if (t == NULL)
	    break;
	g_free(pch);
    }
    for (i = 0; t; i++) {
	pch = g_strdup_printf("n%d", i);

	for (t = list; t; t = t->next) {
	    xmlNs *ns = t->data;

	    if (ns->prefix && !STRCMP(ns->prefix, pch))
		break;
	}
	if (t == NULL)
	    break;
	g_free(pch);
    }
    xmlFree((xmlChar *) change->prefix);
    change->prefix = xmlMalloc(strlen(pch) + 1);
    strcpy((char *) change->prefix, pch);
    g_free(pch);

    g_slist_free(list);
}

/** add an element and apply ns-changes */
static xmlNode *add_element(const xmlNode *node,	/* new added elem */
                            xmlNode *changes,		/* diff-context */
                            xmlNode *add)
{
    xmlNs **pns;
    xmlNode *n = xmlCopyNode((xmlNode *) node, 1);

    if (node->parent && node->parent->type == XML_DOCUMENT_NODE)
	return n;

    /* remove possible additional ns declarations from <n>
     * generated with the above copy */
    for (pns = &n->nsDef; *pns; ) {
	xmlNs *ns = node->nsDef, *nsnew;

	for ( ; ns && ns->prefix != pns[0]->prefix; ns = ns->next)
	    ;

	if (ns) {
	    pns = &(*pns)->next;
	    continue;
	}

	nsnew = xmlSearchNs(changes->doc, changes, pns[0]->prefix);

	if (nsnew == NULL) {
	    nsnew = xmlNewNs(changes, pns[0]->href, pns[0]->prefix);
	}
	else if (STRCMP(pns[0]->href, nsnew->href)) {
	    /* overlapping definition ? */
	    int i, j = 0;
	    gchar *pch = NULL;
	    /* check that only a single in-scope ns declaration exists */
	    xmlNs **nslist = xmlGetNsList(node->doc, node->parent);

	    for (i = 0; (ns = nslist ? nslist[i] : NULL); i++)
		if (!STRCMP(nsnew->href, ns->href))
		    j++;

	    xmlFree(nslist);
	    /* add ns declaration to <add> element */
	    if (j > 1) {
		/* has <add> this same prefix ? */
		if (add->ns && !STRCMP(add->ns->prefix, pns[0]->prefix))
		    change_prefix(changes, add->ns);

		nsnew = xmlNewNs(changes, pns[0]->href, pns[0]->prefix);
	    }
	    else {
		/* change prefix of the diff "body" */
		for (i = 'a'; i < 'z'; i++) {
		    pch = g_strdup_printf("%c", i);

		    if (!(nsnew = xmlSearchNs(changes->doc,
					      changes, (xmlChar *) pch)))
			break;
		    g_free(pch), pch = NULL;
		}

		if (nsnew) {
		    for (i = 0; ; i++) {
			pch = g_strdup_printf("n%d", i);

			if (!xmlSearchNs(changes->doc,
					 changes, (xmlChar *) pch))
			    break;
			g_free(pch), pch = NULL;
		    }
		}
		/* and add this declaration to diff element */
		nsnew = xmlNewNs(changes, pns[0]->href, (xmlChar *) pch);
		g_free(pch);
	    }
	}
	/* update references accordingly */
	set_ns_recursively(n, *pns, nsnew);
	nsnew = *pns;
	*pns = (*pns)->next;
	xmlFreeNs(nsnew);
    }

    return n;
}

/** add sibling node types */
static void add_node(diff_request_t *p, xmlNode *add, xmlNode *changes)
{
    if (p->type == XML_ELEMENT_NODE) {
	xmlAddChild(add, add_element(p->node, changes, add));
    }
    else if (p->type == XML_TEXT_NODE) {
	if (p->add_pos == AFTER &&
		p->node_deleted && p->node_deleted->type == XML_TEXT_NODE) {
	    int c = p->node_deleted->content ?
			strlen((char *) p->node_deleted->content) : 0;

	    if (p->node->content && strlen((char *) p->node->content) >= c)
		xmlNodeAddContent(add, p->node->content + c);
	}
	else {
	    xmlNodeAddContent(add, p->node->content ?
				   p->node->content : (const xmlChar *) "");
	}
    }
    else if (p->type == XML_CDATA_SECTION_NODE) {
	xmlAddChild(add, xmlNewCDataBlock(add->doc,
					  p->node->content,
					  strlen((char *) p->node->content)));
    }
    else if (p->type == XML_COMMENT_NODE) {
	xmlAddChild(add, xmlNewComment(p->node->content));
    }
    else if (p->type == XML_PI_NODE) {
	xmlAddChild(add, xmlNewPI(p->node->name, p->node->content));
    }

    if (p->type == XML_CDATA_SECTION_NODE || p->type == XML_TEXT_NODE) {
	xmlNode *next = p->node->next;

	for ( ; next; next = next->next) {
	    if (next->type == XML_CDATA_SECTION_NODE)
		xmlAddChild(add,
			    xmlNewCDataBlock(add->doc,
					     next->content,
					     strlen((char *) next->content)));
	    else if (next->type == XML_TEXT_NODE)
		xmlNodeAddContent(add, next->content ?
				  next->content : (const xmlChar *) "");
	    else
		break;
	}
    }
}

/** add/replace opers */
static void add_patch_oper(xmlNode *changes, xmlNode *add, int type)
{
    if (type == XML_DIFF_WHITESPACE)
	xmlAddChild(changes, xmlNewText((const xmlChar *) "\n  "));

    xmlAddChild(changes, add);
}

/** process diffing */
static int diff_process(const xmlDoc *a,
                        const xmlDoc *b,
                        diff_e *diff,
                        int shrink,
                        xmlNode *changes)
{
    GSList *req = NULL, *r;
    int cb_child = 0;
    diff_level_t level = { NULL, 0 };

    if (a == NULL || b == NULL) {
	TRACE_ERROR("ERROR: nodes not valid");
	return -1;
    }

    if (changes == NULL) {
	TRACE_ERROR("ERROR: not any node where to add patch operations !");
	return -1;
    }

    add_level(&level, 0, 0);
    diff_cmp_steps_children(shrink, (const xmlNode *) a, (const xmlNode *) b,
			    diff, 0, &level, &req, &cb_child);
    free_levels(&level);

#if DEBUG
    TRACE("OLD doc:");
    xmlDocFormatDump(stdout, a, 1);
    TRACE("\nNEW doc:");
    xmlDocFormatDump(stdout, b, 1);
    TRACE("\nCHANGES:");
#endif

    for (r = req; r; r = r->next) {
	diff_request_t *p = r->data;
	xmlNode *add = NULL;

	switch (p->method) {
	case SKIP:
	    break;

	case DELETE:
	    add = xmlNewNode(changes->ns, (const xmlChar *) "remove");
	    if (!shrink)
		xmlAddChild(changes, xmlNewText((const xmlChar *) "\n  "));
	    xmlAddChild(changes, add);
	    break;

	case ADD:
	    if (p->type == XML_ATTRIBUTE_NODE) {
		xmlAttr *attr = (xmlAttr *) p->node;

		add = xmlNewNode(changes->ns, (const xmlChar *) "add");
		xmlNodeAddContent(add, attr->children ?
				attr->children->content : (const xmlChar *) "");
		add_patch_oper(changes, add, shrink);
		break;
	    }
	    else if (p->type == XML_NAMESPACE_DECL) {
		xmlNs *ns = (xmlNs *) p->node;

		add = xmlNewNode(changes->ns, (const xmlChar *) "add");
		xmlNodeAddContent(add, ns ? ns->href : (const xmlChar *) "");
		add_patch_oper(changes, add, shrink);
		break;
	    }
	    add = xmlNewNode(changes->ns, (const xmlChar *) "add");
	    add_node(p, add, changes);
	    add_patch_oper(changes, add, shrink);
	    {
		diff_request_t *next = p->next_add;

		for ( ; next; next = next->next_add) {
		    add_node(next, add, changes);
		    next->method = SKIP;
		}
	    }
	    break;

	case REPLACE:
	    add = xmlNewNode(changes->ns, (const xmlChar *) "replace");

	    if (p->type == XML_ELEMENT_NODE ||
			p->type == XML_COMMENT_NODE ||
			p->type == XML_TEXT_NODE ||
			p->type == XML_CDATA_SECTION_NODE ||
			p->type == XML_PI_NODE) {
		add_node(p, add, changes);
	    }
	    else if (p->type == XML_ATTRIBUTE_NODE) {
		xmlAttr *attr = (xmlAttr *) p->node;

		xmlNodeAddContent(add, attr->children ?
				attr->children->content : (const xmlChar*) "");
	    }
	    else if (p->type == XML_NAMESPACE_DECL) {
		xmlNs *ns = (xmlNs *) p->node;

		xmlNodeAddContent(add, ns->href ? ns->href :
						(const xmlChar *) "");
	    }
	    add_patch_oper(changes, add, shrink);
	    break;
	}

	if (p->method != SKIP) {
	    if (p->type == XML_ATTRIBUTE_NODE && (p->method == DELETE ||
							p->method == REPLACE)) {
		xmlAttr *attr = (xmlAttr *) p->node_deleted;
		xmlNs *ns = attr_has_ns(attr, changes);
		char *pch = p->pch;

		p->pch = g_strdup_printf("%s/@%s%s%s", pch,
					 ns ? (char *) ns->prefix : "",
					 ns ? ":" : "", attr->name);
		g_free(pch);
	    }
	    else if (p->type == XML_NAMESPACE_DECL && (p->method == DELETE ||
							p->method == REPLACE)) {
		xmlNs *ns = (xmlNs *) p->node_deleted;
		char *pch = p->pch;

		p->pch = g_strdup_printf("%s/namespace::%s", pch,
					 ns ? (char *) ns->prefix : "");
		g_free(pch);
	    }

	    if (add) {
		xmlSetProp(add, (const xmlChar *) "sel",
			   (const xmlChar *) p->pch);

		if (p->add_pos != INIT)
		    xmlSetProp(add, (const xmlChar *) "pos",
					p->add_pos == AFTER ?
						(const xmlChar *) "after" :
						(const xmlChar *) "before");

		if (p->method == ADD && p->type == XML_ATTRIBUTE_NODE) {
		    xmlAttr *attr = (xmlAttr *) p->node;
		    xmlNs *ns = attr_has_ns(attr, changes);

		    char *pch = g_strdup_printf("@%s%s%s",
						ns ? (char *) ns->prefix : "",
						ns ? ":" : "",
						(char *) attr->name);

		    xmlSetProp(add, (const xmlChar *) "type",
					(const xmlChar *) pch);
		    g_free(pch);
		}
		else if (p->method == ADD && p->type == XML_NAMESPACE_DECL) {
		    xmlNs *ns = (xmlNs *) p->node;

		    char *pch = g_strdup_printf("namespace::%s",
						ns ? (char *) ns->prefix : "");
		    xmlSetProp(add, (const xmlChar *) "type",
					(const xmlChar *) pch);
		    g_free(pch);
		}
		else if (p->method == DELETE && p->ws != INIT) {
		    if ((p->ws & BEFORE) == BEFORE && (p->ws & AFTER) == AFTER)
			xmlSetProp(add, (const xmlChar *) "ws",
					(const xmlChar *) "both");
		    else if ((p->ws & BEFORE) == BEFORE)
			xmlSetProp(add, (const xmlChar *) "ws",
					(const xmlChar *) "before");
		    else
			xmlSetProp(add, (const xmlChar *) "ws",
					(const xmlChar *) "after");
		}
	    }
	}
	g_free(p->pch);
	g_free(p);
    }
    if (req)
	g_slist_free(req);

    return *diff == NONE ? 0 : 1;
}

/** public interface to diff */
int xml_exec_diff(const xmlDoc *prev,	/**< old doc */
                  const xmlDoc *new,	/**< new doc */
                  int shrink,		/**< try to shorten payload */
                  xmlNode *node)	/**< node to append patches */
{
    diff_e diff = NONE;
    int rc;

    rc = diff_process(prev, new, &diff, shrink, node);
    if (rc < 0)
	TRACE_ERROR("diff document generation failed");

    return rc;
}

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

#ifndef XML_DIFF_H
#define XML_DIFF_H

#ifdef __cplusplus
extern "C" {
#endif

#define XML_DIFF_WHITESPACE	0
#define XML_DIFF_NO_WHITESPACE	1
#define XML_DIFF_SIZE_OPTIMIZE	2

int xml_exec_diff(const xmlDoc *prev,	/* old doc */
                  const xmlDoc *new,	/* new doc */
                  int fShort,		/* try to shorten payload */
                  xmlNode *node);	/* a node where patches are appended */

#ifdef __cplusplus
}
#endif

#endif /* XML_DIFF_H */

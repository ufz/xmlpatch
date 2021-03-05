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

#ifndef XML_PATCH_H
#define XML_PATCH_H

#ifdef __cplusplus
extern "C" {
#endif

int xml_patch_add(xmlDoc *doc, xmlNode *node);
int xml_patch_remove(xmlDoc *doc, xmlNode *node);
int xml_patch_replace(xmlDoc *doc, xmlNode *node);

#ifdef __cplusplus
}
#endif

#endif /* XML_PATCH_H */

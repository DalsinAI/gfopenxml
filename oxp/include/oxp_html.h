/* OXP's HTML reader: everyday HTML (as chat lines, news pages and help text
 * carry it) read into OXP's small tree (oxp_dom.h), forgiving as browsers
 * are, for programs that need HTML and no more than that. Not an HTML5
 * parser: it knows the void elements, the raw-text elements, the common
 * implied ends (p, li, dd, dt, option, tr, td, th) and the named entities a
 * page of text uses, and it never fails on bad markup.
 * MIT licence. Copyright (c) 2026 AmiCradle Team.
 *
 * The tree:
 *  - its root element is always html; what is outside head goes in a body,
 *    made when the page has none;
 *  - element and attribute names are lower case, with no namespace (a name
 *    with characters XML names can't hold loses them, an attribute that is
 *    left with none is dropped); the first of two same-named attributes wins;
 *  - an end tag closes the nearest open element of its name, and those
 *    inside it; one with nothing open of its name is ignored;
 *  - text is UTF-8 with the entities replaced; a byte that is not UTF-8 is
 *    read as Latin-1, and characters XML can't hold are dropped;
 *  - comments, the doctype and processing instructions are left out.
 */
#ifndef OXP_HTML_H
#define OXP_HTML_H

#include "oxp_dom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The tree of len bytes of HTML; NULL only when memory runs out.
 * Free it with oxp_dom_free(). */
oxp_doc *oxp_html_parse(const char *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif

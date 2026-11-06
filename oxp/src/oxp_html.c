/* OXP's HTML reader (oxp_html.h). Written from the HTML 4.01 element list
 * and the XML 1.0 name and character rules only.
 * MIT licence. Copyright (c) 2026 AmiCradle Team. */
#include "oxp_html.h"

#include <stdlib.h>
#include <string.h>

#define MAX_OPEN 256                 /* deeper elements are not opened */
#define MAX_NAME 64

typedef struct {
    char *s;
    size_t n, cap;
    int oom;
} buf;

static void put(buf *b, const char *s, size_t n)
{
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        char *t;
        while (b->n + n + 1 > cap) cap *= 2;
        if (!(t = realloc(b->s, cap))) { b->oom = 1; return; }
        b->s = t; b->cap = cap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

/* One code point as UTF-8, if XML can hold it. */
static void put_cp(buf *b, unsigned long c)
{
    char u[4];
    if (!(c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xd7ff) ||
          (c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff)))
        return;
    if (c < 0x80) { u[0] = (char)c; put(b, u, 1); }
    else if (c < 0x800) { u[0] = (char)(0xc0 | c >> 6); u[1] = (char)(0x80 | (c & 63)); put(b, u, 2); }
    else if (c < 0x10000) {
        u[0] = (char)(0xe0 | c >> 12); u[1] = (char)(0x80 | (c >> 6 & 63)); u[2] = (char)(0x80 | (c & 63));
        put(b, u, 3);
    } else {
        u[0] = (char)(0xf0 | c >> 18); u[1] = (char)(0x80 | (c >> 12 & 63));
        u[2] = (char)(0x80 | (c >> 6 & 63)); u[3] = (char)(0x80 | (c & 63));
        put(b, u, 4);
    }
}

/* The code point at *p (UTF-8, or one Latin-1 byte when it is not). */
static unsigned long next_cp(const unsigned char **p, const unsigned char *end)
{
    const unsigned char *s = *p;
    unsigned long c = *s, min;
    unsigned n, i;
    if (c < 0x80) { *p = s + 1; return c; }
    if (c >= 0xc2 && c <= 0xdf) { n = 1; c &= 31; min = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { n = 2; c &= 15; min = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { n = 3; c &= 7; min = 0x10000; }
    else { *p = s + 1; return c; }
    if ((size_t)(end - s) <= n) { *p = s + 1; return *s; }
    for (i = 1; i <= n; i++) {
        if ((s[i] & 0xc0) != 0x80) { *p = s + 1; return *s; }
        c = c << 6 | (s[i] & 63);
    }
    if (c < min) { *p = s + 1; return *s; }
    *p = s + n + 1;
    return c;
}

static const struct { const char *name; unsigned short cp; } entities[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
    { "nbsp", 0xa0 }, { "iexcl", 0xa1 }, { "cent", 0xa2 }, { "pound", 0xa3 }, { "curren", 0xa4 },
    { "yen", 0xa5 }, { "brvbar", 0xa6 }, { "sect", 0xa7 }, { "uml", 0xa8 }, { "copy", 0xa9 },
    { "ordf", 0xaa }, { "laquo", 0xab }, { "not", 0xac }, { "shy", 0xad }, { "reg", 0xae },
    { "macr", 0xaf }, { "deg", 0xb0 }, { "plusmn", 0xb1 }, { "sup2", 0xb2 }, { "sup3", 0xb3 },
    { "acute", 0xb4 }, { "micro", 0xb5 }, { "para", 0xb6 }, { "middot", 0xb7 }, { "cedil", 0xb8 },
    { "sup1", 0xb9 }, { "ordm", 0xba }, { "raquo", 0xbb }, { "frac14", 0xbc }, { "frac12", 0xbd },
    { "frac34", 0xbe }, { "iquest", 0xbf }, { "Agrave", 0xc0 }, { "Aacute", 0xc1 }, { "Acirc", 0xc2 },
    { "Atilde", 0xc3 }, { "Auml", 0xc4 }, { "Aring", 0xc5 }, { "AElig", 0xc6 }, { "Ccedil", 0xc7 },
    { "Egrave", 0xc8 }, { "Eacute", 0xc9 }, { "Ecirc", 0xca }, { "Euml", 0xcb }, { "Igrave", 0xcc },
    { "Iacute", 0xcd }, { "Icirc", 0xce }, { "Iuml", 0xcf }, { "ETH", 0xd0 }, { "Ntilde", 0xd1 },
    { "Ograve", 0xd2 }, { "Oacute", 0xd3 }, { "Ocirc", 0xd4 }, { "Otilde", 0xd5 }, { "Ouml", 0xd6 },
    { "times", 0xd7 }, { "Oslash", 0xd8 }, { "Ugrave", 0xd9 }, { "Uacute", 0xda }, { "Ucirc", 0xdb },
    { "Uuml", 0xdc }, { "Yacute", 0xdd }, { "THORN", 0xde }, { "szlig", 0xdf }, { "agrave", 0xe0 },
    { "aacute", 0xe1 }, { "acirc", 0xe2 }, { "atilde", 0xe3 }, { "auml", 0xe4 }, { "aring", 0xe5 },
    { "aelig", 0xe6 }, { "ccedil", 0xe7 }, { "egrave", 0xe8 }, { "eacute", 0xe9 }, { "ecirc", 0xea },
    { "euml", 0xeb }, { "igrave", 0xec }, { "iacute", 0xed }, { "icirc", 0xee }, { "iuml", 0xef },
    { "eth", 0xf0 }, { "ntilde", 0xf1 }, { "ograve", 0xf2 }, { "oacute", 0xf3 }, { "ocirc", 0xf4 },
    { "otilde", 0xf5 }, { "ouml", 0xf6 }, { "divide", 0xf7 }, { "oslash", 0xf8 }, { "ugrave", 0xf9 },
    { "uacute", 0xfa }, { "ucirc", 0xfb }, { "uuml", 0xfc }, { "yacute", 0xfd }, { "thorn", 0xfe },
    { "yuml", 0xff }, { "OElig", 0x152 }, { "oelig", 0x153 }, { "Scaron", 0x160 }, { "scaron", 0x161 },
    { "Yuml", 0x178 }, { "fnof", 0x192 }, { "circ", 0x2c6 }, { "tilde", 0x2dc }, { "ensp", 0x2002 },
    { "emsp", 0x2003 }, { "thinsp", 0x2009 }, { "ndash", 0x2013 }, { "mdash", 0x2014 },
    { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "sbquo", 0x201a }, { "ldquo", 0x201c },
    { "rdquo", 0x201d }, { "bdquo", 0x201e }, { "dagger", 0x2020 }, { "Dagger", 0x2021 },
    { "bull", 0x2022 }, { "hellip", 0x2026 }, { "permil", 0x2030 }, { "prime", 0x2032 },
    { "lsaquo", 0x2039 }, { "rsaquo", 0x203a }, { "euro", 0x20ac }, { "trade", 0x2122 },
    { "larr", 0x2190 }, { "uarr", 0x2191 }, { "rarr", 0x2192 }, { "darr", 0x2193 },
    { "harr", 0x2194 }, { "minus", 0x2212 }, { "infin", 0x221e }, { "ne", 0x2260 },
    { "le", 0x2264 }, { "ge", 0x2265 }, { "hearts", 0x2665 },
};

/* Text from p to end into b, entities replaced. */
static void put_text(buf *b, const unsigned char *p, const unsigned char *end)
{
    while (p < end) {
        if (*p == '&') {
            const unsigned char *q = p + 1;
            unsigned long c = 0;
            int ok = 0;
            if (q < end && *q == '#') {
                int hex = q + 1 < end && (q[1] == 'x' || q[1] == 'X'), digits = 0;
                q += hex ? 2 : 1;
                for (; q < end && digits < 8; q++, digits++) {
                    int d = *q >= '0' && *q <= '9' ? *q - '0' : hex && *q >= 'a' && *q <= 'f' ? *q - 'a' + 10
                          : hex && *q >= 'A' && *q <= 'F' ? *q - 'A' + 10 : -1;
                    if (d < 0) break;
                    c = c * (hex ? 16 : 10) + (unsigned long)d;
                }
                ok = digits > 0;
            } else {
                size_t n = 0, i;
                while (q + n < end && n < 8 && ((q[n] >= 'a' && q[n] <= 'z') || (q[n] >= 'A' && q[n] <= 'Z') ||
                       (q[n] >= '0' && q[n] <= '9')))
                    n++;
                for (i = 0; i < sizeof entities / sizeof entities[0]; i++)
                    if (strlen(entities[i].name) == n && !memcmp(entities[i].name, q, n)) {
                        c = entities[i].cp; ok = 1; break;
                    }
                q += n;
            }
            if (ok) {
                if (q < end && *q == ';') q++;
                put_cp(b, c ? c : 0xfffd);
                p = q;
                continue;
            }
            put(b, "&", 1);
            p++;
            continue;
        }
        put_cp(b, next_cp(&p, end));
    }
}

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

/* A tag or attribute name, lower case, holding only what an XML name may:
 * letters, digits, '-', '_' and '.' (not first). Answers its length (0: none). */
static size_t read_name(const unsigned char **pp, const unsigned char *end, char out[MAX_NAME], int attr)
{
    const unsigned char *p = *pp;
    size_t n = 0;
    while (p < end && !is_space(*p) && *p != '>' && *p != '/' && (!attr || *p != '=') && (attr || *p != '<')) {
        int c = *p++;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (n + 1 < MAX_NAME && ((c >= 'a' && c <= 'z') || c == '_' ||
            (n && ((c >= '0' && c <= '9') || c == '-' || c == '.'))))
            out[n++] = (char)c;
    }
    out[n] = 0;
    *pp = p;
    return n;
}

static int one_of(const char *name, const char *const *list)
{
    for (; *list; list++) if (!strcmp(name, *list)) return 1;
    return 0;
}

static const char *const void_elements[] = { "area", "base", "basefont", "br", "col", "embed", "frame",
    "hr", "img", "input", "isindex", "keygen", "link", "meta", "param", "source", "track", "wbr", NULL };
static const char *const raw_elements[] = { "script", "style", "textarea", "title", "xmp", NULL };
static const char *const closes_p[] = { "address", "blockquote", "center", "dir", "div", "dl", "fieldset",
    "form", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "li", "menu", "ol", "p", "pre", "table", "ul", NULL };

typedef struct {
    oxp_doc *d;
    oxp_node *open[MAX_OPEN];
    int depth;                        /* open[0] is html */
    oxp_node *head, *body;
    buf text;
    int oom;
} reader;

static oxp_node *top(reader *r) { return r->open[r->depth - 1]; }

static void add(reader *r, oxp_node *parent, oxp_node *n)
{
    if (!n || oxp_dom_insert(parent, n, NULL)) r->oom = 1;
}

/* The body, made when there is none, with everything after it open in it. */
static void need_body(reader *r)
{
    if (r->body) {
        if (r->depth == 1 || (r->depth == 2 && top(r) == r->head)) { r->depth = 1; r->open[r->depth++] = r->body; }
        return;
    }
    r->body = oxp_dom_create(r->d, OXP_NODE_ELEMENT, NULL, NULL, "body", NULL);
    add(r, r->open[0], r->body);
    if (r->oom) return;
    r->depth = 1;
    r->open[r->depth++] = r->body;
}

static void flush_text(reader *r)
{
    const char *s = r->text.s;
    size_t i;
    if (r->text.oom) { r->oom = 1; return; }
    if (!r->text.n) return;
    for (i = 0; i < r->text.n && is_space((unsigned char)s[i]); i++) {}
    if (r->depth == 1 || top(r) == r->head) {
        if (i == r->text.n) { r->text.n = 0; return; }      /* spaces between head and body */
        if (top(r) != r->head) need_body(r);
    }
    if (!r->oom) add(r, top(r), oxp_dom_create(r->d, OXP_NODE_TEXT, NULL, NULL, NULL, s));
    r->text.n = 0;
    r->text.s[0] = 0;
}

/* Close up to and with the nearest open element named name, not past one
 * named in stop. Answers whether there was one. */
static int close_named(reader *r, const char *name, const char *const *stop)
{
    int i;
    for (i = r->depth - 1; i > 0; i--) {
        const char *n = r->open[i]->name;
        if (!strcmp(n, name)) { r->depth = i; return 1; }
        if (stop && one_of(n, stop)) return 0;
        if (r->open[i] == r->body) return 0;
    }
    return 0;
}

static const char *const list_scope[] = { "ul", "ol", "dir", "menu", NULL };
static const char *const dl_scope[] = { "dl", NULL };
static const char *const table_scope[] = { "table", NULL };
static const char *const row_scope[] = { "tr", "table", NULL };
static const char *const select_scope[] = { "select", NULL };

static void implied_ends(reader *r, const char *name)
{
    if (one_of(name, closes_p) && !strcmp(top(r)->name, "p")) r->depth--;
    if (!strcmp(name, "li")) close_named(r, "li", list_scope);
    else if (!strcmp(name, "dd") || !strcmp(name, "dt")) {
        if (!close_named(r, "dd", dl_scope)) close_named(r, "dt", dl_scope);
    } else if (!strcmp(name, "option")) close_named(r, "option", select_scope);
    else if (!strcmp(name, "tr")) close_named(r, "tr", table_scope);
    else if (!strcmp(name, "td") || !strcmp(name, "th")) {
        if (!close_named(r, "td", row_scope)) close_named(r, "th", row_scope);
    }
}

/* A start tag from p (just past its '<' and name), its attributes on n. */
static const unsigned char *read_attrs(reader *r, oxp_node *n, const unsigned char *p, const unsigned char *end,
                                       int *self_closed)
{
    *self_closed = 0;
    while (p < end && *p != '>') {
        char name[MAX_NAME];
        const unsigned char *vs = NULL, *ve = NULL;
        if (is_space(*p)) { p++; continue; }
        if (*p == '/') { p++; if (p < end && *p == '>') *self_closed = 1; continue; }
        if (!read_name(&p, end, name, 1) && p < end && *p != '=') { if (p < end && *p != '>') p++; continue; }
        while (p < end && is_space(*p)) p++;
        if (p < end && *p == '=') {
            p++;
            while (p < end && is_space(*p)) p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                int q = *p++;
                vs = p;
                while (p < end && *p != q) p++;
                ve = p;
                if (p < end) p++;
            } else {
                vs = p;
                while (p < end && !is_space(*p) && *p != '>') p++;
                ve = p;
            }
        }
        if (n && *name && !oxp_dom_attr(n, NULL, name)) {
            buf v = { 0, 0, 0, 0 };
            put(&v, "", 0);
            if (vs) put_text(&v, vs, ve);
            if (v.oom) r->oom = 1;
            else if (oxp_dom_set_attr(n, NULL, NULL, name, v.s, 0) == -1) r->oom = 1;
            free(v.s);
        }
    }
    return p < end ? p + 1 : p;
}

static const unsigned char *start_tag(reader *r, const unsigned char *p, const unsigned char *end)
{
    char name[MAX_NAME];
    oxp_node *n;
    int self_closed;

    if (!read_name(&p, end, name, 0)) {
        while (p < end && *p != '>') p++;
        return p < end ? p + 1 : p;
    }
    flush_text(r);
    if (r->oom) return end;
    if (!strcmp(name, "html")) return read_attrs(r, r->open[0], p, end, &self_closed);
    if (!strcmp(name, "head")) {
        if (!r->head && !r->body) {
            r->head = oxp_dom_create(r->d, OXP_NODE_ELEMENT, NULL, NULL, "head", NULL);
            add(r, r->open[0], r->head);
            r->depth = 1;
            if (!r->oom) r->open[r->depth++] = r->head;
        }
        return read_attrs(r, r->head, p, end, &self_closed);
    }
    if (!strcmp(name, "body")) {
        need_body(r);
        if (r->oom) return end;
        r->depth = 2;                          /* back to the body */
        return read_attrs(r, r->body, p, end, &self_closed);
    }
    if (r->depth == 1 || (top(r) == r->head && !one_of(name, (const char *const[]){ "title", "meta", "link",
                                                    "style", "script", "base", NULL })))
        need_body(r);
    if (r->oom) return end;
    implied_ends(r, name);
    n = oxp_dom_create(r->d, OXP_NODE_ELEMENT, NULL, NULL, name, NULL);
    add(r, top(r), n);
    if (r->oom) return end;
    p = read_attrs(r, n, p, end, &self_closed);
    if (one_of(name, raw_elements) && !self_closed) {
        /* its text runs to its own end tag */
        const unsigned char *t = p;
        size_t len = strlen(name);
        while (t < end) {
            if (*t == '<' && t + 1 < end && t[1] == '/' && (size_t)(end - t) >= len + 2) {
                size_t i;
                for (i = 0; i < len; i++) {
                    int c = t[2 + i];
                    if (c >= 'A' && c <= 'Z') c += 32;
                    if (c != name[i]) break;
                }
                if (i == len && (t + 2 + len == end || t[2 + len] == '>' || is_space(t[2 + len]))) break;
            }
            t++;
        }
        if (t > p) {
            buf v = { 0, 0, 0, 0 };
            if (!strcmp(name, "textarea") || !strcmp(name, "title")) put_text(&v, p, t);
            else while (p < t) put_cp(&v, next_cp(&p, t));
            if (v.oom) r->oom = 1;
            else if (v.n) add(r, n, oxp_dom_create(r->d, OXP_NODE_TEXT, NULL, NULL, NULL, v.s));
            free(v.s);
        }
        while (t < end && *t != '>') t++;
        return t < end ? t + 1 : t;
    }
    if (!self_closed && !one_of(name, void_elements) && r->depth < MAX_OPEN) r->open[r->depth++] = n;
    return p;
}

static const unsigned char *end_tag(reader *r, const unsigned char *p, const unsigned char *end)
{
    char name[MAX_NAME];
    read_name(&p, end, name, 0);
    while (p < end && *p != '>') p++;
    flush_text(r);
    if (*name && strcmp(name, "html") && strcmp(name, "body")) {
        if (!strcmp(name, "head")) { if (top(r) == r->head) r->depth = 1; }
        else if (!strcmp(name, "p") && !close_named(r, "p", NULL)) {
            /* </p> with no p open is an empty p, as browsers read it */
            if (r->depth == 1 || top(r) == r->head) need_body(r);
            if (!r->oom) add(r, top(r), oxp_dom_create(r->d, OXP_NODE_ELEMENT, NULL, NULL, "p", NULL));
        }
        else close_named(r, name, NULL);
    }
    return p < end ? p + 1 : p;
}

/* Past a comment, a doctype or a processing instruction from p (at its '<'). */
static const unsigned char *skip_markup(const unsigned char *p, const unsigned char *end)
{
    if (end - p >= 4 && !memcmp(p, "<!--", 4)) {
        const unsigned char *q = p + 4;
        while (q + 2 < end && memcmp(q, "-->", 3)) q++;
        return q + 2 < end ? q + 3 : end;
    }
    while (p < end && *p != '>') p++;
    return p < end ? p + 1 : p;
}

oxp_doc *oxp_html_parse(const char *data, size_t len)
{
    reader r;
    const unsigned char *p = (const unsigned char *)data, *end = p + len, *text = p;
    oxp_node *html;

    memset(&r, 0, sizeof r);
    if (!(r.d = oxp_dom_new())) return NULL;
    html = oxp_dom_create(r.d, OXP_NODE_ELEMENT, NULL, NULL, "html", NULL);
    if (!html || oxp_dom_insert(oxp_dom_document(r.d), html, NULL)) { oxp_dom_free(r.d); return NULL; }
    r.open[r.depth++] = html;
    while (p < end && !r.oom) {
        if (*p != '<' || p + 1 >= end) { p++; continue; }
        if (is_alpha(p[1]) || p[1] == '/' || p[1] == '!' || p[1] == '?') {
            if (p[1] == '/' && (p + 2 >= end || !is_alpha(p[2]))) {
                /* "</" and no name: "</>" is nothing, anything else a comment */
                put_text(&r.text, text, p);
                p = skip_markup(p, end);
                text = p;
                continue;
            }
            put_text(&r.text, text, p);
            if (p[1] == '/') p = end_tag(&r, p + 2, end);
            else if (p[1] == '!' || p[1] == '?') { flush_text(&r); p = skip_markup(p, end); }
            else p = start_tag(&r, p + 1, end);
            text = p;
            continue;
        }
        p++;
    }
    if (!r.oom) {
        put_text(&r.text, text, end);
        flush_text(&r);
    }
    if (!r.oom && !r.body) need_body(&r);
    free(r.text.s);
    if (r.oom) { oxp_dom_free(r.d); return NULL; }
    return r.d;
}

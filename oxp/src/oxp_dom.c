/* OXP's small tree. MIT licence. Copyright (c) 2026 AmigaChrome Team. */
#include "oxp_dom.h"
#include <stdlib.h>
#include <string.h>

typedef struct block { struct block *next; size_t used, size; char data[]; } block;

struct oxp_doc {
    block *blocks;
    oxp_node docnode;
    const char **uris; size_t nuris, capuris;   /* each namespace name stored once */
};

struct oxp_dom_builder {
    oxp_doc *d;
    oxp_node *cur;
    const oxp_parser *p;
    char *pend; size_t npend, cappend; int pend_cdata;
    int nomem;
};

static void *arena(oxp_doc *d, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    block *b = d->blocks;
    if (!b || b->size - b->used < n) {
        size_t sz = n > 16384 ? n : 16384;
        b = malloc(sizeof(block) + sz);
        if (!b) return NULL;
        b->size = sz; b->used = 0; b->next = d->blocks; d->blocks = b;
    }
    void *r = b->data + b->used;
    b->used += n;
    return r;
}
static const char *str(oxp_doc *d, const char *s, size_t n)
{
    if (!s) return NULL;
    char *q = arena(d, n + 1);
    if (!q) return NULL;
    memcpy(q, s, n); q[n] = 0;
    return q;
}
static const char *uri(oxp_doc *d, const char *u)
{
    if (!u) return NULL;
    for (size_t i = d->nuris; i-- > 0;) if (!strcmp(d->uris[i], u)) return d->uris[i];
    const char *s = str(d, u, strlen(u));
    if (!s) return NULL;
    if (d->nuris == d->capuris) {
        size_t c = d->capuris ? d->capuris * 2 : 8;
        const char **q = realloc(d->uris, c * sizeof *q);
        if (!q) return NULL;
        d->uris = q; d->capuris = c;
    }
    d->uris[d->nuris++] = s;
    return s;
}

static oxp_node *node(oxp_dom_builder *b, oxp_node_type t)
{
    oxp_node *n = arena(b->d, sizeof *n);
    if (!n) { b->nomem = 1; return NULL; }
    memset(n, 0, sizeof *n);
    n->type = t;
    n->owner = b->d;
    n->parent = b->cur;
    n->line = b->p ? oxp_line(b->p) : 0;
    if (b->cur->last) { b->cur->last->next = n; n->prev = b->cur->last; } else b->cur->first = n;
    b->cur->last = n;
    return n;
}

static void flush(oxp_dom_builder *b)
{
    if (!b->npend) return;
    oxp_node *n = node(b, b->pend_cdata ? OXP_NODE_CDATA : OXP_NODE_TEXT);
    if (n) { n->text = str(b->d, b->pend, b->npend); n->len = b->npend; if (!n->text) b->nomem = 1; }
    b->npend = 0;
}
static void addtext(oxp_dom_builder *b, const char *t, size_t n, int cdata)
{
    if (b->npend && b->pend_cdata != cdata) flush(b);
    if (cdata && !n) { b->pend_cdata = 1; oxp_node *x = node(b, OXP_NODE_CDATA); if (x) { x->text = ""; } return; }
    if (b->npend + n + 1 > b->cappend) {
        size_t c = (b->npend + n + 1) * 2;
        char *q = realloc(b->pend, c);
        if (!q) { b->nomem = 1; return; }
        b->pend = q; b->cappend = c;
    }
    memcpy(b->pend + b->npend, t, n);
    b->npend += n;
    b->pend_cdata = cdata;
    if (cdata) flush(b);          /* each CDATA section is a node of its own */
}

static void h_start(void *ud, const char *l, const char *pfx, const char *u, int nns, const oxp_ns *ns, int na, const oxp_attr *a)
{
    oxp_dom_builder *b = ud;
    flush(b);
    oxp_node *n = node(b, OXP_NODE_ELEMENT);
    if (!n) return;
    oxp_doc *d = b->d;
    /* str() and uri() give NULL when out of memory: the tree is then refused
     * (finish), never handed out with a NULL name, value or namespace */
    n->name = str(d, l, strlen(l));
    n->prefix = pfx ? str(d, pfx, strlen(pfx)) : NULL;
    n->uri = uri(d, u);
    if (!n->name || (pfx && !n->prefix) || (u && !n->uri)) b->nomem = 1;
    oxp_dns **nt = &n->ns;
    for (int i = 0; i < nns; i++) {
        oxp_dns *x = arena(d, sizeof *x);
        if (!x) { b->nomem = 1; return; }
        x->prefix = ns[i].prefix ? str(d, ns[i].prefix, strlen(ns[i].prefix)) : NULL;
        x->uri = uri(d, ns[i].uri);
        if ((ns[i].prefix && !x->prefix) || (ns[i].uri && !x->uri)) b->nomem = 1;
        x->next = NULL; *nt = x; nt = &x->next;
    }
    oxp_dattr **at = &n->attrs;
    for (int i = 0; i < na; i++) {
        oxp_dattr *x = arena(d, sizeof *x);
        if (!x) { b->nomem = 1; return; }
        x->local = str(d, a[i].local, strlen(a[i].local));
        x->prefix = a[i].prefix ? str(d, a[i].prefix, strlen(a[i].prefix)) : NULL;
        x->uri = uri(d, a[i].uri);
        x->value = str(d, a[i].value, a[i].value_len);
        x->value_len = a[i].value_len;
        x->defaulted = a[i].defaulted;
        if (!x->local || (a[i].prefix && !x->prefix) || (a[i].uri && !x->uri) || (a[i].value && !x->value)) b->nomem = 1;
        x->next = NULL; *at = x; at = &x->next;
    }
    b->cur = n;
}
static void h_end(void *ud, const char *l, const char *p, const char *u)
{
    oxp_dom_builder *b = ud;
    (void)l; (void)p; (void)u;
    flush(b);
    if (b->cur->parent) b->cur = b->cur->parent;
}
static void h_chars(void *ud, const char *t, size_t n) { addtext(ud, t, n, 0); }
static void h_cdata(void *ud, const char *t, size_t n) { addtext(ud, t, n, 1); }
static void h_comment(void *ud, const char *t)
{
    oxp_dom_builder *b = ud;
    flush(b);
    oxp_node *n = node(b, OXP_NODE_COMMENT);
    if (n) { n->len = strlen(t); n->text = str(b->d, t, n->len); if (!n->text) b->nomem = 1; }
}
static void h_pi(void *ud, const char *target, const char *data)
{
    oxp_dom_builder *b = ud;
    flush(b);
    oxp_node *n = node(b, OXP_NODE_PI);
    if (n) {
        n->name = str(b->d, target, strlen(target)); n->len = strlen(data); n->text = str(b->d, data, n->len);
        if (!n->name || !n->text) b->nomem = 1;
    }
}

static const oxp_handler dom_handler = {
    NULL, NULL, NULL, NULL, NULL, h_start, h_end, h_chars, h_cdata, h_comment, h_pi, NULL, NULL, NULL
};
const oxp_handler *oxp_dom_handler(void) { return &dom_handler; }

oxp_dom_builder *oxp_dom_builder_new(void)
{
    oxp_dom_builder *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->d = calloc(1, sizeof *b->d);
    if (!b->d) { free(b); return NULL; }
    b->d->docnode.type = OXP_NODE_DOCUMENT;
    b->d->docnode.owner = b->d;
    b->cur = &b->d->docnode;
    return b;
}
void oxp_dom_builder_set_parser(oxp_dom_builder *b, const oxp_parser *p) { b->p = p; }

oxp_doc *oxp_dom_builder_finish(oxp_dom_builder *b)
{
    flush(b);
    oxp_doc *d = b->d;
    int bad = b->nomem;
    free(b->pend);
    free(b);
    if (bad) { oxp_dom_free(d); return NULL; }
    return d;
}

oxp_doc *oxp_dom_parse(const void *data, size_t len, unsigned options, oxp_code *err, char *msg, size_t msgsize,
                       unsigned long *line, unsigned long *column)
{
    oxp_dom_builder *b = oxp_dom_builder_new();
    if (!b) { if (err) *err = OXP_E_NOMEM; return NULL; }
    oxp_parser *p = oxp_new(&dom_handler, b, NULL);
    if (!p) { oxp_dom_free(oxp_dom_builder_finish(b)); if (err) *err = OXP_E_NOMEM; return NULL; }
    b->p = p;
    oxp_set_options(p, options);
    oxp_code c = oxp_parse(p, data, len, 1);
    const char *m;
    unsigned long l, col;
    oxp_error(p, &m, &l, &col);
    if (err) *err = c;
    if (msg && msgsize) { msg[0] = 0; if (m) { size_t n = strlen(m); if (n >= msgsize) n = msgsize - 1; memcpy(msg, m, n); msg[n] = 0; } }
    if (line) *line = l;
    if (column) *column = col;
    oxp_doc *d = oxp_dom_builder_finish(b);
    oxp_free(p);
    if (c) { oxp_dom_free(d); return NULL; }
    if (!d && err) *err = OXP_E_NOMEM;
    return d;
}

void oxp_dom_free(oxp_doc *d)
{
    if (!d) return;
    for (block *b = d->blocks; b;) { block *n = b->next; free(b); b = n; }
    free(d->uris);
    free(d);
}

oxp_node *oxp_dom_document(oxp_doc *d) { return &d->docnode; }
oxp_node *oxp_dom_root(oxp_doc *d)
{
    for (oxp_node *n = d->docnode.first; n; n = n->next) if (n->type == OXP_NODE_ELEMENT) return n;
    return NULL;
}

static int same(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }
static int match(const oxp_node *n, const char *u, const char *l)
{
    if (n->type != OXP_NODE_ELEMENT) return 0;
    if (l && strcmp(n->name, l)) return 0;
    if (u && u[0] == '*' && !u[1]) return 1;
    return same(n->uri, u);
}

const char *oxp_dom_attr(const oxp_node *el, const char *u, const char *l)
{
    for (const oxp_dattr *a = el->attrs; a; a = a->next)
        if (!strcmp(a->local, l) && same(a->uri, u)) return a->value;
    return NULL;
}
oxp_node *oxp_dom_child(const oxp_node *n, const char *u, const char *l)
{
    for (oxp_node *c = n->first; c; c = c->next) if (match(c, u, l)) return c;
    return NULL;
}
oxp_node *oxp_dom_next(const oxp_node *n, const char *u, const char *l)
{
    for (oxp_node *c = n->next; c; c = c->next) if (match(c, u, l)) return c;
    return NULL;
}

/* The text of n and everything under it. Walked with the tree's own parent and
 * next links, not by recursion: the parser allows thousands of nested elements,
 * and a C frame for each would overflow a 32-64 KB Amiga stack. */
size_t oxp_dom_text(const oxp_node *top, char *buf, size_t size)
{
    size_t at = 0;
    const oxp_node *n = top;
    for (;;) {
        if (n->type == OXP_NODE_TEXT || n->type == OXP_NODE_CDATA) {
            if (at < size) { size_t k = size - at - 1 < n->len ? size - at - 1 : n->len; memcpy(buf + at, n->text, k); }
            at += n->len;
        } else if ((n->type == OXP_NODE_ELEMENT || n->type == OXP_NODE_DOCUMENT) && n->first) {
            n = n->first;
            continue;
        }
        while (n != top && !n->next) n = n->parent;
        if (n == top) break;
        n = n->next;
    }
    if (size) buf[at < size ? at : size - 1] = 0;
    return at;
}

typedef int (*wfn)(void *, const char *, size_t);
static int w(wfn f, void *c, const char *s) { return f(c, s, strlen(s)); }
static int wesc(wfn f, void *c, const char *s, size_t n, int attr)
{
    size_t b = 0;
    for (size_t i = 0; i < n; i++) {
        const char *r = NULL;
        switch (s[i]) {
        case '<': r = "&lt;"; break;
        case '>': r = "&gt;"; break;
        case '&': r = "&amp;"; break;
        case '"': if (attr) r = "&quot;"; break;
        case '\t': if (attr) r = "&#9;"; break;
        case '\n': if (attr) r = "&#10;"; break;
        case '\r': r = "&#13;"; break;
        }
        if (r) { if ((i > b && f(c, s + b, i - b)) || w(f, c, r)) return -1; b = i + 1; }
    }
    return n > b ? f(c, s + b, n - b) : 0;
}
static int qname(wfn f, void *c, const char *pfx, const char *l)
{
    if (pfx && (w(f, c, pfx) || w(f, c, ":"))) return -1;
    return w(f, c, l);
}

/* One node: a leaf whole, an element's start tag ("/>" when it has no children). */
static int write_open(const oxp_node *n, wfn f, void *c)
{
    switch (n->type) {
    case OXP_NODE_DOCUMENT: return 0;
    case OXP_NODE_TEXT: return wesc(f, c, n->text, n->len, 0);
    case OXP_NODE_CDATA: {
        if (w(f,c,"<![CDATA[")) return -1;
        const char *s = n->text, *end = s + n->len, *at;
        while ((at = strstr(s,"]]>")) != NULL) {
            if (f(c,s,(size_t)(at-s)) || w(f,c,"]]]]><![CDATA[>")) return -1;
            s = at + 3;
        }
        return f(c,s,(size_t)(end-s)) || w(f,c,"]]>") ? -1 : 0;
    }
    case OXP_NODE_COMMENT: return (w(f, c, "<!--") || f(c, n->text, n->len) || w(f, c, "-->")) ? -1 : 0;
    case OXP_NODE_PI:
        if (w(f, c, "<?") || w(f, c, n->name)) return -1;
        if (n->len && (w(f, c, " ") || f(c, n->text, n->len))) return -1;
        return w(f, c, "?>");
    case OXP_NODE_ELEMENT:
        if (w(f, c, "<") || qname(f, c, n->prefix, n->name)) return -1;
        for (const oxp_dns *x = n->ns; x; x = x->next) {
            if (w(f, c, x->prefix ? " xmlns:" : " xmlns") || (x->prefix && w(f, c, x->prefix)) || w(f, c, "=\"") ||
                wesc(f, c, x->uri, strlen(x->uri), 1) || w(f, c, "\"")) return -1;
        }
        for (const oxp_dattr *a = n->attrs; a; a = a->next)
            if (w(f, c, " ") || qname(f, c, a->prefix, a->local) || w(f, c, "=\"") || wesc(f, c, a->value, a->value_len, 1) || w(f, c, "\""))
                return -1;
        return w(f, c, n->first ? ">" : "/>");
    }
    return 0;
}

/* Walked with the tree's own links, not by recursion (as oxp_dom_text). */
int oxp_dom_write(const oxp_node *top, wfn f, void *c)
{
    const oxp_node *n = top;
    for (;;) {
        if (write_open(n, f, c)) return -1;
        if ((n->type == OXP_NODE_ELEMENT || n->type == OXP_NODE_DOCUMENT) && n->first) { n = n->first; continue; }
        for (;;) {                             /* n is written: on to the next, closing what ends */
            if (n == top) return 0;
            if (n->next) { n = n->next; break; }
            n = n->parent;
            if (n->type == OXP_NODE_ELEMENT && (w(f, c, "</") || qname(f, c, n->prefix, n->name) || w(f, c, ">"))) return -1;
        }
    }
}

/* Mutable tree operations. All storage is reclaimed with the document. */
static int xmlchar(unsigned long c)
{
    return c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 0xd7ff) ||
        (c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff);
}
static int namestart(unsigned long c)
{
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= 0xc0 && c <= 0xd6) || (c >= 0xd8 && c <= 0xf6) ||
        (c >= 0xf8 && c <= 0x2ff) || (c >= 0x370 && c <= 0x37d) ||
        (c >= 0x37f && c <= 0x1fff) || (c >= 0x200c && c <= 0x200d) ||
        (c >= 0x2070 && c <= 0x218f) || (c >= 0x2c00 && c <= 0x2fef) ||
        (c >= 0x3001 && c <= 0xd7ff) || (c >= 0xf900 && c <= 0xfdcf) ||
        (c >= 0xfdf0 && c <= 0xfffd) || (c >= 0x10000 && c <= 0xeffff);
}
static int valid(const char *s, int name)
{
    const unsigned char *p = (const unsigned char *)s;
    int first = 1;
    if (!p || (name && !*p)) return 0;
    while (*p) {
        unsigned long c = *p++; unsigned n = 0; unsigned long minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; c &= 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; c &= 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; c &= 7; minimum = 0x10000; }
        else if (c >= 0x80) return 0;
        for (unsigned i = 0; i < n; ++i) {
            if ((*p & 0xc0) != 0x80) return 0;
            c = (c << 6) | (*p++ & 63);
        }
        if (c < minimum || !xmlchar(c)) return 0;
        if (name && !namestart(c) && (first || !(c == '-' || c == '.' ||
            (c >= '0' && c <= '9') || c == 0xb7 || (c >= 0x300 && c <= 0x36f) ||
            (c >= 0x203f && c <= 0x2040)))) return 0;
        first = 0;
    }
    return 1;
}
static int nsvalid(const char *u, const char *p, const char *l, int attr)
{
    if (!valid(l, 1) || (p && !valid(p, 1)) || (u && !valid(u, 0))) return 0;
    if (p && (!u || !*u)) return 0;
    if (p && !strcmp(p, "xmlns")) return 0;
    if (attr && !p && !strcmp(l, "xmlns")) return 0;
    if (u && !strcmp(u, "http://www.w3.org/2000/xmlns/")) return 0;
    if ((p && !strcmp(p, "xml")) != (u && !strcmp(u, "http://www.w3.org/XML/1998/namespace"))) return 0;
    if (attr && u && *u && !p) return 0;
    return 1;
}
oxp_doc *oxp_dom_new(void)
{
    oxp_doc *d = calloc(1, sizeof *d);
    if (d) { d->docnode.type = OXP_NODE_DOCUMENT; d->docnode.owner = d; }
    return d;
}
static int bind(oxp_node *n, const char *p, const char *u)
{
    if (p && !strcmp(p, "xml")) return 0;
    const char *want = u ? u : "";
    for (oxp_node *a = n; a; a = a->parent)
        for (oxp_dns *x = a->ns; x; x = x->next)
            if (same(x->prefix, p)) {
                if (!strcmp(x->uri, want)) return 0;
                if (a == n) { const char *v = uri(n->owner, want); if (!v) return -1; x->uri = v; return 0; }
                goto add;
            }
    if (!p && !*want) return 0;
add:;
    oxp_dns *x = arena(n->owner, sizeof *x);
    if (!x) return -1;
    x->prefix = p ? str(n->owner, p, strlen(p)) : NULL;
    x->uri = uri(n->owner, want);
    if ((p && !x->prefix) || !x->uri) return -1;
    x->next = n->ns; n->ns = x;
    return 0;
}
oxp_node *oxp_dom_create(oxp_doc *d, oxp_node_type t, const char *u,
                          const char *p, const char *l, const char *text)
{
    if (!d || t == OXP_NODE_DOCUMENT) return NULL;
    if (t == OXP_NODE_ELEMENT && !nsvalid(u, p, l, 0)) return NULL;
    if (t != OXP_NODE_ELEMENT && !valid(text ? text : "", 0)) return NULL;
    if (t == OXP_NODE_COMMENT && (strstr(text ? text : "", "--") ||
        (text && *text && text[strlen(text)-1] == '-'))) return NULL;
    if (t == OXP_NODE_PI && (!valid(l, 1) || !strcmp(l, "xml") || !strcmp(l,"XML") || strstr(text ? text : "", "?>"))) return NULL;
    if (t < OXP_NODE_ELEMENT || t > OXP_NODE_PI) return NULL;
    oxp_node *n = arena(d, sizeof *n);
    if (!n) return NULL;
    memset(n, 0, sizeof *n); n->owner = d; n->type = t;
    if (l) { n->name = str(d,l,strlen(l)); if (!n->name) return NULL; }
    if (p) { n->prefix = str(d,p,strlen(p)); if (!n->prefix) return NULL; }
    if (u && *u) { n->uri = uri(d,u); if (!n->uri) return NULL; }
    if (t != OXP_NODE_ELEMENT) { n->text = str(d,text ? text : "",text ? strlen(text) : 0); if (!n->text) return NULL; n->len = strlen(n->text); }
    if (t == OXP_NODE_ELEMENT && bind(n,p,u)) return NULL;
    return n;
}
int oxp_dom_detach(oxp_node *n)
{
    if (!n || n->type == OXP_NODE_DOCUMENT) return -2;
    if (!n->parent) return 0;
    if (n->prev) n->prev->next = n->next; else n->parent->first = n->next;
    if (n->next) n->next->prev = n->prev; else n->parent->last = n->prev;
    n->parent = n->prev = n->next = NULL; return 0;
}
int oxp_dom_insert(oxp_node *p, oxp_node *n, oxp_node *before)
{
    if (!p || !n || p->owner != n->owner || n->type == OXP_NODE_DOCUMENT ||
        (p->type != OXP_NODE_DOCUMENT && p->type != OXP_NODE_ELEMENT) ||
        (before && before->parent != p)) return -2;
    for (oxp_node *a = p; a; a = a->parent) if (a == n) return -2;
    if (n == before) return 0;
    if (p->type == OXP_NODE_DOCUMENT) {
        if (n->type == OXP_NODE_TEXT || n->type == OXP_NODE_CDATA) return -2;
        if (n->type == OXP_NODE_ELEMENT)
            for (oxp_node *a = p->first; a; a = a->next)
                if (a != n && a->type == OXP_NODE_ELEMENT) return -2;
    }
    /* Preserve all inherited bindings: descendants may use prefixes which
     * the subtree root itself does not use. Keep the nearest binding. */
    if (n->type == OXP_NODE_ELEMENT) {
        for (oxp_node *ancestor = n->parent; ancestor; ancestor = ancestor->parent)
            for (oxp_dns *x = ancestor->ns; x; x = x->next) {
                int present = 0;
                for (oxp_dns *own = n->ns; own; own = own->next)
                    if (same(own->prefix,x->prefix)) { present = 1; break; }
                if (!present) {
                    oxp_dns *copy = arena(n->owner,sizeof *copy);
                    if (!copy) return -1;
                    *copy = *x; copy->next = n->ns; n->ns = copy;
                }
            }

        if (bind(n,n->prefix,n->uri)) return -1;
        for (oxp_dattr *a = n->attrs; a; a = a->next)
            if (a->prefix && bind(n,a->prefix,a->uri)) return -1;
    }
    oxp_dom_detach(n);
    n->parent = p; n->next = before; n->prev = before ? before->prev : p->last;
    if (n->prev) n->prev->next = n; else p->first = n;
    if (before) before->prev = n; else p->last = n;
    /* A formerly unqualified root may now need xmlns="". */
    if (n->type == OXP_NODE_ELEMENT && bind(n,n->prefix,n->uri)) return -1;
    return 0;
}
int oxp_dom_set_attr(oxp_node *n, const char *u, const char *p,
                        const char *l, const char *v, unsigned defaulted)
{
    if (!n || n->type != OXP_NODE_ELEMENT || !nsvalid(u,p,l,1) || !valid(v,0)) return -2;
    if (p) {
        if (same(n->prefix,p) && !same(n->uri,u)) return -2;
        for (oxp_dattr *existing = n->attrs; existing; existing = existing->next)
            if (same(existing->prefix,p) && !same(existing->uri,u)) return -2;
    }
    oxp_doc *d = n->owner;
    const char *value = str(d,v,strlen(v));
    if (!value) return -1;
    oxp_dattr *a;
    for (a = n->attrs; a; a = a->next) if (same(a->uri,u) && !strcmp(a->local,l)) break;
    if (!a) {
        a = arena(d,sizeof *a); if (!a) return -1;
        memset(a,0,sizeof *a);
        a->local = str(d,l,strlen(l)); a->uri = u ? uri(d,u) : NULL;
        if (!a->local || (u && !a->uri)) return -1;
        a->next = n->attrs; n->attrs = a;
    }
    const char *prefix = p ? str(d,p,strlen(p)) : NULL;
    if (p && (!prefix || bind(n,p,u))) return -1;
    a->prefix = prefix; a->value = value; a->value_len = strlen(v); a->defaulted = defaulted;
    return 0;
}
int oxp_dom_remove_attr(oxp_node *n, const char *u, const char *l)
{
    if (!n || n->type != OXP_NODE_ELEMENT || !l) return -2;
    for (oxp_dattr **a = &n->attrs; *a; a = &(*a)->next)
        if (same((*a)->uri,u) && !strcmp((*a)->local,l)) { *a = (*a)->next; return 0; }
    return 0;
}
int oxp_dom_set_text(oxp_node *n, const char *text)
{
    if (!n || !valid(text,0)) return -2;
    if (n->type == OXP_NODE_ELEMENT) {
        oxp_node *v = oxp_dom_create(n->owner,OXP_NODE_TEXT,NULL,NULL,NULL,text);
        if (!v) return -1;
        while (n->first) oxp_dom_detach(n->first);
        return oxp_dom_insert(n,v,NULL);
    }
    if (n->type != OXP_NODE_TEXT && n->type != OXP_NODE_CDATA) return -2;
    const char *s = str(n->owner,text,strlen(text)); if (!s) return -1;
    n->text = s; n->len = strlen(s); return 0;
}
/* One node, with its namespaces and attributes, no children. */
static oxp_node *import_one(oxp_doc *d, const oxp_node *src)
{
    oxp_node *n = oxp_dom_create(d,src->type,src->uri,src->prefix,src->name,src->text);
    if (!n) return NULL;
    for (const oxp_dns *x = src->ns; x; x = x->next) if (bind(n,x->prefix,x->uri)) return NULL;
    for (const oxp_dattr *a = src->attrs; a; a = a->next)
        if (oxp_dom_set_attr(n,a->uri,a->prefix,a->local,a->value,a->defaulted)) return NULL;
    return n;
}
/* Deep: walked with the source's own links, not by recursion (as oxp_dom_text).
 * Each copy is put in its parent once everything under it is copied, as the
 * recursive copy did, so the namespaces come out the same; the copies of the
 * open elements wait on a stack in the heap. */
oxp_node *oxp_dom_import(oxp_doc *d, const oxp_node *src, int deep)
{
    if (!src || src->type == OXP_NODE_DOCUMENT) return NULL;
    oxp_node *top = import_one(d, src), *k = top, **open = NULL;
    size_t nopen = 0, capopen = 0;
    const oxp_node *s = src;
    if (!top || !deep) return top;
    for (;;) {
        if (s->first) {
            if (nopen == capopen) {
                size_t c = capopen ? capopen * 2 : 32;
                oxp_node **q = c > ((size_t)-1) / sizeof *q ? NULL : realloc(open, c * sizeof *q);
                if (!q) goto fail;
                open = q; capopen = c;
            }
            open[nopen++] = k;
            s = s->first;
            if (!(k = import_one(d, s))) goto fail;
            continue;
        }
        for (;;) {
            if (s == src) { free(open); return top; }
            if (oxp_dom_insert(open[nopen - 1], k, NULL)) goto fail;
            if (s->next) { s = s->next; if (!(k = import_one(d, s))) goto fail; break; }
            s = s->parent;
            k = open[--nopen];
        }
    }
fail:
    free(open);
    return NULL;
}

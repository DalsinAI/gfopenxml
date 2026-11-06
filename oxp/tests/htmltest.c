/* OXP's HTML reader (oxp_html.h): each case's HTML and the tree it must
 * give, written back as XML by oxp_dom_write.
 * MIT licence. Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oxp_html.h"

static char out[8192];
static size_t outn;
static int sink(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    if (outn + n >= sizeof out) return -1;
    memcpy(out + outn, s, n);
    outn += n;
    out[outn] = 0;
    return 0;
}

static const struct { const char *html, *xml; } cases[] = {
    { "", "<html><body/></html>" },
    { "hello", "<html><body>hello</body></html>" },
    { "<html><body bgcolor=\"transparent\"></body></html>", "<html><body bgcolor=\"transparent\"/></html>" },
    { "<b>bold</b> and <font color=#ff0000>red</font><br>next",
      "<html><body><b>bold</b> and <font color=\"#ff0000\">red</font><br/>next</body></html>" },
    { "<HTML><BODY><DIV CLASS=chat>x</DIV></BODY></HTML>", "<html><body><div class=\"chat\">x</div></body></html>" },
    { "<p>one<p>two<div>three</div>", "<html><body><p>one</p><p>two</p><div>three</div></body></html>" },
    { "<ul><li>a<li>b</ul>", "<html><body><ul><li>a</li><li>b</li></ul></body></html>" },
    { "a &amp; b &lt;c&gt; &copy; &#65;&#x42; &bogus; & x",
      "<html><body>a &amp; b &lt;c&gt; \xc2\xa9 AB &amp;bogus; &amp; x</body></html>" },
    { "<i>unclosed <b>both", "<html><body><i>unclosed <b>both</b></i></body></html>" },
    { "x</span>y", "<html><body>xy</body></html>" },
    { "<a href='http://x/?a=1&amp;b=2' href=dup>l</a>",
      "<html><body><a href=\"http://x/?a=1&amp;b=2\">l</a></body></html>" },
    { "<!DOCTYPE html><!-- c --><html><head><title>T &amp; t</title><style>b{x:<y>}</style></head><body>z</body></html>",
      "<html><head><title>T &amp; t</title><style>b{x:&lt;y&gt;}</style></head><body>z</body></html>" },
    { "<script>if (a<b) c=\"</div>\";</script>t",
      "<html><body><script>if (a&lt;b) c=\"&lt;/div&gt;\";</script>t</body></html>" },
    { "caf\xe9 \xc3\xa9", "<html><body>caf\xc3\xa9 \xc3\xa9</body></html>" },
    { "a < b and c<3 d\x01" "e", "<html><body>a &lt; b and c&lt;3 de</body></html>" },
    { "<img src=x.png/><hr noshade>", "<html><body><img src=\"x.png/\"/><hr noshade=\"\"/></body></html>" },
    { "<x:y z:w=1>q</x:y>", "<html><body><xy zw=\"1\">q</xy></body></html>" },
    { "<table><tr><td>1<td>2<tr><td>3</table>",
      "<html><body><table><tr><td>1</td><td>2</td></tr><tr><td>3</td></tr></table></body></html>" },
    { "</p>", "<html><body><p/></body></html>" },
    { "<div", "<html><body><div/></body></html>" },
};

int main(void)
{
    size_t i;
    int fails = 0;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        oxp_doc *d = oxp_html_parse(cases[i].html, strlen(cases[i].html));
        outn = 0; out[0] = 0;
        if (!d || oxp_dom_write(oxp_dom_root(d), sink, NULL)) { printf("FAIL %u: no tree\n", (unsigned)i); fails++; }
        else if (strcmp(out, cases[i].xml)) {
            printf("FAIL %u: %s\n  got  %s\n  want %s\n", (unsigned)i, cases[i].html, out, cases[i].xml);
            fails++;
        }
        oxp_dom_free(d);
    }
    /* deep nesting and big input don't break it */
    {
        size_t n = 200000, k;
        char *big = malloc(n * 3 + 1);
        for (k = 0; k < n; k++) memcpy(big + 3 * k, "<b>", 3);
        big[3 * n] = 0;
        oxp_doc *d = oxp_html_parse(big, 3 * n);
        if (!d) { printf("FAIL deep: no tree\n"); fails++; }
        oxp_dom_free(d);
        free(big);
    }
    /* random markup: never a crash, always a tree that writes */
    {
        static const char *bits[] = { "<", ">", "</", "<b>", "</b>", "<p>", "<li>", "</ul>", "<!--", "-->", "&",
            "&#x", ";", "=", "\"", "'", " a=", "<script>", "</script>", "<body>", "<head>", "x", "\xc3", "\xff",
            "<br/>", "<td>", "<table>", "/", "<!", "?>" };
        unsigned seed = 1, round;
        for (round = 0; round < 3000; round++) {
            char in[400];
            size_t n = 0;
            while (n < sizeof in - 20) {
                const char *b;
                seed = seed * 1103515245u + 12345u;
                b = bits[(seed >> 16) % (sizeof bits / sizeof bits[0])];
                memcpy(in + n, b, strlen(b)); n += strlen(b);
                if ((seed >> 8) % 37 == 0) break;
            }
            oxp_doc *d = oxp_html_parse(in, n);
            outn = 0;
            if (!d || oxp_dom_write(oxp_dom_root(d), sink, NULL)) { printf("FAIL random %u\n", round); fails++; }
            oxp_dom_free(d);
        }
    }
    printf("htmltest: %u cases, %d failed\n", (unsigned)(sizeof cases / sizeof cases[0]), fails);
    return fails != 0;
}

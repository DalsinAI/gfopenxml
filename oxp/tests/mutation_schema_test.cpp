/* Behavioural regression tests for editable OXP trees and validation.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "oxp_schema.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
static unsigned checks = 0, failures = 0;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      ++failures;                                                              \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                \
    }                                                                          \
  } while (0)
using Doc = std::unique_ptr<oxp_doc, void (*)(oxp_doc *)>;
using Schema = std::unique_ptr<oxp_schema, void (*)(oxp_schema *)>;
static Doc parse(const std::string &s) {
  return Doc(oxp_dom_parse(s.data(), s.size(), 0, nullptr, nullptr, 0, nullptr,
                           nullptr),
             oxp_dom_free);
}
static Schema compile(const std::string &s) {
  oxp_schema_error e;
  auto *p = oxp_schema_compile(s.data(), s.size(), &e);
  if (!p)
    std::fprintf(stderr, "schema error: %s\n", e.message);
  return Schema(p, oxp_schema_free);
}
static int emit(void *p, const char *s, size_t n) {
  static_cast<std::string *>(p)->append(s, n);
  return 0;
}
static void validation() {
  const std::string schema =
      R"(<x:schema xmlns:x="http://www.w3.org/2001/XMLSchema"><x:element name="state"><x:complexType><x:sequence><x:element name="item" minOccurs="1" maxOccurs="unbounded"><x:complexType><x:attribute name="n" type="x:short" use="required"/><x:attribute name="ok" type="x:boolean" default="true"/><x:attribute name="id" type="x:ID" use="required"/><x:attribute name="day" type="x:dateTime"/><x:attribute name="label"><x:simpleType><x:restriction base="x:token"><x:enumeration value="hello world"/><x:enumeration value="other"/></x:restriction></x:simpleType></x:attribute></x:complexType></x:element></x:sequence></x:complexType></x:element></x:schema>)";
  auto s = compile(schema);
  CHECK(s);
  if (!s)
    return;
  oxp_schema_error e;
  const char *good[] = {
      "<state><item n=' 32767 ' id='i' label=' hello   world '/></state>",
      "<state><item n='-32768' id='café' ok='0' "
      "day='2024-02-29T24:00:00Z'/></state>"};
  for (const char *x : good) {
    auto d = parse(x);
    CHECK(d);
    CHECK(oxp_schema_validate(s.get(), d.get(), &e) == 0);
    auto *n = oxp_dom_child(oxp_dom_root(d.get()), nullptr, "item");
    CHECK(!std::strcmp(oxp_dom_attr(n, nullptr, "ok"),
                       x == good[0] ? "true" : "0"));
    if (x == good[0]) {
      CHECK(!std::strcmp(oxp_dom_attr(n, nullptr, "n"), "32767"));
      CHECK(!std::strcmp(oxp_dom_attr(n, nullptr, "label"), "hello world"));
      bool def = false;
      for (auto *a = n->attrs; a; a = a->next)
        if (!std::strcmp(a->local, "ok"))
          def = a->defaulted;
      CHECK(def);
      CHECK(oxp_dom_set_attr(n, nullptr, nullptr, "ok", "false", 0) == 0);
      for (auto *a = n->attrs; a; a = a->next)
        if (!std::strcmp(a->local, "ok"))
          CHECK(!a->defaulted);
    }
  }
  const char *bad[] = {
      "<state/>",
      "<state><item id='i'/></state>",
      "<state><item n='32768' id='i'/></state>",
      "<state><item n='3x' id='i'/></state>",
      "<state><item n='1' id='i' ok='yes'/></state>",
      "<state><item n='1' id='i' label='unknown'/></state>",
      "<state><item n='1' id='i' day='2023-02-29T00:00:00Z'/></state>",
      "<state><item n='1' id='i' day='2024-01-01T12:00:00+14:01'/></state>",
      "<state><item n='1' id='i'/><item n='2' id='i'/></state>",
      "<state><item n='1' id='i' extra='x'/></state>",
      "<state><other/><item n='1' id='i'/></state>",
      "<state>not text<item n='1' id='i'/></state>",
      "<state><item n='1' id='1bad'/></state>"};
  for (const char *x : bad) {
    auto d = parse(x);
    CHECK(d);
    CHECK(oxp_schema_validate(s.get(), d.get(), &e) == -1);
    CHECK(e.code == 1);
  }
  std::string unsupported =
      "<x:schema xmlns:x='http://www.w3.org/2001/XMLSchema'><x:element "
      "name='r'><x:complexType><x:choice/></x:complexType></x:element></"
      "x:schema>";
  auto *q = oxp_schema_compile(unsupported.data(), unsupported.size(), &e);
  CHECK(!q);
  CHECK(e.code == 2);
  oxp_schema_free(q);
}
static void keys() {
  auto s = compile(
      R"(<x:schema xmlns:x="http://www.w3.org/2001/XMLSchema"><x:element name="r"><x:complexType><x:sequence><x:element name="a" minOccurs="0" maxOccurs="unbounded"><x:complexType><x:attribute name="id" type="x:string" use="required"/></x:complexType></x:element><x:element name="b" minOccurs="0" maxOccurs="unbounded"><x:complexType><x:attribute name="ref" type="x:string"/></x:complexType></x:element></x:sequence></x:complexType><x:key name="ids"><x:selector xpath="a"/><x:field xpath="@id"/></x:key><x:keyref name="refs" refer="ids"><x:selector xpath="b"/><x:field xpath="@ref"/></x:keyref></x:element></x:schema>)");
  CHECK(s);
  if (!s)
    return;
  oxp_schema_error e;
  auto good = parse("<r><a id='one'/><b ref='one'/></r>");
  CHECK(oxp_schema_validate(s.get(), good.get(), &e) == 0);
  auto dup = parse("<r><a id='one'/><a id='one'/></r>");
  CHECK(oxp_schema_validate(s.get(), dup.get(), &e) == -1);
  auto missing = parse("<r><b ref='absent'/></r>");
  CHECK(oxp_schema_validate(s.get(), missing.get(), &e) == -1);
}
static void mutation() {
  auto d = parse("<r xmlns:p='urn:test'><p:a p:v='yes'>café</p:a></r>\n");
  CHECK(d);
  auto *root = oxp_dom_root(d.get());
  auto *a = oxp_dom_child(root, "urn:test", "a");
  CHECK(a);
  auto foreign = Doc(oxp_dom_new(), oxp_dom_free);
  CHECK(oxp_dom_insert(oxp_dom_document(foreign.get()), a, nullptr) == -2);
  auto *copy = oxp_dom_import(foreign.get(), a, 1);
  CHECK(copy);
  CHECK(oxp_dom_insert(oxp_dom_document(foreign.get()), copy, nullptr) == 0);
  d.reset();
  std::string out;
  CHECK(oxp_dom_write(oxp_dom_document(foreign.get()), emit, &out) == 0);
  auto again = parse(out);
  CHECK(again);
  CHECK(!std::strcmp(oxp_dom_root(again.get())->uri, "urn:test"));
  CHECK(!std::strcmp(oxp_dom_attr(oxp_dom_root(again.get()), "urn:test", "v"),
                     "yes"));
  CHECK(oxp_dom_insert(copy, copy, nullptr) == -2);
  auto *t = oxp_dom_create(foreign.get(), OXP_NODE_ELEMENT, nullptr, nullptr,
                           "tail", nullptr);
  CHECK(t);
  CHECK(oxp_dom_insert(copy, t, copy->first) == 0);
  CHECK(copy->first == t);
  CHECK(oxp_dom_set_text(t, "<& café 😀") == 0);
  CHECK(oxp_dom_set_attr(t, nullptr, nullptr, "value", "a\tb\nc\r&<\"", 0) ==
        0);
  out.clear();
  CHECK(oxp_dom_write(oxp_dom_document(foreign.get()), emit, &out) == 0);
  again = parse(out);
  CHECK(again);
  auto *tail = oxp_dom_child(oxp_dom_root(again.get()), nullptr, "tail");
  CHECK(tail);
  CHECK(!std::strcmp(oxp_dom_attr(tail, nullptr, "value"), "a\tb\nc\r&<\""));
  CHECK(oxp_dom_detach(t) == 0);
  CHECK(copy->first->prev == nullptr);
  CHECK(oxp_dom_create(foreign.get(), OXP_NODE_ELEMENT, nullptr, nullptr,
                       "bad:name", nullptr) == nullptr);
  CHECK(oxp_dom_create(foreign.get(), OXP_NODE_COMMENT, nullptr, nullptr,
                       nullptr, "bad--comment") == nullptr);
  CHECK(oxp_dom_set_attr(copy, nullptr, nullptr, "bad name", "x", 0) == -2);
  CHECK(oxp_dom_set_text(copy, "\xC0\xAF") == -2);
  auto *cd = oxp_dom_create(foreign.get(), OXP_NODE_CDATA, nullptr, nullptr,
                            nullptr, "inside ]]> after");
  CHECK(cd);
  CHECK(oxp_dom_insert(copy, cd, nullptr) == 0);
  out.clear();
  CHECK(oxp_dom_write(oxp_dom_document(foreign.get()), emit, &out) == 0);
  again = parse(out);
  CHECK(again);
}
static void namespaces() {
  auto d = parse(
      "<a xmlns:p='urn:old'><b><p:c/></b><target xmlns:p='urn:new'/></a>");
  auto *r = oxp_dom_root(d.get());
  auto *b = oxp_dom_child(r, nullptr, "b");
  auto *t = oxp_dom_child(r, nullptr, "target");
  CHECK(oxp_dom_insert(t, b, nullptr) == 0);
  std::string out;
  CHECK(oxp_dom_write(oxp_dom_document(d.get()), emit, &out) == 0);
  auto again = parse(out);
  CHECK(again);
  auto *target = oxp_dom_child(oxp_dom_root(again.get()), nullptr, "target");
  auto *branch = oxp_dom_child(target, nullptr, "b");
  CHECK(oxp_dom_child(branch, "urn:old", "c") != nullptr);
  auto *c = oxp_dom_child(b, "urn:old", "c");
  CHECK(oxp_dom_set_attr(c, "urn:conflict", "p", "bad", "x", 0) == -2);
}
static void patterns() {
  auto s = compile(
      R"(<x:schema xmlns:x="http://www.w3.org/2001/XMLSchema"><x:element name="revision"><x:simpleType><x:restriction base="x:string"><x:pattern value="$Revision[:]?[ ]*[\d]*[ ]*$"/></x:restriction></x:simpleType></x:element></x:schema>)");
  CHECK(s);
  if (!s)
    return;
  oxp_schema_error e;
  for (const char *v : {"$Revision:15 $", "$Revision: 130 $", "$Revision$"}) {
    auto d = parse(std::string("<revision>") + v + "</revision>");
    CHECK(oxp_schema_validate(s.get(), d.get(), &e) == 0);
  }
  auto invalid = parse("<revision>$Revision: fail $</revision>");
  CHECK(oxp_schema_validate(s.get(), invalid.get(), &e) == -1);
}
int main() {
  validation();
  keys();
  mutation();
  namespaces();
  patterns();
  std::printf("OXP mutation/schema: %u checks, %u failures\n", checks,
              failures);
  return failures ? 1 : 0;
}

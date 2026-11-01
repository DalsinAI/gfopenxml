/* OXP optional XSD validator. MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "oxp_schema.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using S = std::string;
using N = oxp_node;
/* libstdc++'s std::regex matches depth-first by default: a C++ frame or more for
 * every character of the value, so a float, dateTime, language or pattern value a
 * few hundred characters long overflows a 32-64 KB Amiga stack (no guard page).
 * __polynomial (a libstdc++ extension) picks its breadth-first matcher instead,
 * whose depth is bounded by the expression, not the value; the same values match.
 * It refuses back-references, which pattern() never lets through. */
#ifdef __GLIBCXX__
const std::regex::flag_type RX =
    std::regex::ECMAScript | std::regex_constants::__polynomial;
#else
const std::regex::flag_type RX = std::regex::ECMAScript;
#endif
const S XS = "http://www.w3.org/2001/XMLSchema",
        XI = "http://www.w3.org/2001/XMLSchema-instance";
S val(const char *s) { return s ? s : ""; }
S attr(const N *n, const char *k) { return val(oxp_dom_attr(n, nullptr, k)); }
bool xs(const N *n, const char *t) {
  return n && val(n->uri) == XS && val(n->name) == t;
}
N *child(const N *n, const char *t) { return oxp_dom_child(n, XS.c_str(), t); }
std::vector<N *> children(const N *n) {
  std::vector<N *> r;
  for (N *c = n->first; c; c = c->next)
    if (c->type == OXP_NODE_ELEMENT)
      r.push_back(c);
  return r;
}
struct Failure : std::runtime_error {
  int code;
  unsigned long line;
  Failure(int c, const N *n, const S &m)
      : std::runtime_error(m), code(c), line(n ? n->line : 0) {}
};
void fail(const N *n, const S &m, int code = 1) { throw Failure(code, n, m); }
S ns(const N *n, const S &p) {
  if (p == "xml")
    return "http://www.w3.org/XML/1998/namespace";
  for (; n; n = n->parent)
    for (oxp_dns *x = n->ns; x; x = x->next)
      if (val(x->prefix) == p)
        return val(x->uri);
  return "";
}
using QName = std::pair<S, S>;
QName qname(const N *n, const S &q) {
  size_t i = q.find(':');
  S p = i == S::npos ? "" : q.substr(0, i);
  S u = ns(n, p);
  if (i != S::npos && u.empty())
    fail(n, "Unbound schema prefix: " + p, 2);
  return {u, i == S::npos ? q : q.substr(i + 1)};
}
S replaceSpace(S s) {
  for (char &c : s)
    if (c == '\t' || c == '\n' || c == '\r')
      c = ' ';
  return s;
}
S collapse(S s) {
  s = replaceSpace(s);
  S r;
  bool gap = false;
  for (char c : s) {
    if (c == ' ') {
      gap = !r.empty();
    } else {
      if (gap)
        r += ' ';
      r += c;
      gap = false;
    }
  }
  return r;
}
bool integer(const S &s) {
  if (s.empty())
    return false;
  size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
  if (i == s.size())
    return false;
  for (; i < s.size(); ++i)
    if (s[i] < '0' || s[i] > '9')
      return false;
  return true;
}
S pattern(const N *n, const S &s) {
  S r;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '\\') {
      if (++i == s.size())
        fail(n, "Incomplete XSD pattern", 2);
      if (s[i] == 'd')
        r += "\\d";
      else if (S(".\\?*+{}()[]|-^").find(s[i]) != S::npos) {
        r += '\\';
        r += s[i];
      } else
        fail(n, "Unsupported XSD pattern escape", 2);
    } else if (c == '$' || c == '^') {
      r += '\\';
      r += c;
    } else
      r += c;
  }
  if (s.find("-[") != S::npos)
    fail(n, "XSD character-class subtraction is unsupported", 2);
  try {
    std::regex test(r, RX);
    (void)test;
  } catch (const std::regex_error &) {
    fail(n, "Invalid/unsupported XSD pattern", 2);
  }
  return r;
}
bool dateTime(const S &s) {
  static const std::regex re(
      R"((-?[0-9]{4,})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(\.[0-9]+)?(Z|[+-]([0-9]{2}):([0-9]{2}))?)",
      RX);
  std::smatch m;
  if (!std::regex_match(s, m, re))
    return false;
  long y = std::strtol(m[1].str().c_str(), nullptr, 10);
  int month = std::atoi(m[2].str().c_str()),
      day = std::atoi(m[3].str().c_str()), h = std::atoi(m[4].str().c_str()),
      mi = std::atoi(m[5].str().c_str()), se = std::atoi(m[6].str().c_str());
  if (y == 0 || month < 1 || month > 12 || h > 24 || mi > 59 || se > 59)
    return false;
  int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))
    days[1] = 29;
  if (day < 1 || day > days[month - 1] ||
      (h == 24 &&
       (mi || se ||
        (m[7].matched && m[7].str().find_first_of("123456789") != S::npos))))
    return false;
  if (m[9].matched) {
    int zh = std::atoi(m[9].str().c_str()), zm = std::atoi(m[10].str().c_str());
    if (zh > 14 || zm > 59 || (zh == 14 && zm))
      return false;
  }
  return true;
}
void outputError(oxp_schema_error *e, int code, unsigned long line,
                 const char *m) {
  if (e) {
    e->code = code;
    e->line = line;
    std::snprintf(e->message, sizeof e->message, "%s", m);
  }
}
struct Guard {
  oxp_doc *d;
  ~Guard() { oxp_dom_free(d); }
};
} // namespace
struct oxp_schema {
  oxp_doc *doc = nullptr;
  N *root = nullptr;
  S target;
  bool elementsQualified = false, attrsQualified = false;
  std::map<QName, N *> types, globalElements;
  std::map<const N *, std::regex> patterns;
  ~oxp_schema() { oxp_dom_free(doc); }
};
namespace {
const std::set<S> builtin = {"string",      "normalizedString",
                             "token",       "boolean",
                             "short",       "int",
                             "integer",     "unsignedShort",
                             "unsignedInt", "positiveInteger",
                             "float",       "double",
                             "anyURI",      "language",
                             "ID",          "dateTime"};
const std::set<S> constructs = {
    "schema",       "annotation",  "documentation", "element",
    "complexType",  "sequence",    "attribute",     "any",
    "anyAttribute", "simpleType",  "restriction",   "minInclusive",
    "maxInclusive", "enumeration", "union",         "simpleContent",
    "extension",    "key",         "selector",      "field",
    "keyref",       "pattern"};
const std::map<S, std::set<S>> allowedAttrs = {
    {"schema",
     {"targetNamespace", "elementFormDefault", "attributeFormDefault",
      "version"}},
    {"element", {"name", "type", "minOccurs", "maxOccurs", "form"}},
    {"attribute", {"name", "type", "use", "default", "fixed", "form"}},
    {"complexType", {"name", "mixed"}},
    {"simpleType", {"name"}},
    {"sequence", {"minOccurs", "maxOccurs"}},
    {"any", {"namespace", "minOccurs", "maxOccurs", "processContents"}},
    {"anyAttribute", {"namespace", "processContents"}},
    {"restriction", {"base"}},
    {"extension", {"base"}},
    {"union", {"memberTypes"}},
    {"key", {"name"}},
    {"keyref", {"name", "refer"}},
    {"selector", {"xpath"}},
    {"field", {"xpath"}},
    {"pattern", {"value"}},
    {"minInclusive", {"value"}},
    {"maxInclusive", {"value"}},
    {"enumeration", {"value"}},
    {"simpleContent", {}}};
void knownType(const oxp_schema &s, N *n, const S &t) {
  QName q = qname(n, t);
  if (q.first == XS) {
    if (!builtin.count(q.second))
      fail(n, "Unsupported XSD primitive: " + t, 2);
  } else if (!s.types.count(q))
    fail(n, "Unknown schema type: " + t, 2);
}
const std::map<S, std::set<S>> allowedChildren = {
    {"schema", {"annotation", "simpleType", "complexType", "element"}},
    {"element", {"annotation", "simpleType", "complexType", "key", "keyref"}},
    {"complexType",
     {"annotation", "sequence", "simpleContent", "attribute", "anyAttribute"}},
    {"simpleType", {"annotation", "restriction", "union"}},
    {"restriction",
     {"annotation", "enumeration", "minInclusive", "maxInclusive", "pattern"}},
    {"union", {"annotation", "simpleType"}},
    {"sequence", {"annotation", "element", "any", "sequence"}},
    {"attribute", {"annotation", "simpleType"}},
    {"simpleContent", {"annotation", "extension"}},
    {"extension", {"annotation", "attribute", "anyAttribute"}},
    {"key", {"annotation", "selector", "field"}},
    {"keyref", {"annotation", "selector", "field"}},
    {"any", {"annotation"}},
    {"anyAttribute", {"annotation"}},
    {"minInclusive", {}},
    {"maxInclusive", {}},
    {"pattern", {}},
    {"enumeration", {}},
    {"selector", {}},
    {"field", {}}};
void shape(N *n) {
  const S tag = n->name;
  std::map<S, unsigned> counts;
  for (N *c : children(n)) {
    if (val(c->uri) != XS || !allowedChildren.at(tag).count(c->name))
      fail(c, "Unsupported/misplaced schema construct: " + val(c->name), 2);
    ++counts[c->name];
  }
  for (const char *k : {"simpleContent", "complexType", "sequence",
                        "anyAttribute", "restriction", "union", "selector",
                        "field", "minInclusive", "maxInclusive", "pattern"})
    if (counts[k] > 1 && !(tag == "sequence" && S(k) == "sequence"))
      fail(n, "Repeated schema construct is unsupported: " + S(k), 2);
  if ((tag == "element" || tag == "attribute") && counts["simpleType"] > 1)
    fail(n, "Multiple simple types", 2);
  if (tag == "simpleType" && counts["restriction"] + counts["union"] != 1)
    fail(n, "Expected one simple-type definition", 2);
  if (tag == "simpleContent" && counts["extension"] != 1)
    fail(n, "Expected one simple-content extension", 2);
  if (tag == "complexType" && counts["sequence"] && counts["simpleContent"])
    fail(n, "Conflicting content declarations", 2);
  if ((tag == "element" || tag == "attribute") && !attr(n, "type").empty() &&
      (counts["simpleType"] || counts["complexType"]))
    fail(n, "Conflicting inline/named types", 2);
  if (tag == "element" && counts["simpleType"] && counts["complexType"])
    fail(n, "Conflicting inline types", 2);
  for (const char *k : {"minOccurs", "maxOccurs"}) {
    S v = attr(n, k);
    if (v.empty() || (S(k) == "maxOccurs" && v == "unbounded"))
      continue;
    if (v.find_first_not_of("0123456789") != S::npos || v.empty())
      fail(n, "Invalid occurrence bound", 2);
    errno = 0;
    std::strtoul(v.c_str(), nullptr, 10);
    if (errno)
      fail(n, "Occurrence bound overflow", 2);
  }
  S min = attr(n, "minOccurs"), max = attr(n, "maxOccurs");
  if (max != "unbounded" &&
      std::strtoul(min.empty() ? "1" : min.c_str(), nullptr, 10) >
          std::strtoul(max.empty() ? "1" : max.c_str(), nullptr, 10))
    fail(n, "Invalid occurrence range", 2);
  for (const char *k : {"form", "elementFormDefault", "attributeFormDefault"}) {
    S v = attr(n, k);
    if (!v.empty() && v != "qualified" && v != "unqualified")
      fail(n, "Invalid namespace form", 2);
  }
  if (tag == "attribute") {
    S use = attr(n, "use");
    if (!use.empty() && use != "required" && use != "optional" &&
        use != "prohibited")
      fail(n, "Invalid attribute use", 2);
    if (oxp_dom_attr(n, nullptr, "default") &&
        (use == "required" || use == "prohibited" ||
         oxp_dom_attr(n, nullptr, "fixed")))
      fail(n, "Invalid attribute default", 2);
  }
  if (tag == "key" || tag == "keyref") {
    if (counts["selector"] != 1 || counts["field"] != 1)
      fail(n, "Only single-field identity constraints supported", 2);
  }
  if (tag == "selector" || tag == "field") {
    S path = attr(n, "xpath");
    if (path.empty() || path.find_first_of(".[]*| ") != S::npos ||
        (tag == "field" && (path[0] != '@' || path.find('/') != S::npos)))
      fail(n, "Unsupported identity XPath", 2);
  }
}
void audit(oxp_schema &s, N *n) {
  if (xs(n, "annotation"))
    return;
  if (val(n->uri) != XS || !constructs.count(val(n->name)))
    fail(n, "Unsupported schema construct: " + val(n->name), 2);
  shape(n);
  for (oxp_dattr *a = n->attrs; a; a = a->next) {
    if (val(a->uri) == "http://www.w3.org/XML/1998/namespace")
      continue;
    if (a->uri || !allowedAttrs.at(n->name).count(a->local))
      fail(n, "Unsupported schema attribute: " + val(a->local), 2);
  }
  for (const char *k : {"type", "base"})
    if (!attr(n, k).empty())
      knownType(s, n, attr(n, k));
  if (xs(n, "union")) {
    std::istringstream in(attr(n, "memberTypes"));
    S t;
    while (in >> t)
      knownType(s, n, t);
  }
  if (xs(n, "pattern"))
    s.patterns[n] = std::regex(pattern(n, attr(n, "value")), RX);
  if (xs(n, "any") || xs(n, "anyAttribute")) {
    S pc = attr(n, "processContents");
    if (pc != "skip" && !(xs(n, "anyAttribute") && pc == "lax"))
      fail(n, "Only explicit skip/lax wildcards are supported", 2);
  }
  if (xs(n, "complexType") && attr(n, "mixed") == "true")
    fail(n, "Mixed schema content is unsupported", 2);
  for (N *c : children(n))
    audit(s, c);
}
bool wildcard(const oxp_schema &s, N *rule, const S &u) {
  S a = attr(rule, "namespace");
  if (a.empty() || a == "##any")
    return true;
  std::istringstream in(a);
  S token;
  while (in >> token) {
    if (token == "##local" && u.empty())
      return true;
    if (token == "##targetNamespace" && u == s.target)
      return true;
    if (token == "##other" && !u.empty() && u != s.target)
      return true;
    if (token == u)
      return true;
  }
  return false;
}
S declaredNS(const oxp_schema &s, N *decl, bool attribute = false) {
  S form = attr(decl, "form");
  bool qualified =
      form == "qualified" ||
      (form.empty() && (attribute ? s.attrsQualified : s.elementsQualified));
  if (decl->parent == s.root && !attribute)
    qualified = true;
  return qualified ? s.target : "";
}
struct Validation {
  const oxp_schema &s;
  std::set<S> ids;
  bool primitive(const S &type, S &v) {
    if (type != "string") {
      v = replaceSpace(v);
      if (type != "normalizedString")
        v = collapse(v);
    }
    if (type == "string" || type == "normalizedString" || type == "token" ||
        type == "anyURI")
      return true;
    if (type == "boolean")
      return v == "true" || v == "false" || v == "0" || v == "1";
    if (type == "language") {
      static const std::regex r("[A-Za-z]{1,8}(-[A-Za-z0-9]{1,8})*", RX);
      return std::regex_match(v, r);
    }
    if (type == "ID") {
      if (v.empty() || v.find(':') != S::npos)
        return false;
      S xml = "<" + v + "/>";
      oxp_doc *d = oxp_dom_parse(xml.data(), xml.size(), 0, nullptr, nullptr, 0,
                                 nullptr, nullptr);
      if (!d)
        return false;
      oxp_dom_free(d);
      return true;
    }
    if (type == "dateTime")
      return dateTime(v);
    if (type == "float" || type == "double") {
      static const std::regex r(
          "[+-]?([0-9]+(\\.[0-9]*)?|\\.[0-9]+)([eE][+-]?[0-9]+)?", RX);
      return v == "INF" || v == "-INF" || v == "NaN" || std::regex_match(v, r);
    }
    if (!integer(v))
      return false;
    if (type == "integer" || type == "positiveInteger") {
      if (type == "positiveInteger") {
        if (v[0] == '-')
          return false;
        return v.find_first_of("123456789") != S::npos;
      }
      return true;
    }
    // Integer bounds need no soft-float conversion on 68040 targets.
    const bool negative = v[0] == '-';
    const char *digits = v.c_str() + ((v[0] == '-' || v[0] == '+') ? 1 : 0);
    errno = 0;
    char *end = nullptr;
    unsigned long x = std::strtoul(digits, &end, 10);
    if (errno || !end || *end)
      return false;
    if (type == "short")
      return x <= (negative ? 32768UL : 32767UL);
    if (type == "int")
      return x <= (negative ? 2147483648UL : 2147483647UL);
    if (type == "unsignedShort")
      return (!negative || x == 0) && x <= 65535UL;
    if (type == "unsignedInt")
      return (!negative || x == 0) && x <= 4294967295UL;
    return false;
  }
  bool simple(N *rule, S &v, unsigned depth = 0) {
    if (depth > 64)
      fail(rule, "Schema type recursion limit", 2);
    if (xs(rule, "simpleType")) {
      N *r = child(rule, "restriction");
      if (r)
        return simple(r, v, depth + 1);
      N *u = child(rule, "union");
      if (u) {
        std::istringstream in(attr(u, "memberTypes"));
        S t;
        while (in >> t) {
          S trial = v;
          if (named(u, t, trial, depth + 1)) {
            v = trial;
            return true;
          }
        }
        for (N *c : children(u))
          if (xs(c, "simpleType")) {
            S trial = v;
            if (simple(c, trial, depth + 1)) {
              v = trial;
              return true;
            }
          }
        return false;
      }
      fail(rule, "Unsupported simpleType definition", 2);
    }
    if (xs(rule, "restriction")) {
      if (!named(rule, attr(rule, "base"), v, depth + 1))
        return false;
      bool hasEnum = false, enumOK = false;
      for (N *c : children(rule)) {
        if (xs(c, "annotation"))
          continue;
        S limit = attr(c, "value");
        if (xs(c, "enumeration")) {
          hasEnum = true;
          enumOK |= v == limit;
        } else if (xs(c, "minInclusive")) {
          if (std::strtod(v.c_str(), nullptr) <
              std::strtod(limit.c_str(), nullptr))
            return false;
        } else if (xs(c, "maxInclusive")) {
          if (std::strtod(v.c_str(), nullptr) >
              std::strtod(limit.c_str(), nullptr))
            return false;
        } else if (xs(c, "pattern")) {
          auto i = s.patterns.find(c);
          if (i == s.patterns.end())
            fail(c, "Missing compiled XSD pattern", 2);
          if (!std::regex_search(v, i->second))
            return false;
        } else
          fail(c, "Unsupported restriction content", 2);
      }
      return !hasEnum || enumOK;
    }
    fail(rule, "Expected a simple type", 2);
    return false;
  }
  bool named(N *ctx, const S &t, S &v, unsigned depth = 0) {
    if (depth > 64)
      fail(ctx, "Schema type recursion limit", 2);
    QName q = qname(ctx, t);
    if (q.first == XS)
      return primitive(q.second, v);
    auto i = s.types.find(q);
    if (i == s.types.end())
      fail(ctx, "Unknown schema type: " + t, 2);
    return simple(i->second, v, depth + 1);
  }
  bool value(N *decl, S &v) {
    S type = attr(decl, "type");
    if (!type.empty())
      return named(decl, type, v);
    N *r = child(decl, "simpleType");
    return r ? simple(r, v) : true;
  }
  void attributes(N *type, N *el) {
    std::set<QName> seen;
    N *any = child(type, "anyAttribute");
    for (N *r : children(type))
      if (xs(r, "attribute")) {
        S u = declaredNS(s, r, true), name = attr(r, "name");
        QName key = {u, name};
        if (!seen.insert(key).second)
          fail(r, "Duplicate attribute declaration", 2);
        const char *original =
            oxp_dom_attr(el, u.empty() ? nullptr : u.c_str(), name.c_str());
        S v = val(original);
        bool defaulted = !original;
        const char *def = oxp_dom_attr(r, nullptr, "default");
        const char *fixed = oxp_dom_attr(r, nullptr, "fixed");
        if (!original) {
          if (attr(r, "use") == "required")
            fail(el, "Missing required attribute: " + name);
          if (!def && !fixed)
            continue;
          v = def ? def : fixed;
        }
        if (attr(r, "use") == "prohibited")
          fail(el, "Prohibited attribute: " + name);
        if (!value(r, v))
          fail(el, "Invalid value for attribute " + name + ": " + v);
        if (fixed && v != fixed)
          fail(el, "Incorrect fixed attribute: " + name);
        S typ = attr(r, "type");
        if (!typ.empty() && qname(r, typ) == QName(XS, "ID") &&
            !ids.insert(v).second)
          fail(el, "Duplicate XML ID: " + v);
        if (defaulted || v != original) {
          const char *prefix = nullptr;
          S chosen;
          if (!u.empty()) {
            if (val(el->uri) == u && el->prefix)
              chosen = el->prefix;
            else
              chosen = "oxs";
            prefix = chosen.c_str();
          }
          unsigned flag = defaulted ? 1 : 0;
          if (!defaulted)
            for (oxp_dattr *a = el->attrs; a; a = a->next)
              if (val(a->uri) == u && val(a->local) == name)
                flag = a->defaulted;
          if (oxp_dom_set_attr(el, u.empty() ? nullptr : u.c_str(), prefix,
                               name.c_str(), v.c_str(), flag))
            fail(el, "Cannot store schema attribute", 3);
        }
      }
    for (oxp_dattr *a = el->attrs; a; a = a->next) {
      S u = val(a->uri);
      if (u == XI) {
        S n = a->local;
        if (n == "schemaLocation" || n == "noNamespaceSchemaLocation")
          continue;
        fail(el, "Unsupported schema-instance attribute: " + n);
      }
      if (!seen.count({u, a->local}) && (!any || !wildcard(s, any, u)))
        fail(el, "Unexpected attribute: " + val(a->local));
    }
  }
  void sequence(N *seq, N *el, const std::vector<N *> &nodes, size_t &at,
                unsigned depth) {
    for (N *r : children(seq)) {
      if (xs(r, "annotation"))
        continue;
      if (!xs(r, "element") && !xs(r, "any") && !xs(r, "sequence"))
        fail(r, "Unsupported sequence particle", 2);
      S minS = attr(r, "minOccurs"), maxS = attr(r, "maxOccurs");
      unsigned long min = minS.empty()
                              ? 1
                              : std::strtoul(minS.c_str(), nullptr, 10),
                    max = maxS.empty()
                              ? 1
                              : (maxS == "unbounded"
                                     ? ULONG_MAX
                                     : std::strtoul(maxS.c_str(), nullptr, 10));
      unsigned long count = 0;
      if (xs(r, "sequence")) {
        if (min != 1 || max != 1)
          fail(r, "Repeated/nested optional sequences unsupported", 2);
        sequence(r, el, nodes, at, depth + 1);
        continue;
      }
      while (at < nodes.size() && count < max) {
        N *n = nodes[at];
        bool match = xs(r, "any") ? wildcard(s, r, val(n->uri))
                                  : (val(n->name) == attr(r, "name") &&
                                     val(n->uri) == declaredNS(s, r));
        if (!match)
          break;
        if (xs(r, "element"))
          element(r, n, depth + 1);
        ++at;
        ++count;
      }
      if (count < min)
        fail(el, "Missing or out-of-order element: " +
                     (xs(r, "any") ? S("wildcard") : attr(r, "name")));
    }
  }
  void complex(N *type, N *el, unsigned depth) {
    N *simpleContent = child(type, "simpleContent");
    N *base = type;
    if (simpleContent) {
      base = child(simpleContent, "extension");
      if (!base)
        fail(simpleContent, "Unsupported simpleContent", 2);
      if (!children(el).empty())
        fail(el, "Simple content contains child elements");
      S text;
      size_t z = oxp_dom_text(el, nullptr, 0);
      text.resize(z);
      std::vector<char> buf(z + 1);
      oxp_dom_text(el, buf.data(), buf.size());
      text = buf.data();
      if (!named(base, attr(base, "base"), text))
        fail(el, "Invalid simple content");
      if (text != buf.data() && oxp_dom_set_text(el, text.c_str()))
        fail(el, "Cannot normalise text", 3);
    } else {
      for (N *c = el->first; c; c = c->next)
        if ((c->type == OXP_NODE_TEXT || c->type == OXP_NODE_CDATA) &&
            !collapse(val(c->text)).empty())
          fail(el, "Unexpected text in element-only content");
      auto nodes = children(el);
      size_t at = 0;
      N *seq = child(type, "sequence");
      if (seq)
        sequence(seq, el, nodes, at, depth);
      if (at != nodes.size())
        fail(nodes[at], "Unexpected child element: " + val(nodes[at]->name));
    }
    attributes(base, el);
  }
  std::vector<N *> select(N *root, N *ctx, const S &path) {
    std::vector<N *> current = {root};
    std::istringstream in(path);
    S part;
    while (std::getline(in, part, '/')) {
      if (part.empty() || part.find('@') != S::npos ||
          part.find('.') != S::npos)
        fail(ctx, "Unsupported identity XPath", 2);
      QName q = qname(ctx, part);
      std::vector<N *> next;
      for (N *n : current)
        for (N *c : children(n))
          if (val(c->uri) == q.first && val(c->name) == q.second)
            next.push_back(c);
      current.swap(next);
    }
    return current;
  }
  void identities(N *decl, N *el) {
    std::map<QName, std::set<S>> keys;
    for (int pass = 0; pass < 2; ++pass)
      for (N *r : children(decl))
        if (xs(r, pass ? "keyref" : "key")) {
          N *selector = child(r, "selector"), *field = child(r, "field");
          if (!selector || !field)
            fail(r, "Incomplete identity constraint", 2);
          S xp = attr(field, "xpath");
          if (xp.empty() || xp[0] != '@' || xp.find('/') != S::npos)
            fail(field, "Only attribute identity fields supported", 2);
          QName fq = qname(field, xp.substr(1));
          QName k = pass ? qname(r, attr(r, "refer"))
                         : QName(s.target, attr(r, "name"));
          if (pass && !keys.count(k))
            fail(r, "Unknown key reference", 2);
          for (N *n : select(el, selector, attr(selector, "xpath"))) {
            const char *v =
                oxp_dom_attr(n, fq.first.empty() ? nullptr : fq.first.c_str(),
                             fq.second.c_str());
            if (!v) {
              if (!pass)
                fail(n, "Missing key field");
              continue;
            }
            if (pass) {
              if (!keys[k].count(v))
                fail(n, "Unresolved key reference: " + S(v));
            } else if (!keys[k].insert(v).second)
              fail(n, "Duplicate identity key: " + S(v));
          }
          if (!pass)
            keys[k];
        }
  }
  void element(N *decl, N *el, unsigned depth = 0) {
    if (depth > 256)
      fail(el, "Schema validation depth limit");
    if (attr(decl, "name") != val(el->name) ||
        declaredNS(s, decl) != val(el->uri))
      fail(el, "Element namespace/name mismatch");
    N *t = child(decl, "complexType");
    S namedType = attr(decl, "type");
    if (!t && !namedType.empty()) {
      auto i = s.types.find(qname(decl, namedType));
      if (i != s.types.end() && xs(i->second, "complexType"))
        t = i->second;
    }
    if (t)
      complex(t, el, depth);
    else {
      if (!children(el).empty())
        fail(el, "Simple element has children");
      std::vector<char> buf(oxp_dom_text(el, nullptr, 0) + 1);
      oxp_dom_text(el, buf.data(), buf.size());
      S v = buf.data();
      if (!value(decl, v))
        fail(el, "Invalid element value");
      if (v != buf.data() && oxp_dom_set_text(el, v.c_str()))
        fail(el, "Cannot normalise element", 3);
      for (oxp_dattr *a = el->attrs; a; a = a->next)
        if (val(a->uri) != XI)
          fail(el, "Unexpected simple-element attribute");
    }
    identities(decl, el);
  }
};
} // namespace
extern "C" oxp_schema *oxp_schema_compile(const void *data, size_t len,
                                          oxp_schema_error *error) {
  outputError(error, 0, 0, "");
  try {
    std::unique_ptr<oxp_schema> s(new oxp_schema);
    char msg[512];
    unsigned long line = 0;
    oxp_code c;
    s->doc = oxp_dom_parse(data, len, 0, &c, msg, sizeof msg, &line, nullptr);
    if (!s->doc)
      throw Failure(2, nullptr, msg);
    s->root = oxp_dom_root(s->doc);
    if (!xs(s->root, "schema"))
      fail(s->root, "Expected xs:schema", 2);
    s->target = attr(s->root, "targetNamespace");
    s->elementsQualified = attr(s->root, "elementFormDefault") == "qualified";
    s->attrsQualified = attr(s->root, "attributeFormDefault") == "qualified";
    for (N *n : children(s->root)) {
      if (xs(n, "simpleType") || xs(n, "complexType")) {
        if (!s->types.emplace(QName(s->target, attr(n, "name")), n).second)
          fail(n, "Duplicate schema type", 2);
      } else if (xs(n, "element")) {
        if (!s->globalElements.emplace(QName(s->target, attr(n, "name")), n)
                 .second)
          fail(n, "Duplicate global element", 2);
      }
    }
    audit(*s, s->root);
    return s.release();
  } catch (const Failure &e) {
    outputError(error, e.code, e.line, e.what());
  } catch (const std::bad_alloc &) {
    outputError(error, 3, 0, "Out of memory");
  } catch (const std::exception &e) {
    outputError(error, 2, 0, e.what());
  }
  return nullptr;
}
extern "C" void oxp_schema_free(oxp_schema *s) { delete s; }
extern "C" int oxp_schema_validate(const oxp_schema *s, oxp_doc *d,
                                   oxp_schema_error *error) {
  outputError(error, 0, 0, "");
  try {
    if (!s || !d)
      fail(nullptr, "Missing schema/document", 2);
    N *n = oxp_dom_root(d);
    if (!n)
      fail(n, "No document element");
    auto i = s->globalElements.find({val(n->uri), val(n->name)});
    if (i == s->globalElements.end())
      fail(n, "No matching global declaration");
    Validation v{*s, {}};
    v.element(i->second, n);
    return 0;
  } catch (const Failure &e) {
    outputError(error, e.code, e.line, e.what());
  } catch (const std::bad_alloc &) {
    outputError(error, 3, 0, "Out of memory");
  } catch (const std::exception &e) {
    outputError(error, 1, 0, e.what());
  }
  return -1;
}

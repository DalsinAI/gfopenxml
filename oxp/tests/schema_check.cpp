/* Standalone schema/fixture runner. MIT, AmigaChrome Team 2026. */
#include "oxp_schema.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  std::ifstream f(argv[1], std::ios::binary);
  std::string schema((std::istreambuf_iterator<char>(f)), {});
  oxp_schema_error e;
  oxp_schema *s = oxp_schema_compile(schema.data(), schema.size(), &e);
  if (!s) {
    std::fprintf(stderr, "SCHEMA %lu: %s\n", e.line, e.message);
    return 2;
  }
  int bad = 0;
  for (int i = 2; i < argc; ++i) {
    std::ifstream g(argv[i], std::ios::binary);
    std::string xml((std::istreambuf_iterator<char>(g)), {});
    oxp_code c;
    char msg[512];
    oxp_doc *d = oxp_dom_parse(xml.data(), xml.size(), 0, &c, msg, sizeof msg,
                               nullptr, nullptr);
    if (!d) {
      std::fprintf(stderr, "XML %s: %s\n", argv[i], msg);
      ++bad;
      continue;
    }
    if (oxp_schema_validate(s, d, &e)) {
      std::fprintf(stderr, "INVALID %s:%lu: %s\n", argv[i], e.line, e.message);
      ++bad;
    } else
      std::printf("VALID %s\n", argv[i]);
    oxp_dom_free(d);
  }
  oxp_schema_free(s);
  return bad ? 1 : 0;
}

/* Compare integer validation through the public schema API.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 AmigaChrome Team */
#include "oxp_schema.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/time.h>
#ifndef BENCH_LABEL
#define BENCH_LABEL "current"
#endif
int main() {
  const char *xsd="<x:schema xmlns:x='http://www.w3.org/2001/XMLSchema'><x:element name='r'><x:complexType><x:sequence><x:element name='item' maxOccurs='unbounded'><x:complexType><x:attribute name='v' type='x:unsignedInt' use='required'/></x:complexType></x:element></x:sequence></x:complexType></x:element></x:schema>";
  oxp_schema_error error;
  oxp_schema *schema=oxp_schema_compile(xsd,std::strlen(xsd),&error);
  if (!schema) return 2;
  std::string xml="<r>";
  for(unsigned i=0;i<256;++i) xml += "<item v='4294967295'/>";
  xml += "</r>";
  oxp_doc *doc=oxp_dom_parse(xml.data(),xml.size(),0,nullptr,nullptr,0,nullptr,nullptr);
  if (!doc) { oxp_schema_free(schema); return 2; }
  timeval begin,end;
  if (gettimeofday(&begin,nullptr)) return 2;
  unsigned failures=0;
  for(unsigned i=0;i<4;++i) if(oxp_schema_validate(schema,doc,&error)) ++failures;
  if (gettimeofday(&end,nullptr)) return 2;
  double seconds=(end.tv_sec-begin.tv_sec)+(end.tv_usec-begin.tv_usec)/1000000.0;
  std::printf("integer range %s: 1024 values, %.6f seconds, %u failures\n",BENCH_LABEL,seconds,failures);
  oxp_dom_free(doc);
  oxp_schema_free(schema);
  return failures ? 1 : 0;
}

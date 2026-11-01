/* Optional XSD subset validator for OXP trees. MIT, AmigaChrome Team 2026.
 * Supported constructs are documented in SCHEMA.md. Unsupported schema
 * constructs fail at compilation; there is no silently non-validating mode.
 * No external documents, schemas, DTDs or entities are fetched. */
#ifndef OXP_SCHEMA_H
#define OXP_SCHEMA_H
#include "oxp_dom.h"
#ifdef __cplusplus
extern "C" {
#endif
struct oxp_schema;
typedef struct oxp_schema oxp_schema;
typedef struct oxp_schema_error {
  int code; /* 1 invalid document, 2 unsupported/invalid schema, 3 allocation */
  unsigned long line;
  char message[512];
} oxp_schema_error;
oxp_schema *oxp_schema_compile(const void *data, size_t len,
                               oxp_schema_error *error);
void oxp_schema_free(oxp_schema *schema);
/* Validates and supplies defaults/normalised values in the owning tree.
 * Returns 0 on success, -1 on error. A failed document may contain defaults
 * applied before the error; callers should discard failed documents. */
int oxp_schema_validate(const oxp_schema *, oxp_doc *, oxp_schema_error *error);
#ifdef __cplusplus
}
#endif
#endif

# Editable trees and optional schema validation

The streaming XML parser remains C99. The editable tree API is also C99.
The optional XSD module is C++17 with a C interface in `oxp_schema.h`; programs
which do not use it need no C++ runtime or schema machinery.

Build with `sh oxp/build-schema.sh host` or `sh oxp/build-schema.sh amiga`.
The latter uses the same Open3 soft-68040 configuration as the existing
`build-amiga.sh`. `OS32_GCC16` selects the compiler root and `OUT` the output.
Applications may instead compile the module with their own 68040/FPU flags.
Link C++ consumers with `-loxpschema -loxp -lm`.

## Tree ownership and editing

`oxp_dom_new`, `oxp_dom_create`, `oxp_dom_insert`, `oxp_dom_detach`,
`oxp_dom_set_attr`, `oxp_dom_remove_attr`, `oxp_dom_set_text` and
`oxp_dom_import` operate on the existing OXP tree. Nodes belong to their document
arena, including detached/replaced nodes and strings, until `oxp_dom_free`.
Long-lived documents should be periodically serialized/reparsed if edits are
unbounded. Freeing a document frees all its arena storage. Import is the explicit
cross-document operation; inserting a foreign node or creating a cycle fails.
Moving/importing subtrees preserves inherited namespace bindings. XML names,
UTF-8 values and reserved namespaces are checked. The writer splits CDATA
terminators so modified CDATA remains valid XML.

`oxp_node.owner` and `oxp_dattr.defaulted` extend public structs: rebuild clients
rather than mixing old/new struct layouts. Parsed DTD defaults and newly applied
schema defaults are distinguishable from explicitly supplied attributes. The
normal writer writes all attributes; applications that need to preserve the
specified/defaulted distinction on reload should omit defaulted attributes and
validate again, as Enigma's adapter does.

## Schema scope

`oxp_schema_compile` reads a caller-supplied schema with OXP.
`oxp_schema_validate` validates a tree and adds schema defaults and normalized
values. A failed tree may contain defaults applied before the failure; discard
it. Schema compilation reports unsupported constructs explicitly. No network
or external-resource loader is supplied by this module.

Supported: global/local elements; inline/named simple and complex types;
ordered sequences with element/skip-wildcard occurrence bounds; required,
optional and default/fixed attributes; namespace form qualification; string,
normalizedString, token, boolean, signed/unsigned 16/32-bit integers, integer,
positiveInteger, float/double lexical forms, dateTime, language, ID and anyURI;
restrictions with enumeration, inclusive bounds and one pattern; unions;
simple-content extensions; skip wildcards and lax attribute wildcards; and
single-attribute keys/keyrefs with child-path selectors. IDs are checked for
uniqueness when declared directly as ID. The implemented pattern dialect covers
Enigma's ASCII digit patterns, with XSD's literal dollar/hat and substring
semantics; Unicode category escapes and class subtraction are rejected.

This is a defined XSD subset, not a complete XML Schema implementation. Choice,
all, imports/includes, schema inheritance beyond simple-content extension,
multi-field identities, general XPath, Unicode regex categories and XSLT are
not implemented. Lax wildcard attributes are accepted without declarations;
global attribute declarations are currently unsupported. The seven schemas of
the pinned Enigma source use the supported subset. The test records describe
actual fixture and target coverage separately from these API capabilities.

## Additional input encoding

Windows-1252 (`windows-1252` or `cp1252`) is supported for Enigma's legacy levels.
Its undefined bytes are rejected. The existing UTF-8, UTF-16, Latin-1 and ASCII
behaviour remains covered by the original cases; two Windows-1252 cases also
run whole and in 1-, 3- and 7-byte chunks.

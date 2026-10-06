#include "graph/code_languages.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace apogee::graph {
namespace {

// ---- The queries -------------------------------------------------------------
//
// One per grammar, in the capture vocabulary code_languages.h names. Each was
// written against the pinned grammar's node types (its src/node-types.json)
// and is held by the per-language golden fixtures under
// tests/fixtures/code_graph/: a pin move that renames a node type fails the
// query's compilation in `CodeParser`'s constructor, and one that changes a
// tree's shape fails a golden.

constexpr std::string_view kCQuery = R"QUERY(
(struct_specifier name: (_) @name body: (_) @body) @definition.class
(union_specifier name: (_) @name body: (_) @body) @definition.class
(enum_specifier name: (_) @name body: (_) @body) @definition.class
(type_definition type: (struct_specifier !name body: (_) @body) declarator: (type_identifier) @name) @definition.class
(type_definition type: [(primitive_type) (type_identifier) (sized_type_specifier)] declarator: (type_identifier) @name) @definition.class

(function_definition (storage_class_specifier)? @static type: (_)? @return.type declarator: (function_declarator declarator: (identifier) @name) body: (_) @body) @definition.function
(function_definition (storage_class_specifier)? @static type: (_)? @return.type declarator: (pointer_declarator declarator: (function_declarator declarator: (identifier) @name)) body: (_) @body) @definition.function
(declaration (storage_class_specifier)? @static type: (_)? @return.type declarator: (function_declarator declarator: (identifier) @name)) @declaration.function
(declaration (storage_class_specifier)? @static type: (_)? @return.type declarator: (pointer_declarator declarator: (function_declarator declarator: (identifier) @name))) @declaration.function

(call_expression function: (_) @call.target) @call

(preproc_include path: (_) @import.path) @import

(parameter_declaration type: (_) @param.type declarator: [(identifier) @param.name (pointer_declarator declarator: (identifier) @param.name)])
(declaration type: (_) @local.type declarator: [(identifier) @local.name (pointer_declarator declarator: (identifier) @local.name) (init_declarator declarator: [(identifier) @local.name (pointer_declarator declarator: (identifier) @local.name)])])
(field_declaration type: (_) @field.type declarator: [(field_identifier) @field.name (pointer_declarator declarator: (field_identifier) @field.name)])
)QUERY";

constexpr std::string_view kCppQuery = R"QUERY(
(namespace_definition name: (_) @name body: (_) @body) @definition.module
(namespace_definition !name body: (_) @body) @scope.anonymous

(class_specifier name: (_) @name body: (_) @body) @definition.class
(struct_specifier name: (_) @name body: (_) @body) @definition.class
(union_specifier name: (_) @name body: (_) @body) @definition.class
(enum_specifier name: (_) @name body: (_) @body) @definition.class
(alias_declaration name: (_) @name) @definition.class
(type_definition type: [(primitive_type) (type_identifier) (qualified_identifier) (template_type) (sized_type_specifier)] declarator: (type_identifier) @name) @definition.class

(function_definition (storage_class_specifier)? @static type: (_)? @return.type declarator: (function_declarator declarator: (_) @name) body: (_) @body) @definition.function
(function_definition (storage_class_specifier)? @static type: (_)? @return.type declarator: (reference_declarator (function_declarator declarator: (_) @name)) body: (_) @body) @definition.function
(function_definition (storage_class_specifier)? @static type: (_)? @return.type declarator: (pointer_declarator declarator: (function_declarator declarator: (_) @name)) body: (_) @body) @definition.function
(declaration (storage_class_specifier)? @static type: (_)? @return.type declarator: (function_declarator declarator: (_) @name)) @declaration.function
(field_declaration (storage_class_specifier)? @static type: (_)? @return.type declarator: (function_declarator declarator: (_) @name)) @declaration.function

(class_specifier name: (_) @inherit.child (base_class_clause [(type_identifier) (qualified_identifier) (template_type)] @inherit.base))
(struct_specifier name: (_) @inherit.child (base_class_clause [(type_identifier) (qualified_identifier) (template_type)] @inherit.base))

(call_expression function: (_) @call.target) @call
(new_expression type: (_) @call.target) @call

(preproc_include path: (_) @import.path) @import
(using_declaration (qualified_identifier) @import.path) @import.symbol
(using_declaration "namespace" [(identifier) (qualified_identifier)] @import.path) @import.namespace

(parameter_declaration type: (_) @param.type declarator: [(identifier) @param.name (reference_declarator (identifier) @param.name) (pointer_declarator declarator: (identifier) @param.name)])
(optional_parameter_declaration type: (_) @param.type declarator: [(identifier) @param.name (reference_declarator (identifier) @param.name) (pointer_declarator declarator: (identifier) @param.name)])
(declaration type: (_) @local.type declarator: [(identifier) @local.name (init_declarator declarator: [(identifier) @local.name (reference_declarator (identifier) @local.name) (pointer_declarator declarator: (identifier) @local.name)])])
(field_declaration type: (_) @field.type declarator: [(field_identifier) @field.name (pointer_declarator declarator: (field_identifier) @field.name) (reference_declarator (field_identifier) @field.name)])
)QUERY";

constexpr std::string_view kPythonQuery = R"QUERY(
(class_definition name: (identifier) @name body: (_) @body) @definition.class
(function_definition name: (identifier) @name return_type: (_)? @return.type body: (_) @body) @definition.function

(class_definition name: (identifier) @inherit.child superclasses: (argument_list [(identifier) (attribute)] @inherit.base))

(call function: (_) @call.target) @call

(import_statement name: (dotted_name) @import.path) @import
(import_statement name: (aliased_import name: (dotted_name) @import.path alias: (identifier) @import.alias)) @import
(import_from_statement module_name: (_) @import.path name: (dotted_name) @import.name) @import
(import_from_statement module_name: (_) @import.path name: (aliased_import name: (dotted_name) @import.name alias: (identifier) @import.alias)) @import
(import_from_statement module_name: (_) @import.path (wildcard_import)) @import.namespace

(typed_parameter (identifier) @param.name type: (_) @param.type)
(typed_default_parameter name: (identifier) @param.name type: (_) @param.type)
(assignment left: (identifier) @local.name type: (_) @local.type)
)QUERY";

constexpr std::string_view kJavaScriptQuery = R"QUERY(
(class_declaration name: (_) @name body: (_) @body) @definition.class
(function_declaration name: (identifier) @name body: (_) @body) @definition.function
(generator_function_declaration name: (identifier) @name body: (_) @body) @definition.function
(method_definition name: (_) @name body: (_) @body) @definition.function
(variable_declarator name: (identifier) @name value: [(arrow_function body: (_) @body) (function_expression body: (_) @body)]) @definition.function

(class_declaration name: (_) @inherit.child (class_heritage [(identifier) (member_expression)] @inherit.base))

(call_expression function: (_) @call.target) @call
(new_expression constructor: (_) @call.target) @call

(import_statement source: (string (string_fragment) @import.path)) @import
(import_statement (import_clause (identifier) @import.alias) source: (string (string_fragment) @import.path)) @import
(import_statement (import_clause (namespace_import (identifier) @import.alias)) source: (string (string_fragment) @import.path)) @import
(import_statement (import_clause (named_imports (import_specifier name: (_) @import.name !alias))) source: (string (string_fragment) @import.path)) @import
(import_statement (import_clause (named_imports (import_specifier name: (_) @import.name alias: (_) @import.alias))) source: (string (string_fragment) @import.path)) @import
(variable_declarator name: (identifier) @import.alias value: (call_expression function: (identifier) @_require arguments: (arguments . (string (string_fragment) @import.path))) (#eq? @_require "require")) @import

(jsx_opening_element name: [(identifier) (member_expression)] @call.target) @call
(jsx_self_closing_element name: [(identifier) (member_expression)] @call.target) @call
)QUERY";

// JSX in a TSX file: rendering a component is calling it. A lower-case
// element (`<div>`) is the host's, not a component, and the extractor drops
// it. TSX only -- the TypeScript grammar has no JSX nodes.
constexpr std::string_view kJsxQuery = R"QUERY(
(jsx_opening_element name: [(identifier) (member_expression)] @call.target) @call
(jsx_self_closing_element name: [(identifier) (member_expression)] @call.target) @call
)QUERY";

// TypeScript and TSX share one query: the TSX grammar is TypeScript's with
// JSX added, and every node type named here exists in both.
constexpr std::string_view kTypeScriptQuery = R"QUERY(
(internal_module name: (_) @name body: (_) @body) @definition.module
(module name: (identifier) @name body: (_) @body) @definition.module

(class_declaration name: (_) @name body: (_) @body) @definition.class
(abstract_class_declaration name: (_) @name body: (_) @body) @definition.class
(interface_declaration name: (_) @name body: (_) @body) @definition.class
(enum_declaration name: (_) @name body: (_) @body) @definition.class
(type_alias_declaration name: (_) @name) @definition.class

(function_declaration name: (identifier) @name return_type: (_)? @return.type body: (_) @body) @definition.function
(generator_function_declaration name: (identifier) @name return_type: (_)? @return.type body: (_) @body) @definition.function
(method_definition name: (_) @name return_type: (_)? @return.type body: (_) @body) @definition.function
(variable_declarator name: (identifier) @name value: [(arrow_function body: (_) @body) (function_expression body: (_) @body)]) @definition.function
(function_signature name: (identifier) @name) @declaration.function
(method_signature name: (_) @name) @declaration.function
(abstract_method_signature name: (_) @name) @declaration.function

(class_declaration name: (_) @inherit.child (class_heritage (extends_clause value: (_) @inherit.base)))
(class_declaration name: (_) @inherit.child (class_heritage (implements_clause [(type_identifier) (nested_type_identifier) (generic_type)] @inherit.base)))
(abstract_class_declaration name: (_) @inherit.child (class_heritage (extends_clause value: (_) @inherit.base)))
(abstract_class_declaration name: (_) @inherit.child (class_heritage (implements_clause [(type_identifier) (nested_type_identifier) (generic_type)] @inherit.base)))
(interface_declaration name: (_) @inherit.child (extends_type_clause [(type_identifier) (nested_type_identifier) (generic_type)] @inherit.base))

(call_expression function: (_) @call.target) @call
(new_expression constructor: (_) @call.target) @call

(import_statement source: (string (string_fragment) @import.path)) @import
(import_statement (import_clause (identifier) @import.alias) source: (string (string_fragment) @import.path)) @import
(import_statement (import_clause (namespace_import (identifier) @import.alias)) source: (string (string_fragment) @import.path)) @import
(import_statement (import_clause (named_imports (import_specifier name: (_) @import.name !alias))) source: (string (string_fragment) @import.path)) @import
(import_statement (import_clause (named_imports (import_specifier name: (_) @import.name alias: (_) @import.alias))) source: (string (string_fragment) @import.path)) @import
(variable_declarator name: (identifier) @import.alias value: (call_expression function: (identifier) @_require arguments: (arguments . (string (string_fragment) @import.path))) (#eq? @_require "require")) @import

(required_parameter pattern: (identifier) @param.name type: (type_annotation (_) @param.type))
(optional_parameter pattern: (identifier) @param.name type: (type_annotation (_) @param.type))
(variable_declarator name: (identifier) @local.name type: (type_annotation (_) @local.type))
(public_field_definition name: (_) @field.name type: (type_annotation (_) @field.type))
(property_signature name: (_) @field.name type: (type_annotation (_) @field.type))
)QUERY";

constexpr std::string_view kGoQuery = R"QUERY(
(package_clause (package_identifier) @name) @package

(type_declaration (type_spec name: (type_identifier) @name type: [(struct_type) (interface_type)] @body)) @definition.class
(type_declaration (type_spec name: (type_identifier) @name type: [(type_identifier) (qualified_type) (pointer_type) (slice_type) (map_type) (function_type) (generic_type) (array_type) (channel_type)])) @definition.class
(type_declaration (type_alias name: (type_identifier) @name)) @definition.class

(function_declaration name: (identifier) @name result: (_)? @return.type body: (_) @body) @definition.function
(method_declaration receiver: (parameter_list (parameter_declaration type: (_) @receiver)) name: (field_identifier) @name result: (_)? @return.type body: (_) @body) @definition.function
(method_elem name: (field_identifier) @name) @declaration.function

(call_expression function: (_) @call.target) @call

(import_spec path: (_) @import.path) @import
(import_spec name: (package_identifier) @import.alias path: (_) @import.path) @import

(parameter_declaration name: (identifier) @param.name type: (_) @param.type)
(var_spec name: (identifier) @local.name type: (_) @local.type)
(field_declaration name: (field_identifier) @field.name type: (_) @field.type)
)QUERY";

constexpr std::string_view kRustQuery = R"QUERY(
(mod_item name: (identifier) @name body: (_) @body) @definition.module

(struct_item name: (type_identifier) @name) @definition.class
(enum_item name: (type_identifier) @name) @definition.class
(union_item name: (type_identifier) @name) @definition.class
(trait_item name: (type_identifier) @name body: (_) @body) @definition.class
(type_item name: (type_identifier) @name) @definition.class
(impl_item type: (_) @name body: (_) @body) @scope.impl

(function_item name: (identifier) @name return_type: (_)? @return.type body: (_) @body) @definition.function
(function_signature_item name: (identifier) @name) @declaration.function

(trait_item name: (type_identifier) @inherit.child bounds: (trait_bounds [(type_identifier) (scoped_type_identifier) (generic_type)] @inherit.base))
(impl_item trait: (_) @inherit.base type: (_) @inherit.child)

(call_expression function: (_) @call.target) @call
(macro_invocation macro: (_) @call.target) @call

(use_declaration argument: (_) @import.path) @import

(parameter pattern: (identifier) @param.name type: (_) @param.type)
(let_declaration pattern: (identifier) @local.name type: (_) @local.type)
(field_declaration name: (field_identifier) @field.name type: (_) @field.type)
)QUERY";

constexpr std::string_view kJavaQuery = R"QUERY(
(package_declaration [(identifier) (scoped_identifier)] @name) @package

(class_declaration name: (identifier) @name body: (_) @body) @definition.class
(interface_declaration name: (identifier) @name body: (_) @body) @definition.class
(enum_declaration name: (identifier) @name body: (_) @body) @definition.class
(record_declaration name: (identifier) @name body: (_) @body) @definition.class
(annotation_type_declaration name: (identifier) @name body: (_) @body) @definition.class

(method_declaration type: (_) @return.type name: (identifier) @name body: (_) @body) @definition.function
(method_declaration type: (_) @return.type name: (identifier) @name !body) @declaration.function
(constructor_declaration name: (identifier) @name body: (_) @body) @definition.function

(class_declaration name: (identifier) @inherit.child superclass: (superclass (_) @inherit.base))
(class_declaration name: (identifier) @inherit.child interfaces: (super_interfaces (type_list (_) @inherit.base)))
(interface_declaration name: (identifier) @inherit.child (extends_interfaces (type_list (_) @inherit.base)))
(enum_declaration name: (identifier) @inherit.child interfaces: (super_interfaces (type_list (_) @inherit.base)))
(record_declaration name: (identifier) @inherit.child interfaces: (super_interfaces (type_list (_) @inherit.base)))

(method_invocation object: (_) @call.receiver name: (identifier) @call.name) @call
(method_invocation !object name: (identifier) @call.name) @call
(object_creation_expression type: (_) @call.target) @call

(import_declaration [(identifier) (scoped_identifier)] @import.path) @import.symbol
(import_declaration [(identifier) (scoped_identifier)] @import.path (asterisk)) @import.namespace

(formal_parameter type: (_) @param.type name: (identifier) @param.name)
(local_variable_declaration type: (_) @local.type declarator: (variable_declarator name: (identifier) @local.name))
(field_declaration type: (_) @field.type declarator: (variable_declarator name: (identifier) @field.name))
)QUERY";

constexpr std::string_view kCSharpQuery = R"QUERY(
(namespace_declaration name: (_) @name body: (_) @body) @definition.module
(file_scoped_namespace_declaration name: (_) @name) @package

(class_declaration name: (identifier) @name body: (_) @body) @definition.class
(interface_declaration name: (identifier) @name body: (_) @body) @definition.class
(struct_declaration name: (identifier) @name body: (_) @body) @definition.class
(enum_declaration name: (identifier) @name body: (_) @body) @definition.class
(record_declaration name: (identifier) @name) @definition.class

(method_declaration returns: (_) @return.type name: (identifier) @name body: (_) @body) @definition.function
(method_declaration returns: (_) @return.type name: (identifier) @name !body) @declaration.function
(constructor_declaration name: (identifier) @name body: (_) @body) @definition.function

(class_declaration name: (identifier) @inherit.child (base_list (_) @inherit.base))
(interface_declaration name: (identifier) @inherit.child (base_list (_) @inherit.base))
(struct_declaration name: (identifier) @inherit.child (base_list (_) @inherit.base))
(record_declaration name: (identifier) @inherit.child (base_list (_) @inherit.base))

(invocation_expression function: (_) @call.target) @call
(object_creation_expression type: (_) @call.target) @call

(using_directive !name [(identifier) (qualified_name)] @import.path) @import.namespace
(using_directive name: (identifier) @import.alias [(identifier) (qualified_name)] @import.path) @import.symbol

(parameter type: (_) @param.type name: (identifier) @param.name)
(local_declaration_statement (variable_declaration type: (_) @local.type (variable_declarator name: (identifier) @local.name)))
(field_declaration (variable_declaration type: (_) @field.type (variable_declarator name: (identifier) @field.name)))
(property_declaration type: (_) @field.type name: (identifier) @field.name)
)QUERY";

constexpr std::string_view kRubyQuery = R"QUERY(
(module name: (_) @name) @definition.module
(class name: (_) @name) @definition.class
(method name: (_) @name) @definition.function
(singleton_method name: (_) @name) @definition.function

(class name: (_) @inherit.child superclass: (superclass (_) @inherit.base))

(call receiver: (_) @call.receiver method: (_) @call.name) @call
(call !receiver method: (_) @call.name) @call

(call method: (identifier) @_m arguments: (argument_list . (string (string_content) @import.path)) (#eq? @_m "require")) @import
(call method: (identifier) @_m arguments: (argument_list . (string (string_content) @import.path)) (#eq? @_m "require_relative")) @import.relative
)QUERY";

constexpr std::string_view kBashQuery = R"QUERY(
(function_definition name: (word) @name body: (_) @body) @definition.function

(command name: (command_name) @call.target) @call

(command name: (command_name (word) @_cmd) argument: [(word) (string) (raw_string) (concatenation)] @import.path (#any-of? @_cmd "source" ".")) @import.relative
)QUERY";

// ---- Extensions, builtins, wrappers ----------------------------------------

constexpr std::array<std::string_view, 1> kCExtensions{".c"};
constexpr std::array<std::string_view, 10> kCppExtensions{".cpp", ".cc",  ".cxx", ".c++", ".hpp",
                                                          ".hh",  ".hxx", ".h++", ".ipp", ".inl"};
constexpr std::array<std::string_view, 2> kPythonExtensions{".py", ".pyi"};
constexpr std::array<std::string_view, 4> kJavaScriptExtensions{".js", ".mjs", ".cjs", ".jsx"};
constexpr std::array<std::string_view, 3> kTypeScriptExtensions{".ts", ".mts", ".cts"};
constexpr std::array<std::string_view, 1> kTsxExtensions{".tsx"};
constexpr std::array<std::string_view, 1> kGoExtensions{".go"};
constexpr std::array<std::string_view, 1> kRustExtensions{".rs"};
constexpr std::array<std::string_view, 1> kJavaExtensions{".java"};
constexpr std::array<std::string_view, 1> kCSharpExtensions{".cs"};
constexpr std::array<std::string_view, 2> kRubyExtensions{".rb", ".rake"};
constexpr std::array<std::string_view, 2> kBashExtensions{".sh", ".bash"};

constexpr std::array<std::string_view, 30> kCBuiltins{
    "void",     "bool",     "char",   "short",  "int",       "long",    "float",   "double",
    "signed",   "unsigned", "auto",   "const",  "volatile",  "struct",  "class",   "enum",
    "union",    "typename", "static", "inline", "constexpr", "mutable", "wchar_t", "char8_t",
    "char16_t", "char32_t", "_Bool",  "extern", "register",  "restrict"};
constexpr std::array<std::string_view, 6> kCppWrappers{
    "unique_ptr", "shared_ptr", "weak_ptr", "optional", "reference_wrapper", "atomic"};
constexpr std::array<std::string_view, 13> kPythonBuiltins{
    "int",  "float", "str", "bytes", "bool", "None", "object",
    "list", "dict",  "set", "tuple", "type", "Any"};
constexpr std::array<std::string_view, 1> kPythonWrappers{"Optional"};
constexpr std::array<std::string_view, 14> kTypeScriptBuiltins{
    "number", "string", "boolean",   "void",   "any",    "unknown", "never",
    "object", "null",   "undefined", "symbol", "bigint", "this",    "readonly"};
constexpr std::array<std::string_view, 1> kTypeScriptWrappers{"Promise"};
constexpr std::array<std::string_view, 27> kGoBuiltins{
    "bool",      "string",    "int",        "int8",   "int16",  "int32",   "int64",
    "uint",      "uint8",     "uint16",     "uint32", "uint64", "uintptr", "float32",
    "float64",   "complex64", "complex128", "byte",   "rune",   "error",   "any",
    "interface", "struct",    "func",       "map",    "chan",   "const"};
constexpr std::array<std::string_view, 24> kRustBuiltins{
    "bool", "char", "str",   "i8",  "i16", "i32",  "i64",  "i128", "isize", "u8",   "u16", "u32",
    "u64",  "u128", "usize", "f32", "f64", "Self", "self", "mut",  "dyn",   "impl", "ref", "const"};
constexpr std::array<std::string_view, 6> kRustWrappers{"Box",    "Rc",      "Arc",
                                                        "Option", "RefCell", "Cell"};
// java.lang's own types are in every file's scope without an import, as
// much the language's as `int` is: never a reference worth a node.
constexpr std::array<std::string_view, 22> kJavaBuiltins{
    "void",   "boolean",   "byte",    "char",   "short",  "int",     "long", "float",
    "double", "var",       "final",   "String", "Object", "Integer", "Long", "Short",
    "Byte",   "Character", "Boolean", "Float",  "Double", "Void"};
constexpr std::array<std::string_view, 21> kCSharpBuiltins{
    "void",   "bool",   "byte", "sbyte",   "char",  "decimal", "double",
    "float",  "int",    "uint", "long",    "ulong", "short",   "ushort",
    "object", "string", "var",  "dynamic", "nint",  "nuint",   "readonly"};
constexpr std::span<const std::string_view> kNone{};

constexpr std::array<CodeLanguage, 12> kLanguages{{
    {CodeLanguageId::C,
     "c",
     "C",
     "tree-sitter-c v0.24.2",
     "::",
     Qualification::Global,
     "namespace",
     kCExtensions,
     kCBuiltins,
     kNone,
     kCQuery,
     {}},
    {CodeLanguageId::Cpp,
     "cpp",
     "C++",
     "tree-sitter-cpp v0.23.4",
     "::",
     Qualification::Global,
     "namespace",
     kCppExtensions,
     kCBuiltins,
     kCppWrappers,
     kCppQuery,
     {}},
    {CodeLanguageId::Python,
     "python",
     "Python",
     "tree-sitter-python v0.25.0",
     ".",
     Qualification::ModulePath,
     "module",
     kPythonExtensions,
     kPythonBuiltins,
     kPythonWrappers,
     kPythonQuery,
     {}},
    {CodeLanguageId::JavaScript,
     "javascript",
     "JavaScript",
     "tree-sitter-javascript v0.25.0",
     ".",
     Qualification::FileScoped,
     "module",
     kJavaScriptExtensions,
     kTypeScriptBuiltins,
     kNone,
     kJavaScriptQuery,
     {}},
    {CodeLanguageId::TypeScript,
     "typescript",
     "TypeScript",
     "tree-sitter-typescript v0.23.2",
     ".",
     Qualification::FileScoped,
     "namespace",
     kTypeScriptExtensions,
     kTypeScriptBuiltins,
     kTypeScriptWrappers,
     kTypeScriptQuery,
     {}},
    {CodeLanguageId::Tsx, "tsx", "TSX", "tree-sitter-typescript (tsx) v0.23.2", ".",
     Qualification::FileScoped, "namespace", kTsxExtensions, kTypeScriptBuiltins,
     kTypeScriptWrappers, kTypeScriptQuery, kJsxQuery},
    {CodeLanguageId::Go,
     "go",
     "Go",
     "tree-sitter-go v0.25.0",
     ".",
     Qualification::PackageDirectory,
     "package",
     kGoExtensions,
     kGoBuiltins,
     kNone,
     kGoQuery,
     {}},
    {CodeLanguageId::Rust,
     "rust",
     "Rust",
     "tree-sitter-rust v0.24.2",
     "::",
     Qualification::CrateModule,
     "module",
     kRustExtensions,
     kRustBuiltins,
     kRustWrappers,
     kRustQuery,
     {}},
    {CodeLanguageId::Java,
     "java",
     "Java",
     "tree-sitter-java v0.23.5",
     ".",
     Qualification::Package,
     "package",
     kJavaExtensions,
     kJavaBuiltins,
     kNone,
     kJavaQuery,
     {}},
    {CodeLanguageId::CSharp,
     "csharp",
     "C#",
     "tree-sitter-c-sharp v0.23.5",
     ".",
     Qualification::Package,
     "namespace",
     kCSharpExtensions,
     kCSharpBuiltins,
     kNone,
     kCSharpQuery,
     {}},
    {CodeLanguageId::Ruby,
     "ruby",
     "Ruby",
     "tree-sitter-ruby v0.23.1",
     "::",
     Qualification::Global,
     "module",
     kRubyExtensions,
     kNone,
     kNone,
     kRubyQuery,
     {}},
    {CodeLanguageId::Bash,
     "bash",
     "Bash",
     "tree-sitter-bash v0.25.1",
     "::",
     Qualification::FileScoped,
     "script",
     kBashExtensions,
     kNone,
     kNone,
     kBashQuery,
     {}},
}};

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] std::string extension_of(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return {};
    }
    return lower(name.substr(dot));
}

}  // namespace

std::span<const CodeLanguage> code_languages() noexcept {
    return kLanguages;
}

const CodeLanguage& code_language(CodeLanguageId id) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): every id is in range.
    return kLanguages[static_cast<std::size_t>(id)];
}

const CodeLanguage* code_language_by_name(std::string_view name) {
    const std::string folded = lower(name);

    struct Alias {
        std::string_view alias;
        CodeLanguageId id;
    };

    static constexpr std::array<Alias, 11> kAliases{{{"c++", CodeLanguageId::Cpp},
                                                     {"cxx", CodeLanguageId::Cpp},
                                                     {"py", CodeLanguageId::Python},
                                                     {"js", CodeLanguageId::JavaScript},
                                                     {"ts", CodeLanguageId::TypeScript},
                                                     {"golang", CodeLanguageId::Go},
                                                     {"rs", CodeLanguageId::Rust},
                                                     {"cs", CodeLanguageId::CSharp},
                                                     {"c#", CodeLanguageId::CSharp},
                                                     {"rb", CodeLanguageId::Ruby},
                                                     {"sh", CodeLanguageId::Bash}}};
    for (const CodeLanguage& language : kLanguages) {
        if (language.name == folded) {
            return &language;
        }
    }
    for (const Alias& alias : kAliases) {
        if (alias.alias == folded) {
            return &code_language(alias.id);
        }
    }
    return nullptr;
}

const CodeLanguage* code_language_for_path(std::string_view path, bool headers_are_cpp) {
    const std::string extension = extension_of(path);
    if (extension.empty()) {
        return nullptr;
    }
    if (extension == ".h") {
        return &code_language(headers_are_cpp ? CodeLanguageId::Cpp : CodeLanguageId::C);
    }
    for (const CodeLanguage& language : kLanguages) {
        if (std::ranges::find(language.extensions, extension) != language.extensions.end()) {
            return &language;
        }
    }
    return nullptr;
}

bool tree_has_cpp(const std::vector<std::string>& paths) {
    return std::ranges::any_of(paths, [](const std::string& path) {
        const std::string extension = extension_of(path);
        return std::ranges::find(kCppExtensions, extension) != kCppExtensions.end();
    });
}

std::string code_extractor_id(const CodeLanguage& language) {
    return std::string{language.name} + ":" + std::string{language.grammar} + ":extractor-" +
           std::to_string(kCodeExtractorVersion);
}

std::string code_language_names() {
    std::string out;
    for (const CodeLanguage& language : kLanguages) {
        out += (out.empty() ? "" : ", ") + std::string{language.name};
    }
    return out;
}

}  // namespace apogee::graph

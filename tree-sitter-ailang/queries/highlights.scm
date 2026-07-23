; Syntax-highlighting queries for AiLang (tree-sitter).

[
  "fn" "lp" "mt" "rt" "el" "if" "en" "st" "mu" "im" "ex" "cl" "vt" "tr"
  "cinc" "csrc" "br" "ct" "in" "as" "super"
] @keyword

(primitive_type) @type.builtin
(boolean) @constant.builtin
(comment) @comment
(string) @string
(escape) @string.escape
(bytes_literal) @string.special
(interpolation) @embedded
(integer) @number
(float) @number

(function_definition name: (identifier) @function)
(method name: (identifier) @function.method)
(trait_method name: (identifier) @function.method)
(call_expression function: (identifier) @function.call)
(field_expression field: (identifier) @property)

(struct_definition name: (identifier) @type)
(enum_definition name: (identifier) @type)
(class_definition name: (identifier) @type)
(trait_definition name: (identifier) @type)
(generic_type (identifier) @type)
(variant name: (identifier) @constructor)

(parameter name: (identifier) @variable.parameter)

[
  "+" "-" "*" "/" "%" "++" "==" "!=" "<" "<=" ">" ">=" "&&" "||" "!"
  "|>" "??" "->" "=>" ":=" "=" "+=" "-=" "*=" "/=" "%=" "?"
] @operator

(identifier) @variable

// tree-sitter-ailang — grammar for the AiLang language (.ail).
//
// STATUS: authored from skill/SKILL.md's keyword/operator spec as a starting
// scaffold. It has NOT yet been run through `tree-sitter generate` in this repo,
// so validate + tune conflicts with:
//     npm install && npx tree-sitter generate && npx tree-sitter test
// Highlighting queries live in queries/highlights.scm.
//
// Grammar scope: AiLang is deliberately small — ~2-char keywords, a fixed
// operator set, `"${expr}"` interpolation, `//` line comments. This covers the
// surface needed for editor/docs/playground highlighting.

module.exports = grammar({
  name: "ailang",

  extras: ($) => [/\s/, $.comment],

  word: ($) => $.identifier,

  rules: {
    source_file: ($) => repeat($._item),

    _item: ($) =>
      choice(
        $.function_definition,
        $.struct_definition,
        $.enum_definition,
        $.class_definition,
        $.trait_definition,
        $.import,
        $.extern,
        $.cinc,
        $._statement,
      ),

    comment: ($) => token(seq("//", /.*/)),

    // ── declarations ────────────────────────────────────────────────────────
    import: ($) =>
      seq("im", $.string, optional(seq("as", field("alias", $.identifier)))),

    cinc: ($) => seq("cinc", $.string),

    extern: ($) =>
      seq(
        "ex",
        optional($.string), // `ex "lib"` link hint
        "fn",
        field("name", $.identifier),
        $.parameter_list,
        optional($._return_type),
      ),

    function_definition: ($) =>
      seq(
        "fn",
        field("name", $.identifier),
        optional($.type_parameters),
        $.parameter_list,
        optional($._return_type),
        $._fn_body,
      ),

    _fn_body: ($) => choice($.block, $._expression),

    type_parameters: ($) =>
      seq("<", commaSep1(seq($.identifier, optional(seq(":", $.identifier)))), ">"),

    parameter_list: ($) => seq("(", optional(commaSep($.parameter)), optional(seq(",", "...")), ")"),

    parameter: ($) => seq(field("name", $.identifier), optional(seq(":", $._type))),

    _return_type: ($) => seq("->", $._type),

    struct_definition: ($) =>
      seq("st", field("name", $.identifier), optional($.type_parameters), $.field_block),

    field_block: ($) => seq("{", repeat($.field), "}"),

    field: ($) => seq(field("name", $.identifier), ":", $._type, optional(choice(";", ","))),

    enum_definition: ($) =>
      seq("en", field("name", $.identifier), optional($.type_parameters), $.variant_block),

    variant_block: ($) => seq("{", commaSep($.variant), optional(","), "}"),

    variant: ($) =>
      seq(field("name", $.identifier), optional($.parameter_list)),

    class_definition: ($) =>
      seq(
        "cl",
        field("name", $.identifier),
        optional(seq(":", field("base", $.identifier))),
        $.class_block,
      ),

    class_block: ($) => seq("{", repeat(choice($.field, $.method)), "}"),

    method: ($) =>
      seq(optional("vt"), "fn", field("name", $.identifier), $.parameter_list, optional($._return_type), $.block),

    trait_definition: ($) =>
      seq("tr", field("name", $.identifier), "{", repeat($.trait_method), "}"),

    trait_method: ($) =>
      seq("fn", field("name", $.identifier), $.parameter_list, optional($._return_type), optional(";")),

    // ── types ───────────────────────────────────────────────────────────────
    _type: ($) =>
      choice(
        $.primitive_type,
        $.array_type,
        $.map_type,
        $.pointer_type,
        $.result_type,
        $.tuple_type,
        $.fn_type,
        $.generic_type,
        $.identifier,
      ),

    primitive_type: ($) =>
      choice("i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64", "bool", "str", "bytes", "void"),

    array_type: ($) => seq("[", $._type, "]"),
    map_type: ($) => seq("{", $._type, ":", $._type, "}"),
    pointer_type: ($) => seq("*", $._type),
    result_type: ($) => seq("!", $._type),
    tuple_type: ($) => seq("(", commaSep1($._type), ")"),
    fn_type: ($) => seq("fn", "(", optional(commaSep($._type)), ")", "->", $._type),
    generic_type: ($) => seq($.identifier, "<", commaSep1($._type), ">"),

    // ── statements ──────────────────────────────────────────────────────────
    block: ($) => seq("{", repeat($._statement), "}"),

    _statement: ($) =>
      choice(
        $.let_declaration,
        $.assignment,
        $.loop_statement,
        $.if_statement,
        $.match_statement,
        $.return_statement,
        $.break_statement,
        $.continue_statement,
        $._expression,
      ),

    let_declaration: ($) =>
      seq(
        optional("mu"),
        field("name", $.identifier),
        optional(seq(":", $._type)),
        ":=",
        field("value", $._expression),
      ),

    assignment: ($) =>
      seq(
        field("target", $._expression),
        choice("=", "+=", "-=", "*=", "/=", "%="),
        field("value", $._expression),
      ),

    loop_statement: ($) =>
      seq(
        "lp",
        optional(
          choice(
            seq(field("var", $._loop_binding), "in", field("iter", $._expression)),
            field("cond", $._expression),
          ),
        ),
        $.block,
      ),

    _loop_binding: ($) => choice($.identifier, seq("(", commaSep1($.identifier), ")")),

    if_statement: ($) =>
      prec.right(
        seq(
          "if",
          field("cond", $._expression),
          $._if_body,
          optional(seq("el", choice($.if_statement, $._if_body))),
        ),
      ),

    _if_body: ($) => choice($.block, $._statement),

    match_statement: ($) => seq("mt", field("subject", $._expression), "{", repeat($.match_arm), "}"),

    match_arm: ($) =>
      seq(field("pattern", $._expression), optional(seq("if", $._expression)), "=>", $._statement, optional(";")),

    return_statement: ($) => prec.right(seq("rt", optional($._expression))),
    break_statement: ($) => "br",
    continue_statement: ($) => "ct",

    // ── expressions ─────────────────────────────────────────────────────────
    _expression: ($) =>
      choice(
        $.binary_expression,
        $.unary_expression,
        $.call_expression,
        $.field_expression,
        $.index_expression,
        $.lambda,
        $.parenthesized_expression,
        $.array_literal,
        $.map_literal,
        $.try_expression,
        $.address_of,
        $.integer,
        $.float,
        $.string,
        $.bytes_literal,
        $.boolean,
        $.identifier,
      ),

    binary_expression: ($) => {
      const table = [
        ["||", 1], ["&&", 2],
        ["==", 3], ["!=", 3], ["<", 3], ["<=", 3], [">", 3], [">=", 3],
        ["|", 4], ["^", 4], ["&", 4],
        ["<<", 5], [">>", 5],
        ["+", 6], ["-", 6], ["++", 6],
        ["*", 7], ["/", 7], ["%", 7],
        ["|>", 8], ["??", 8],
      ];
      return choice(
        ...table.map(([op, p]) =>
          prec.left(p, seq(field("left", $._expression), field("op", op), field("right", $._expression))),
        ),
      );
    },

    unary_expression: ($) => prec(9, seq(choice("-", "!"), $._expression)),

    address_of: ($) => prec(9, seq("&", $._expression)),

    try_expression: ($) => prec(10, seq($._expression, "?")),

    call_expression: ($) =>
      prec(11, seq(field("function", $._expression), $.argument_list)),

    argument_list: ($) => seq("(", optional(commaSep($._expression)), ")"),

    field_expression: ($) =>
      prec(12, seq(field("object", $._expression), ".", field("field", $.identifier))),

    index_expression: ($) =>
      prec(12, seq(field("object", $._expression), "[", field("index", $._expression), "]")),

    lambda: ($) => seq("fn", $.parameter_list, optional($._return_type), $._fn_body),

    parenthesized_expression: ($) => seq("(", $._expression, ")"),

    array_literal: ($) => seq("[", optional(commaSep($._expression)), "]"),

    map_literal: ($) => seq("{", optional(commaSep(seq($._expression, ":", $._expression))), "}"),

    // ── literals ────────────────────────────────────────────────────────────
    integer: ($) => /-?\d+/,
    float: ($) => /-?\d+\.\d+/,
    boolean: ($) => choice("true", "false"),

    string: ($) => seq('"', repeat(choice($.interpolation, $.escape, $._string_text)), '"'),
    _string_text: ($) => token.immediate(prec(1, /[^"\\$]+/)),
    escape: ($) => token.immediate(/\\./),
    interpolation: ($) => seq("${", $._expression, "}"),

    bytes_literal: ($) => seq('b"', repeat(choice($.escape, /[^"\\]+/)), '"'),

    identifier: ($) => /[A-Za-z_][A-Za-z0-9_]*/,
  },
});

function commaSep(rule) {
  return optional(commaSep1(rule));
}
function commaSep1(rule) {
  return seq(rule, repeat(seq(",", rule)));
}

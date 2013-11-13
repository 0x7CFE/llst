%{

#include <stdio.h>

%}

/* image macros */
%token CLASS        "CLASS"
%token RAWCLASS     "RAWCLASS"
%token METHOD       "METHOD"
%token COMMENT      "COMMENT"
%token BEGIN        "BEGIN"
%token END          "END"

/* literals */
%token IDENTIFIER   "identifier"
%token ARGUMENT     "argument identifier"
%token SELECTOR     "selector identifier"
%token INTEGER      "integer literal"
%token STRING       "string literal"
%token SYMBOL       "symbol literal"
%token CHARACTER    "character literal"

/* keywords */
%token TRUE         "true"
%token FALSE        "false"
%token NIL          "nil"
%token SELF         "self"
%token SUPER        "super"

/* syntax terminals */
%token DOT          "."
%token CASCADE      ";"
%token RET          "^"
%token PIPE         "|"
%token BANG         "!"
%token ARROW        "<-"

%token ARRAY        "#("
%token LPAREN       "("
%token RPAREN       ")"
%token LBLOCK       "["
%token RBLOCK       "]"

/* relation operators */
%token LESS         "<"
%token GTER         ">"
%token LEQ          "<="
%token GTREQ        ">="
%token EQ           "="
%token EQEQ         "=="
%token NEQ          "~="
%token NENE         "~~"

/*  arithmetics */
%token MINUS        "-"
%token PLUS         "+"
%token MUL          "*"
%token DIV          "/"

/* special key messages */
%token ISNIL        "isNil"
%token NOTNIL       "notNil"
%token NOT          "not"
%token AND          "and:"
%token OR           "or:"
%token IFTRUE       "ifTrue:"
%token IFFALSE      "ifFalse:"
%token WHILETRUE    "whileTrue:"
%token WHILEFALSE   "whileFalse:"
%token UNARY_WHILETRUE  "whileTrue"
%token UNARY_WHILEFALSE "whileFalse"

/* operator priorites */
%nonassoc "^"
%nonassoc "<-"
%left SPECIAL_MESSAGE
%left KEY_MESSAGE
%left "-" "+" "*" "/" "<" ">" "<=" ">=" "=" "==" "~=" "~~"
%left UNARY_MESSAGE
%left "("

%left UNARY_MINUS
%nonassoc PRIMITIVE

%%

%start image;

image_contents : /* empty image */
    | comment
    | class_definition
    | rawclass_definition
    | method_definition
    ;

image : image_contents initial_method;

id : IDENTIFIER;

arg_id : ARGUMENT;

id_list_tail : /* empty tail */
    | id id_list_tail;

id_list : id id_list_tail;

comment : COMMENT;

class_definition :
      CLASS id id id_list "\n"
    | error "\n"
    ;

rawclass_definition :
      RAWCLASS id id id id_list "\n"
    /* | error "\n" */
    ;

method_definition : METHOD id "\n" method;

initial_method : BEGIN method_body END;

arg_list_tail : /* empty */
    | arg_id arg_list_tail;

arg_list : arg_id arg_list_tail;

arguments :
      arg_list "|"
    | error    "|" /* error recovery */
    ;

block_body :
      statements /* may be empty */
    | arguments statements
    ;

block :
      "[" block_body "]"
    | "[" error      "]" /* error recovery */
    ;

selector : SELECTOR;

selector_value_pair: selector expression;

key_message_tail : /* empty */
    | selector_value_pair key_message_tail;

key_message : selector_value_pair key_message_tail;

message :
      /* simple unary message identifier */
      id %prec UNARY_MESSAGE

      /* a set of selector-value pairs */
    | key_message %prec KEY_MESSAGE
    ;

message_chain_tail : /* empty */
    | message message_chain_tail
    | ";" message message_chain_tail /* cascade messages */
    ;

message_chain : message message_chain_tail;

literals : /* empty */
    | literal;

string : STRING;
symbol : SYMBOL;
number : INTEGER;
char   : CHARACTER;
array  :
      "#(" literals ")"
    | "#(" error    ")"  /* error recovery */
    ;

literal:
      string  /* string literal */
    | symbol  /* symbol literal */
    | number  /* number literal */
    | char    /* character literal $x */
    | array   /* inline literal array #( ) */

    | "-" number %prec UNARY_MINUS
    ;

subexpression:
      "(" expression ")"
    | "(" error      ")" /* error recovery */
    ;

receiver :
      id            /* global, temporary or instance variable identifier */
    | literal       /* inline literal object */
    | TRUE
    | FALSE
    | NIL
    | SELF
    | SUPER
    | block         /* inline block */
    | subexpression /* expression in parentheses */
    ;

assignment : id "<-" expression;

expression :
      receiver
    | receiver message_chain

    | assignment

    | expression "+"  expression
    | expression "-"  expression
    | expression "*"  expression
    | expression "/"  expression
    | expression "="  expression
    | expression "~=" expression
    | expression "==" expression
    | expression "~~" expression
    | expression "<"  expression
    | expression ">"  expression
    | expression "<=" expression
    | expression ">=" expression

    | expression "isNil"                            %prec UNARY_MESSAGE
    | expression "notNil"                           %prec UNARY_MESSAGE
    | expression "not"                              %prec UNARY_MESSAGE

    | expression "and:"     block                   %prec SPECIAL_MESSAGE
    | expression "or:"      block                   %prec SPECIAL_MESSAGE

    | expression "ifTrue:"  block                   %prec SPECIAL_MESSAGE
    | expression "ifFalse:" block                   %prec SPECIAL_MESSAGE
    | expression "ifTrue:"  block "ifFalse:" block  %prec SPECIAL_MESSAGE
    | expression "ifFalse:" block "ifTrue:"  block  %prec SPECIAL_MESSAGE

    /* TODO #to:do: #to:by:do: */
    | block "whileTrue:"    block                   %prec SPECIAL_MESSAGE
    | block "whileFalse:"   block                   %prec SPECIAL_MESSAGE

    | block "whileTrue"                             %prec UNARY_MESSAGE
    | block "whileFalse"                            %prec UNARY_MESSAGE
    ;

return:
      "^" expression
    | "^" primitive
    ;

primitive_params : /* empty */
    | receiver primitive_params;

primitive :
      "<" number primitive_params ">" %prec PRIMITIVE
    | "<" error ">" /* error recovery */
    ;

statement :
      expression
    | primitive
    | return
    ;

statements : /* empty */
    | statement "." statements
    | statement /* last one in a block */

    | error "." /* error recovery till end of a statement */
    ;

method_body : statements;

temporaries :
      "|" id_list "|"
    | "|" error   "|" /* error recovery */
    ;

method_interface_tail : /* empty */
    | selector id;

method_interface :
      id                                /* simple unary method name */
    | selector id method_interface_tail /* parametrized method */
    ;

method :
      method_interface method_body "!"
    | method_interface temporaries method_body "!"
    | error "!" /* error recovery till end of a method */
    ;


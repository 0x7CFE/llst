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

/* terminals */
%token DOT          "."
%token RET          "^"
%token PIPE         "|"
%token BANG         "!"
%token ARROW        "<-"

%token LPAREN       "("
%token RPAREN       ")"
%token LBLOCK       "["
%token RBLOCK       "]"

%token LESS         "<"
%token GTER         ">"
%token LEQ          "<="
%token GTREQ        ">="
%token EQ           "="
%token EQEQ         "=="
%token NEQ          "~="
%token NENE         "~~"

/* TODO Unary minus */
%token MINUS        "-" 
%token PLUS         "+"
%token MUL          "*"
%token DIV          "/"

/* special key messages */
%token ISNIL        "isNil"
%token NOTNIL       "notNil"
%token IFTRUE       "ifTrue:"
%token IFFALSE      "ifFalse:"
%token WHILETRUE    "whileTrue:"
%token WHILEFALSE   "whileFalse:"

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
    
class_definition : CLASS id id id_list "\n";

rawclass_definition : RAWCLASS id id id id_list "\n";

method_definition : METHOD id method;

initial_method : BEGIN method_body END;

temporaries : "|" id_list "|";

arg_list_tail : /* empty */
    | arg_id arg_list_tail;

arg_list : arg_id arg_list_tail;

arguments : arg_list;

block_body : /* empty */
    | statements
    | arguments "|" statements
    ;

block : "[" block_body "]";

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
array  : "#(" literals ")";

literal:
      string  /* string literal */
    | symbol  /* symbol literal */
    | number  /* number literal */
    | char    /* character literal $x */
    | array   /* inline literal array #( ) */
    ;
    
receiver : 
      id      /* global, temporary or instance variable identifier */
    | literal /* inline literal object */ 
    | TRUE
    | FALSE
    | NIL
    | SELF
    | SUPER
    | block   /* inline block */
    | "(" expression ")"
    ;
    
expression : 
      receiver
    | receiver message_chain
    
    | "-" expression                                %prec UNARY_MINUS
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
    
    | expression "ifTrue:"  block                   %prec SPECIAL_MESSAGE
    | expression "ifFalse:" block                   %prec SPECIAL_MESSAGE
    | expression "ifTrue:"  block "ifFalse:" block  %prec SPECIAL_MESSAGE
    | expression "ifFalse:" block "ifTrue:"  block  %prec SPECIAL_MESSAGE
    
    /* TODO #to:do: #to:by:do: */
    | block "whileTrue:"    block                   %prec SPECIAL_MESSAGE
    | block "whileFalse:"   block                   %prec SPECIAL_MESSAGE
    ;

assignment : id "<-" expression;

primitive_params : /* empty */
    | receiver primitive_params;

primitive : "<" number primitive_params ">" %prec PRIMITIVE;

statement : 
      expression
    | assignment
    | primitive
    | "^" expression
    ;

statements : /* empty */    
    | statement "." statements
    | statement /* last one in a block */
    ;

method_interface_tail : /* empty */
    | selector id;
    
method_interface : 
      id /* simple unary method name */
    | selector id method_interface_tail /* parametrized method */
    ;
    
method_body : statements;

method : 
      method_interface method_body "!"
    | method_interface temporaries method_body "!"
    ;


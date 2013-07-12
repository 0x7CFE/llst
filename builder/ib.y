%{

#include <stdio.h>

%}

%token CLASS        "CLASS"
%token RAWCLASS     "RAWCLASS"
%token COMMENT      "COMMENT"

%token RET          "^"
%token PIPE         "|"
%token BANG         "!"

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
      id /* simple unary message identifier */
    | key_message
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
    ;

assignment : id "<-" expression;

primitive_params : /* empty */
    | receiver primitive_params;

primitive : "<" number primitive_params ">";

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


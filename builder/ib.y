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



image : /* empty image */
    | comment
    | class_definition
    | rawclass_definition
    | method_definition;

id : IDENTIFIER;

arg_id : ARGUMENT;
    
id_list_tail : /* empty tail */
    | id id_list_tail;

id_list : id id_list_tail;
    
class_definition : CLASS id id id_list;

rawclass_definition : RAWCLASS id id id id_list;

method_definition : METHOD id method;

temporaries : "|" id_list "|";


arg_list_tail : /* empty */
    | arg_id arg_list_tail;

arg_list : arg_id arg_list_tail;

arguments : arg_list;

block_body : 
    statements
    | arguments "|" statements
    ;

block : "[" block_body "]";

selector : SELECTOR;    
    
selector_value_pair: selector expression;

key_message_tail : /* empty */
    | selector_value_pair key_message_tail;

key_message : selector_value_pair key_message_tail;

message : 
      id
    | operator expression
    | key_message
    ;

message_chain_tail : /* empty */
    | message message_chain_tail;

message_chain : message message_chain_tail;
    
inline_object : 
    block
    | string
    | symbol
    | number
    | array /* #( ) constants */
    | char  /* $x   constants */
    | TRUE
    | FALSE
    | NIL
    ;

target : 
    inline_object
    | id
    | SELF
    | SUPER
    ;
    
expression : 
    target
    | target message_chain
    | "(" expression ")"
    | expression "+" expression
    | expression "-" expression
    | expression "*" expression
    | expression "/" expression
    | expression "=" expression
    | expression "~=" expression
    | expression "==" expression
    | expression "<" expression
    | expression ">" expression
    | expression "<=" expression
    | expression ">=" expression
    ;

assignment : id "<-" expression;

primitive_params : id_list;

primitive : "<" number primitive_params ">";

statement : 
    block message_chain
    | expression
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
    
method_interface : selector id method_interface_tail;
    
method_body : statements;

method : 
      method_interface method_body "!"
    | method_interface temporaries method_body "!"
    ;


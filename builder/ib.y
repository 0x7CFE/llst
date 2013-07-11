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

col_id : ":" IDENTIFIER;
    
id_list_tail : /* empty tail */
    | id id_list_tail;

id_list : id id_list_tail;
    
class_definition : CLASS id id id_list;

rawclass_definition : RAWCLASS id id id id_list;

temporaries : "|" id_list "|";


arg_list_tail : /* empty */
    | col_id arg_list_tail;

arg_list : col_id arg_list_tail;

arguments : arg_list;

block_body : 
    statements
    | arguments "|" statements
    ;

block : "[" block_body "]";

selector : SELECTOR;    
    
key_message_tail : /* empty */
    | selector expression key_message_tail;

key_message : selector expression key_message_tail;

message : 
      id
    | operator expression
    | key_message
    ;

message_chain : message message_chain;
    
inline_object : 
    block
    | string 
    | number
    | array
    | char
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
    inline_object 
    | id
    | "(" expression ")"
    | expression message_chain
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

primitive : "<" number primitive_params ">";

statement : 
    block message_chain
    | expression
    | assignment
    | "^" expression
    | primitive
    ;

statements : /* empty */    
    | statement "." statements;

method_body : statements;

method_definition : 
      method_interface method_body BANG
    | method_interface temporaries method_body BANG
    ;


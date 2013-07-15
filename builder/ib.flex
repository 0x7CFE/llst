
%{
#include "ib.tab.hpp"
%}

%x COMMENT
%x STRING

NL    \n

%%

        /* TODO special cases where \n should be returned */
[ \t\r]+                            yylloc->step();
{NL}+                               yylloc->lines(yyleng); yylloc->step();


"\""                                BEGIN(COMMENT);
<COMMENT>[^\"\r\n]*                 /* eat anything that's not a " */
<COMMENT>{NL}+                      yylloc->lines(yyleng);
<COMMENT>"\""                       BEGIN(INITIAL);
<COMMENT><<EOF>>                    PushError(yylloc, "unexpected end of file in a comment");

[1-9][0-9]*                         RETURN(INTEGER);

        /* single-quoted string */
\'                                  BEGIN(STRING);
<STRING><<EOF>>                     PushError(yylloc, "unexpected end of file in a string literal");
<STRING>[^\\\'\r\n\"]+              m_buf += yytext;
<STRING>{NL}+                       m_buf += yytext; yylloc->lines(yyleng);
<STRING>"'"                         BEGIN(INITIAL); RETURN_B(STRING);

"."             RETURN( DOT          );     
";"             RETURN( CASCADE      );
"^"             RETURN( RET          );
"|"             RETURN( PIPE         );
"!"             RETURN( BANG         );
"<-"            RETURN( ARROW        );

"#("            RETURN( ARRAY        );
"("             RETURN( LPAREN       );
")"             RETURN( RPAREN       );
"["             RETURN( LBLOCK       );
"]"             RETURN( RBLOCK       );

"<"             RETURN( LESS         );
">"             RETURN( GTER         );
"<="            RETURN( LEQ          );
">="            RETURN( GTREQ        );
"="             RETURN( EQ           );
"=="            RETURN( EQEQ         );
"~="            RETURN( NEQ          );
"~~"            RETURN( NENE         );

"-"             RETURN( MINUS        );
"+"             RETURN( PLUS         );
"*"             RETURN( MUL          );
"/"             RETURN( DIV          );

"isNil"         RETURN( ISNIL        );        
"notNil"        RETURN( NOTNIL       );
"not"           RETURN( NOT          );
"and:"          RETURN( AND          );
"or:"           RETURN( OR           );
"ifTrue:"       RETURN( IFTRUE       );
"ifFalse:"      RETURN( IFFALSE      );
"whileTrue:"    RETURN( WHILETRUE    );
"whileFalse:"   RETURN( WHILEFALSE   );
"whileTrue"     RETURN( UNARY_WHILETRUE  );
"whileFalse"    RETURN( UNARY_WHILEFALSE );

true            RETURN(TRUE);         
false           RETURN(FALSE);        
nil             RETURN(NIL);          
self            RETURN(SELF);        
super           RETURN(SUPER);




\$[^\t\r\n]                         RETURN(CHARACTER)

\#[a-zA-Z_][a-zA-Z0-9_:]*           {
                                        if(strlen(yytext) > 128)
                                            PushError(yylloc, "symbol is too long");
                                        RETURN(SYMBOL);
                                    }
                                    
\:[a-zA-Z_][a-zA-Z0-9_]*            {
                                        if(strlen(yytext) > 128)
                                            PushError(yylloc, "argument identifier is too long");
                                        RETURN(ARGUMENT);
                                    }
                                    
[a-zA-Z_][a-zA-Z0-9_]\:*            {
                                        if(strlen(yytext) > 128)
                                            PushError(yylloc, "selector is too long");
                                        RETURN(SELECTOR);
                                    }
        /* identifier */
[a-zA-Z_][a-zA-Z0-9_]*              {
                                        if(strlen(yytext) > 128)
                                            PushError(yylloc, "identifier is too long");
                                        RETURN(IDENTIFIER);
                                    }

                                    

                                    
                                    
%%

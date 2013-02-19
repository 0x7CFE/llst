#
# TSymbol
#

define tsymbol
    if $argc == 0
        help tsymbol
    else
        #FIXME I can't call .c_str()
        printf "#%s\n", (char*) $arg0->toString()
    end
end

document tsymbol
    Prints TSymbol information.
    Syntax: tsymbol <TSymbol>
    Example:
    tsymbol globals.badMethodSymbol
end

#
# TMethod
#

define tmethod
    if $argc == 0
        help tmethod
    else
        set $class = (TClass*) $arg0->getClass()
        set $className  = $class->name->toString()
        set $methodName = $arg0->name->toString()
        printf "Method name: \"%s\" \n", (char*) $methodName
        printf "Method class: \"%s\", ", (char*) $className
        p $class
    end
end

document tmethod
    Prints TMethod information.
    Syntax: tmethod <TMethod>
    Example:
    tmethod globals.initialMethod
end

#
# TClass
#

define tclass
    if $argc == 0
        help tclass
    else
        set $className   = (char*) $arg0->name->toString()
        set $parentName  = (char*) $arg0->parentClass->name->toString()
        set $methodSize  = $arg0->methods->keys->getSize()
        
        printf "Class name: \"%s\"\n", $className
        printf "Instance size: %d\n", $arg0->instanceSize
        printf "Parent class: \"%s\", ", $parentName
        p $arg0->parentClass
        printf "Got %d methods: \n", $methodSize
        
        set $i = 0
        while $i < $methodSize
            set $methodName = (char*) $arg0->methods->keys->getField($i)->toString()
            set $method     = (TMethod*) $arg0->methods->values->getField($i)
            printf "%s, ", $methodName
            p $method
            set $i++
        end
        printf "\n"
    end
end

document tclass
    Prints TClass information.
    Syntax: tclass <TClass>
    Example:
    tclass globals.smallIntClass
end

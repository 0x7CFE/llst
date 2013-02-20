import gdb

#python execfile("../misc/views.py")


char_ptr_ty = gdb.lookup_type("char").pointer()
g_nil   = gdb.parse_and_eval("globals.nilObject")
g_true  = gdb.parse_and_eval("globals.trueObject")
g_false = gdb.parse_and_eval("globals.falseObject")

class TSymbolPrinter:
    def __init__(self, val):
        self.val = val
    
    def to_string (self):
        name = gdb.parse_and_eval("((TSymbol*) %d)->toString()" % self.val).cast(char_ptr_ty).string()
        return "#" + name
    
    def display_hint (self):
        return 'string'

class TSmallIntPrinter:
    def __init__(self, val):
        self.val = val
    
    def to_string (self):
        integer = gdb.parse_and_eval("getIntegerValue(%d)" % self.val)
        return str(integer)
    
    def display_hint (self):
        return 'string'

class TStringPrinter:
    def __init__(self, val):
        self.val = val
    
    def to_string (self):
        string = gdb.parse_and_eval("((TString*) %d)->getBytes()" % self.val).string()
        return string
    
    def display_hint (self):
        return 'string'
        
class TObjectArrayPrinter:
    def __init__(self, val):
        self.val = val
    
    def to_string (self):
        size = gdb.parse_and_eval("((TObjectArray*) %d)->getSize()" % self.val)
        return "Array got %d elements" % size
    
    def children(self):
        size = gdb.parse_and_eval("((TObjectArray*) %d)->getSize()" % self.val)
        for i in range(size):
            elem = gdb.parse_and_eval("((TObjectArray*) %d)->getField((uint32_t) %d)" % (self.val, i)).cast( gdb.lookup_type("TObject").pointer() )
            yield ("[%s]" % str(i), elem)
        #FIXME Why gdb.error is raised?
    
    def display_hint (self):
        return 'array'

#class TClassPrinter:
#    def __init__(self, val):
#        self.val = val
#    
#    def to_string (self):
#        if self.val == g_nil:
#            return "nil"
#        
#        class_name = gdb.parse_and_eval("((TClass*) %d)->name->toString()" % self.val).cast(char_ptr_ty).string()
#        return class_name
#    
#    def children(self):
#        if self.val == g_nil:
#            raise StopIteration
#        
#        for field in self.val.dereference().type.fields()[1:] :
#            yield ( field.name, self.val[ field.name ] )
#            
#    def display_hint (self):
#        return 'array'

class TObjectPrinter:
    def __init__(self, val):
        self.val = val
    
    def to_string (self):
        if self.val == g_nil:
            return "nil"
        
        if self.val == g_true:
            return "true"
        
        if self.val == g_false:
            return "false"
        
        
        
        #class_name = gdb.parse_and_eval("((TObject*) %d)->getClass()->name->toString()" % self.val).cast(char_ptr_ty).string()
        
        #if class_name == "Symbol":
        #    return TSymbolPrinter(self.val).to_string()
        
        return None

def lookup_type (val):
    if val.type == gdb.lookup_type("TSymbol").pointer():
        return TSymbolPrinter(val)
    if val.type == gdb.lookup_type("TInteger"):
        return TSmallIntPrinter(val)
    if val.type == gdb.lookup_type("TString").pointer():
        return TStringPrinter(val)
    if val.type == gdb.lookup_type("TObjectArray").pointer():
        return TObjectArrayPrinter(val)
    #if val.type == gdb.lookup_type("TClass").pointer():
    #    return TClassPrinter(val)
        
    
    #if val.type == gdb.lookup_type("TObject").pointer():
    #    return TObjectPrinter(val)
    return None

gdb.pretty_printers.append (lookup_type)
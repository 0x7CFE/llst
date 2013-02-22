import gdb

#add lines to ~/.gdbinit
#python
#  import sys
#  sys.path.insert(0, '/LLSTPATH/misc/')
#  from views import register_printers
#  register_printers ()
#end

def g_nil():
    return gdb.parse_and_eval("globals.nilObject")
def g_true():
    return gdb.parse_and_eval("globals.trueObject")
def g_false(): 
    return gdb.parse_and_eval("globals.falseObject")

char_ptr_ty = gdb.lookup_type("char").pointer()

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

#class TClassPrinter:
#    def __init__(self, val):
#        self.val = val
#    
#    def to_string (self):
#        if self.val == g_nil():
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
        if self.val == g_nil():
            return "nil"
        
        if self.val == g_true():
            return "true"
        
        if self.val == g_false():
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
    #if val.type == gdb.lookup_type("TClass").pointer():
    #    return TClassPrinter(val)
        
    
    if val.type == gdb.lookup_type("TObject").pointer():
        return TObjectPrinter(val)
    return None
    
def register_printers ():
    gdb.pretty_printers.append (lookup_type)
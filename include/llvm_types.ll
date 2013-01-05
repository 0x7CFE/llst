; ModuleID = 'types'
%struct.TSize = type { i32 ; data
                     }
%struct.TObject = type { %struct.TSize, ; size
                         %struct.TClass*, ; class
                         [0 x %struct.TObject*] ; fields
                       }
%struct.TByteObject = type { %struct.TObject }
%struct.TSymbol = type { %struct.TByteObject }
%struct.TString = type { %struct.TByteObject }
%struct.TChar = type { %struct.TObject,
                       i32 ; value
                     }
%struct.TArray = type { %struct.TObject }
%struct.TObjectArray = type { %struct.TObject }
%struct.TSymbolArray = type { %struct.TObject }
%struct.TContext = type { %struct.TObject,
                          %struct.TMethod*,
                          %struct.TObjectArray*, ; arguments
                          %struct.TObjectArray*, ; temporaries
                          %struct.TObjectArray*, ; stack
                          i32, ; bytePointer
                          i32, ; stackTop
                          %struct.TContext* ; previousContext
                        }
%struct.TBlock = type { %struct.TContext,
                        i32, ; argumentLocation
                        %struct.TContext*, ; creatingContext
                        i32  ; blockBytePointer
                      }
%struct.TMethod = type { %struct.TObject,
                         %struct.TSymbol*, ; name
                         %struct.TByteObject*, ; byteCodes
                         %struct.TSymbolArray*, ; literals
                         i32, ; stackSize
                         i32, ; temporarySize
                         %struct.TClass*, ; class
                         %struct.TString*, ; text
                         %struct.TObject* ; package
                       }
%struct.TDictionary = type { %struct.TObject,
                             %struct.TSymbolArray*, ; keys
                             %struct.TObjectArray* ; values
                           }
%struct.TClass = type { %struct.TObject,
                        %struct.TSymbol*, ; name
                        %struct.TClass*, ; parentClass
                        %struct.TDictionary*, ; methods
                        i32, ; instanceSize
                        %struct.TSymbolArray*, ; variables
                        %struct.TObject* ; package
                      }
%struct.TProcess = type { %struct.TObject,
                          %struct.TContext*, ; context
                          %struct.TObject*, ; state
                          %struct.TObject* ; result
                        }

; We can use extern C++ function but
; llvm passes may optimize/inline IR code.

define i1 @"isSmallInteger()"(%struct.TObject* %value) {
    %int = ptrtoint %struct.TObject* %value to i32
    %flag = and i32 %int, 1
    %result = trunc i32 %flag to i1
    ret i1 true
}

define i32 @"getIntegerValue()"(%struct.TObject* %value) {
    %int = ptrtoint %struct.TObject* %value to i32
    %result = ashr i32 %int, 1
    ret i32 %result
}

define i32 @"TObject::getSize()"(%struct.TObject* %this) {
    %1 = getelementptr %struct.TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %result = lshr i32 %data, 2
    ret i32 %result
}

define i1 @"TObject::isRelocated()"(%struct.TObject* %this) {
    %1 = getelementptr %struct.TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %field = and i32 %data, 1
    %result = trunc i32 %field to i1
    ret i1 %result
}

define i1 @"TObject::isBinary()"(%struct.TObject* %this) {
    %1 = getelementptr %struct.TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %field = and i32 %data, 2
    %result = icmp ne i32 %field, 0
    ret i1 %result
}

define %struct.TClass* @"TObject::getClass()"(%struct.TObject* %this) {
    %addr = getelementptr %struct.TObject* %this, i32 0, i32 1
    %class = load %struct.TClass** %addr
    ret %struct.TClass* %class
}

define %struct.TObject** @"TObject::getFields()"(%struct.TObject* %this) {
    %fields = getelementptr %struct.TObject* %this, i32 0, i32 2, i32 0
    ret %struct.TObject** %fields
}

define void @"MethodCompilerExample"(%struct.TContext* %context) {
entry:
    %args  = getelementptr %struct.TContext* %context, i32 2, i32 0, i32 2, i32 0
    %temps = getelementptr %struct.TContext* %context, i32 3, i32 0, i32 2, i32 0
    %self  = getelementptr %struct.TObject** %args, i32 0
    
    ; push argument 3
    %ptr.0  = getelementptr %struct.TObject** %args, i32 3
    %load.0 = load %struct.TObject** %ptr.0
    ; we remember stack[0] = instruction %load.0
    
    ; assign instance 5
    %ptr.1 = getelementptr %struct.TObject** %self, i32 5 ; FIXME bad gep
    ; and now we take the top instruction from our stack (stack[0] = load.0)
    store %struct.TObject* %load.0, %struct.TObject** %ptr.1
    
    ;push instance 4
    %ptr.2   = getelementptr %struct.TObject** %self, i32 4 ; FIXME
    %load.2 = load %struct.TObject** %ptr.2
    ; we remember stack[1] = instruction %load.2
    
    ; assign temporary 3
    %ptr.3 = getelementptr %struct.TObject** %temps, i32 3
    store %struct.TObject* %load.2, %struct.TObject** %ptr.3
    
    ret void
}

define i32 @main() {
    ret i32 0
}
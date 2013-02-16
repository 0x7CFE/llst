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
%struct.TGlobals = type { %struct.TObject*, ; nilObject
                          %struct.TObject*, ; trueObject
                          %struct.TObject*, ; falseObject
                          %struct.TClass*, ; smallIntClass
                          %struct.TClass*, ; arrayClass
                          %struct.TClass*, ; blockClass
                          %struct.TClass*, ; contextClass
                          %struct.TClass*, ; stringClass
                          %struct.TDictionary*, ; globalsObject
                          %struct.TMethod*, ; initialMethod
                          [3 x %struct.TObject*], ; binaryMessages : [<, <=, +]
                          %struct.TClass*, ; integerClass
                          %struct.TSymbol* ; badMethodSymbol
                        }

%struct.TBlockReturn = type {
    %struct.TObject*, ; value
    %struct.TContext* ; targetContext
}

; We can use extern C++ function but
; llvm passes may optimize/inline IR code.

define i1 @"isSmallInteger()"(%struct.TObject* %value) {
    %int = ptrtoint %struct.TObject* %value to i32
    ;%flag = and i32 %int, 1
    %result = trunc i32 %int to i1
    ret i1 %result
}

define i32 @"getIntegerValue()"(%struct.TObject* %value) {
    %int = ptrtoint %struct.TObject* %value to i32
    %result = ashr i32 %int, 1
    ret i32 %result
}

define %struct.TObject* @"newInteger()"(i32 %value) {
    %shled = shl i32 %value, 1
    %ored  = or  i32 %shled, 1
    %result = inttoptr i32 %ored to %struct.TObject*
    ret %struct.TObject* %result
}

define i32 @"getSlotSize()"(i32 %fieldsCount) {
    ;sizeof(TObject) + fieldsCount * sizeof(TObject*)

    %fieldsSize = mul i32 4, %fieldsCount
    %slotSize   = add i32 8, %fieldsSize

    ret i32 %slotSize
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
    %fields = getelementptr inbounds %struct.TObject* %this, i32 0, i32 2
    %result = getelementptr inbounds [0 x %struct.TObject*]* %fields, i32 0, i32 0
    ret %struct.TObject** %result
}

; FIXME demangle TObject ::getField() properly

define %struct.TObject* @"TObject::getField(int)"(%struct.TObject* %this, i32 %index) {
    %fields    = getelementptr inbounds %struct.TObject* %this, i32 0, i32 2
    %resultPtr = getelementptr inbounds [0 x %struct.TObject*]* %fields, i32 0, i32 %index
    %result    = load %struct.TObject** %resultPtr
    ret %struct.TObject* %result
}

declare void @llvm.memcpy.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %size, i32 %align, i1 %volatile)
declare void @llvm.gcroot(i8** %ptrloc, i8* %metadata)

define %struct.TObject* @dummy() gc "shadow-stack" {
    ; enabling shadow stack init on this module
    ret %struct.TObject* null
}

; memory management functions
;declare %struct.TObject*     @newOrdinaryFunction(%struct.TClass, i32)
;declare %struct.TByteObject* @newBinaryFunction(%struct.TClass, i32)

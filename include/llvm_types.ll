; This module implements types described in types.h within LLVM.
; Also it implements some base functions and methods such as TObject::getClass(). 
;  LLVM passes may optimize/inline them => we will gain more perfomance.

%TSize = type { i32 ; data
              }
%TObject = type { %TSize, ; size
                  %TClass*, ; class
                  [0 x %TObject*] ; fields
                }
%TByteObject = type { %TObject }
%TSymbol = type { %TByteObject }
%TString = type { %TByteObject }
%TChar = type { %TObject,
                i32 ; value
              }
%TArray = type { %TObject }
%TObjectArray = type { %TObject }
%TSymbolArray = type { %TObject }
%TContext = type { %TObject,
                   %TMethod*,
                   %TObjectArray*, ; arguments
                   %TObjectArray*, ; temporaries
                   %TObjectArray*, ; stack
                   i32, ; bytePointer
                   i32, ; stackTop
                   %TContext* ; previousContext
                 }
%TBlock = type { %TContext,
                 i32, ; argumentLocation
                 %TContext*, ; creatingContext
                 i32 ; blockBytePointer
               }
%TMethod = type { %TObject,
                  %TSymbol*, ; name
                  %TByteObject*, ; byteCodes
                  %TSymbolArray*, ; literals
                  i32, ; stackSize
                  i32, ; temporarySize
                  %TClass*, ; class
                  %TString*, ; text
                  %TObject* ; package
                }
%TDictionary = type { %TObject,
                      %TSymbolArray*, ; keys
                      %TObjectArray* ; values
                    }
%TClass = type { %TObject,
                 %TSymbol*, ; name
                 %TClass*, ; parentClass
                 %TDictionary*, ; methods
                 i32, ; instanceSize
                 %TSymbolArray*, ; variables
                 %TObject* ; package
               }
%TProcess = type { %TObject,
                   %TContext*, ; context
                   %TObject*, ; state
                   %TObject* ; result
                 }
%TGlobals = type { %TObject*, ; nilObject
                   %TObject*, ; trueObject
                   %TObject*, ; falseObject
                   %TClass*, ; smallIntClass
                   %TClass*, ; arrayClass
                   %TClass*, ; blockClass
                   %TClass*, ; contextClass
                   %TClass*, ; stringClass
                   %TDictionary*, ; globalsObject
                   %TMethod*, ; initialMethod
                   [3 x %TObject*], ; binaryMessages : [<, <=, +]
                   %TClass*, ; integerClass
                   %TSymbol* ; badMethodSymbol
                 }

%TBlockReturn = type {
                       %TObject*, ; value
                       %TContext* ; targetContext
                     }


define i1 @"isSmallInteger()"(%TObject* %value) {
    %int = ptrtoint %TObject* %value to i32
    ;%flag = and i32 %int, 1
    %result = trunc i32 %int to i1
    ret i1 %result
}

define i32 @"getIntegerValue()"(%TObject* %value) {
    %int = ptrtoint %TObject* %value to i32
    %result = ashr i32 %int, 1
    ret i32 %result
}

define %TObject* @"newInteger()"(i32 %value) {
    %shled = shl i32 %value, 1
    %ored  = or  i32 %shled, 1
    %result = inttoptr i32 %ored to %TObject*
    ret %TObject* %result
}

define i32 @"getSlotSize()"(i32 %fieldsCount) {
    ;sizeof(TObject) + fieldsCount * sizeof(TObject*)

    %fieldsSize = mul i32 4, %fieldsCount
    %slotSize   = add i32 8, %fieldsSize

    ret i32 %slotSize
}


define i32 @"TObject::getSize()"(%TObject* %this) {
    %1 = getelementptr %TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %result = lshr i32 %data, 2
    ret i32 %result
}

define i1 @"TObject::isRelocated()"(%TObject* %this) {
    %1 = getelementptr %TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %field = and i32 %data, 1
    %result = trunc i32 %field to i1
    ret i1 %result
}

define i1 @"TObject::isBinary()"(%TObject* %this) {
    %1 = getelementptr %TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %field = and i32 %data, 2
    %result = icmp ne i32 %field, 0
    ret i1 %result
}

define %TClass* @"TObject::getClass()"(%TObject* %this) {
    %addr = getelementptr %TObject* %this, i32 0, i32 1
    %class = load %TClass** %addr
    ret %TClass* %class
}

define %TObject** @"TObject::getFields()"(%TObject* %this) {
    %fields = getelementptr inbounds %TObject* %this, i32 0, i32 2
    %result = getelementptr inbounds [0 x %TObject*]* %fields, i32 0, i32 0
    ret %TObject** %result
}

; FIXME demangle TObject ::getField() properly

define %TObject* @getObjectField(%TObject* %object, i32 %index) {
    %fields    = getelementptr inbounds %TObject* %object, i32 0, i32 2
    %fieldPtr  = getelementptr inbounds [0 x %TObject*]* %fields, i32 0, i32 %index
    %result    = load %TObject** %fieldPtr
    ret %TObject* %result
}

define %TObject** @setObjectField(%TObject* %object, i32 %index, %TObject* %value) {
    %fields   = getelementptr inbounds %TObject* %object, i32 0, i32 2
    %fieldPtr = getelementptr inbounds [0 x %TObject*]* %fields, i32 0, i32 %index
    store %TObject* %value, %TObject** %fieldPtr
    ret %TObject** %fieldPtr
}

declare void @llvm.memcpy.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %size, i32 %align, i1 %volatile)
declare void @llvm.gcroot(i8** %ptrloc, i8* %metadata)

define %TObject* @dummy() gc "shadow-stack" {
    ; enabling shadow stack init on this module
    ret %TObject* null
}

; memory management functions
;declare %TObject*     @newOrdinaryFunction(%TClass, i32)
;declare %TByteObject* @newBinaryFunction(%TClass, i32)



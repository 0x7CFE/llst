;
;    Core.ll
;
;    This is a JIT core file. It describes basic Smalltalk types
;    (defined in types.h) from the LLVM's point of view.
;
;    Also a lot of functions are presented that help perform
;    object and field access witin the LLVM IR code. They're
;    small and almost all of them gets inlined, so it does not
;    affect the perfomance of JIT code.
;
;    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.4
;
;    LLST is
;        Copyright (C) 2012-2015 by Dmitry Kashitsyn   <korvin@deeptown.org>
;        Copyright (C) 2012-2015 by Roman Proskuryakov <humbug@deeptown.org>
;
;    LLST is based on the LittleSmalltalk which is
;        Copyright (C) 1987-2005 by Timothy A. Budd
;        Copyright (C) 2007 by Charles R. Childers
;        Copyright (C) 2005-2007 by Danny Reinhold
;
;    Original license of LittleSmalltalk may be found in the LICENSE file.
;
;
;    This file is part of LLST.
;    LLST is free software: you can redistribute it and/or modify
;    it under the terms of the GNU General Public License as published by
;    the Free Software Foundation, either version 3 of the License, or
;    (at your option) any later version.
;
;    LLST is distributed in the hope that it will be useful,
;    but WITHOUT ANY WARRANTY; without even the implied warranty of
;    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;    GNU General Public License for more details.
;
;    You should have received a copy of the GNU General Public License
;    along with LLST.  If not, see <http://www.gnu.org/licenses/>.

; This module implements types described in types.h within LLVM.
; Also it implements some base functions and methods such as TObject::getClass().
; LLVM passes may optimize/inline them => we will gain more perfomance.


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;; types ;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%TSize = type {
    i32             ; data
}

%TObject = type {
    %TSize,         ; size
    %TClass*,       ; class
    [0 x %TObject*] ; fields
}

%TByteObject = type { %TObject }
%TSymbol     = type { %TByteObject }
%TString     = type { %TByteObject }

%TChar = type {
    %TObject,
    i32             ; value
}

%TArray       = type { %TObject }
%TObjectArray = type { %TObject }
%TSymbolArray = type { %TObject }

%TContext = type {
    %TObject,
    %TMethod*,
    %TObjectArray*, ; arguments
    %TObjectArray*, ; temporaries
    %TObjectArray*, ; stack
    i32,            ; bytePointer
    i32,            ; stackTop
    %TContext*      ; previousContext
}

%TBlock = type {
    %TContext,
    i32,            ; argumentLocation
    %TContext*,     ; creatingContext
    i32             ; blockBytePointer
}

%TMethod = type {
    %TObject,
    %TSymbol*,      ; name
    %TByteObject*,  ; byteCodes
    %TSymbolArray*, ; literals
    i32,            ; stackSize
    i32,            ; temporarySize
    %TClass*,       ; class
    %TString*,      ; text
    %TObject*       ; package
}

%TDictionary = type {
    %TObject,
    %TSymbolArray*, ; keys
    %TObjectArray*  ; values
}

%TClass = type {
    %TObject,
    %TSymbol*,      ; name
    %TClass*,       ; parentClass
    %TDictionary*,  ; methods
    i32,            ; instanceSize
    %TSymbolArray*, ; variables
    %TObject*       ; package
}

%TProcess = type {
    %TObject,
    %TContext*,     ; context
    %TObject*,      ; state
    %TObject*       ; result
}

%TGlobals = type {
    %TObject*,      ; nilObject
    %TObject*,      ; trueObject
    %TObject*,      ; falseObject
    %TClass*,       ; smallIntClass
    %TClass*,       ; arrayClass
    %TClass*,       ; blockClass
    %TClass*,       ; contextClass
    %TClass*,       ; stringClass
    %TDictionary*,  ; globalsObject
    %TMethod*,      ; initialMethod
    [3x%TObject*],  ; binaryMessages : [<, <=, +]
    %TClass*,       ; integerClass
    %TSymbol*       ; badMethodSymbol
}

%TBlockReturn = type {
    %TObject*,      ; value
    %TContext*      ; targetContext
}

%TGCMetaData = type {
    i1 ; isStackObject
}

@stackObjectMeta = constant %TGCMetaData { i1 true }

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;; functions ;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

define i1 @isSmallInteger(%TObject* %value) alwaysinline {
    ; return reinterpret_cast<int32_t>(value) & 1;

    %int = ptrtoint %TObject* %value to i32
    %result = trunc i32 %int to i1
    ret i1 %result
}

define i32 @getIntegerValue(%TObject* %value) alwaysinline {
    ; return (int32_t) (value >> 1)

    %int = ptrtoint %TObject* %value to i32
    %result = ashr i32 %int, 1
    ret i32 %result
}

define %TObject* @newInteger(i32 %value) alwaysinline {
    ; return reinterpret_cast<TObject>( (value << 1) | 1 );

    %shled = shl i32 %value, 1
    %ored  = or  i32 %shled, 1
    %result = inttoptr i32 %ored to %TObject*
    ret %TObject* %result
}

define i32 @getSlotSize(i32 %fieldsCount) alwaysinline {
    ;sizeof(TObject) + fieldsCount * sizeof(TObject*)

    %fieldsSize = mul i32 4, %fieldsCount
    %slotSize   = add i32 8, %fieldsSize

    ret i32 %slotSize
}


define i32 @getObjectSize(%TObject* %this) alwaysinline {
    %1 = getelementptr %TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %result = lshr i32 %data, 2
    ret i32 %result
}

define %TObject* @setObjectSize(%TObject* %this, i32 %size) alwaysinline {
    %addr = getelementptr %TObject* %this, i32 0, i32 0, i32 0
    %ssize = shl i32 %size, 2
    store i32 %ssize, i32* %addr
    ret %TObject* %this
}

define i1 @isObjectRelocated(%TObject* %this) alwaysinline {
    %1 = getelementptr %TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %field = and i32 %data, 1
    %result = trunc i32 %field to i1
    ret i1 %result
}

define i1 @isObjectBinary(%TObject* %this) alwaysinline {
    %1 = getelementptr %TObject* %this, i32 0, i32 0, i32 0
    %data = load i32* %1
    %field = and i32 %data, 2
    %result = icmp ne i32 %field, 0
    ret i1 %result
}

define %TClass** @getObjectClassPtr(%TObject* %this) alwaysinline {
    %pclass = getelementptr inbounds %TObject* %this, i32 0, i32 1
    ret %TClass** %pclass
}

@SmallInt = external global %TClass

define %TClass* @getObjectClass(%TObject* %this) alwaysinline {
    ; TODO SmallInt case
    %test = call i1 @isSmallInteger(%TObject* %this)
    br i1 %test, label %is_smallint, label %is_object
is_smallint:
    ret %TClass* @SmallInt
is_object:
    %addr = call %TClass** @getObjectClassPtr(%TObject* %this)
    %class = load %TClass** %addr
    ret %TClass* %class
}

define %TObject* @setObjectClass(%TObject* %this, %TClass* %class) alwaysinline {
    %addr = call %TClass** @getObjectClassPtr(%TObject* %this)
    store %TClass* %class, %TClass** %addr
    ret %TObject* %this
}

define %TObject** @getObjectFieldPtr(%TObject* %object, i32 %index) alwaysinline {
    %fields    = getelementptr inbounds %TObject* %object, i32 0, i32 2
    %fieldPtr  = getelementptr inbounds [0 x %TObject*]* %fields, i32 0, i32 %index
    ret %TObject** %fieldPtr
}

define %TObject** @getObjectFields(%TObject* %this) alwaysinline {
    %fieldsPtr = call %TObject** @getObjectFieldPtr(%TObject* %this, i32 0)
    ret %TObject** %fieldsPtr
}

define %TObject* @getObjectField(%TObject* %object, i32 %index) alwaysinline {
    %fieldPtr  = call %TObject** @getObjectFieldPtr(%TObject* %object, i32 %index)
    %result    = load %TObject** %fieldPtr
    ret %TObject* %result
}

define %TObject** @setObjectField(%TObject* %object, i32 %index, %TObject* %value) alwaysinline {
    %fieldPtr = call %TObject** @getObjectFieldPtr(%TObject* %object, i32 %index)
    store %TObject* %value, %TObject** %fieldPtr
    ret %TObject** %fieldPtr
}

define %TObject* @getArgFromContext(%TContext* %context, i32 %index) alwaysinline {
    %argsPtr = getelementptr inbounds %TContext* %context, i32 0, i32 2
    %args    = load %TObjectArray** %argsPtr
    %argsObj = bitcast %TObjectArray* %args to %TObject*
    %arg     = call %TObject* @getObjectField(%TObject* %argsObj, i32 %index)
    ret %TObject* %arg
}

define %TObject* @getLiteralFromContext(%TContext* %context, i32 %index) alwaysinline {
    %methodPtr   = getelementptr inbounds %TContext* %context, i32 0, i32 1
    %method      = load %TMethod** %methodPtr
    %literalsPtr = getelementptr inbounds %TMethod* %method, i32 0, i32 3
    %literals    = load %TSymbolArray** %literalsPtr
    %literalsObj = bitcast %TSymbolArray* %literals to %TObject*
    %literal     = call %TObject* @getObjectField(%TObject* %literalsObj, i32 %index)
    ret %TObject* %literal
}

define %TObject* @getTempsFromContext(%TContext* %context) alwaysinline {
    %tempsPtr = getelementptr inbounds %TContext* %context, i32 0, i32 3
    %temps    = load %TObjectArray** %tempsPtr
    %tempsObj = bitcast %TObjectArray* %temps to %TObject*
    ret %TObject* %tempsObj
}

define %TObject* @getTemporaryFromContext(%TContext* %context, i32 %index) alwaysinline {
    %temps = call %TObject* @getTempsFromContext(%TContext* %context)
    %temporary = call %TObject* @getObjectField(%TObject* %temps, i32 %index)
    ret %TObject* %temporary
}

define void @setTemporaryInContext(%TContext* %context, i32 %index, %TObject* %value) alwaysinline {
    %temps = call %TObject* @getTempsFromContext(%TContext* %context)
    call %TObject** @setObjectField(%TObject* %temps, i32 %index, %TObject* %value)
    ret void
}

define %TObject* @getInstanceFromContext(%TContext* %context, i32 %index) alwaysinline {
    %self = call %TObject* @getArgFromContext(%TContext* %context, i32 0)
    %instance = call %TObject* @getObjectField(%TObject* %self, i32 %index)
    ret %TObject* %instance
}

define void @setInstanceInContext(%TContext* %context, i32 %index, %TObject* %value) alwaysinline {
    %self = call %TObject* @getArgFromContext(%TContext* %context, i32 0)
    %instancePtr = call %TObject** @getObjectFieldPtr(%TObject* %self, i32 %index)
    call void @checkRoot(%TObject* %value, %TObject** %instancePtr)
    store %TObject* %value, %TObject** %instancePtr
    ret void
}

define void @dummy() gc "shadow-stack" {
    ; enabling shadow stack init on this module
    ret void
}

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;; memory management functions ;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

declare %TObject* @newOrdinaryObject(%TClass*, i32)
declare %TByteObject* @newBinaryObject(%TClass*, i32)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;; runtime API ;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;declare %TObject* @sendMessage(%TContext* %callingContext, %TSymbol* %selector, %TObjectArray* %arguments, %TClass* %receiverClass, i32 %callSiteOffset)
declare { %TObject*, %TContext* } @sendMessage(%TContext* %callingContext, %TSymbol* %selector, %TObjectArray* %arguments, %TClass* %receiverClass, i32 %callSiteOffset)

declare %TBlock*  @createBlock(%TContext* %callingContext, i8 %argLocation, i16 %bytePointer)
declare { %TObject*, %TContext* } @invokeBlock(%TBlock* %block, %TContext* %callingContext)
;declare %TObject* @invokeBlock(%TBlock* %block, %TContext* %callingContext)

declare void @emitBlockReturn(%TObject* %value, %TContext* %targetContext)
declare void @checkRoot(%TObject* %value, %TObject** %slot)
declare i1 @bulkReplace(%TObject* %destination, %TObject* %sourceStartOffset, %TObject* %source, %TObject* %destinationStopOffset, %TObject* %destinationStartOffset)
declare %TObject* @callPrimitive(i8 %opcode, %TObjectArray* %args, i1* %primitiveFailed)

%TReturnValue = type {
    %TObject*,      ; value
    %TContext*      ; targetContext
}

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;; exception API ;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

declare i32 @__gcc_personality_v0(...)
declare i32 @__gxx_personality_v0(...)
declare i8* @__cxa_begin_catch(i8*)
declare void @__cxa_end_catch()
declare i8* @__cxa_allocate_exception(i32)
declare void @__cxa_throw(i8*, i8*, i8*)


define { i32, i32 } @"test2"(%TContext* %contextParameter) #1 {
  %1 = insertvalue { i32, i32 } undef, i32 1, 0
  %2 = insertvalue { i32, i32 } %1, i32 2, 1
  ret { i32, i32 } %2
}

; Function Attrs: inlinehint
define { %TObject*, %TContext* } @"Mock>>selfReturn_"(%TContext* %pContext) #1 {
preamble:
  %0 = getelementptr inbounds %TContext* %pContext, i32 0, i32 2
  %1 = load %TObjectArray** %0
  %2 = getelementptr inbounds %TObjectArray* %1, i32 0, i32 0
  %fields.i.i = getelementptr inbounds %TObject* %2, i32 0, i32 2
  %fieldPtr.i.i = getelementptr inbounds [0 x %TObject*]* %fields.i.i, i32 0, i32 0
  %result.i = load %TObject** %fieldPtr.i.i
  %3 = insertvalue { %TObject*, %TContext* } undef, %TObject* %result.i, 0
  %4 = insertvalue { %TObject*, %TContext* } %3, %TContext* null, 1
  ret { %TObject*, %TContext* } %4
}

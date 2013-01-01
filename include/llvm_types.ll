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

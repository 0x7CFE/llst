
CLASS Exception Object      message backTrace

METHOD MetaException
throw: aMessage |instance|
    instance <- self new message: aMessage.
    instance throw
!

METHOD Exception
message: aMessage
    message <- aMessage
!

METHOD Exception
message
    ^message
!

METHOD Exception
throw
    <42 self>
    self primitiveFailed.
!

METHOD Block
registerHandler: aBlock for: anException
    <43 self aBlock anException>
!

METHOD Block
tryCatch: anException with: aBlock
    self registerHandler: aBlock for: anException.
    self value.
!

METHOD Block
test
    [ Exception throw: 'Hello' ]
        tryCatch: Exception
        with: [ :e | e printStackTrace ].
!

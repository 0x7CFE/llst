#include <instruction.h>

std::string TInstruction::toString()
{
    std::ostringstream ss;
    
    int iHigh = (int) high;
    int iLow  = (int) low;
    
    std::ostringstream errSs;
    errSs << "Unknown instrunction {" << iHigh << ", " << iLow << "}";
    
    switch(high)
    {
        case opcode::pushInstance:    ss << "PushInstance " << iLow;  break;
        case opcode::pushArgument:    ss << "PushArgument " << iLow;  break;
        case opcode::pushTemporary:   ss << "PushTemporary " << iLow; break;
        case opcode::pushLiteral:     ss << "PushLiteral " << iLow;   break;
        case opcode::pushConstant: {
            ss << "PushConstant ";
            switch(low) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                    ss << iLow;
                    break;
                case pushConstants::nil:         ss << "nil";   break;
                case pushConstants::trueObject:  ss << "true";  break;
                case pushConstants::falseObject: ss << "false"; break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;
        case opcode::pushBlock:       ss << "PushBlock " << iLow;       break;
        
        case opcode::assignTemporary: ss << "AssignTemporary " << iLow; break;
        case opcode::assignInstance:  ss << "AssignInstance " << iLow;  break;
        
        case opcode::markArguments:   ss << "MarkArguments " << iLow;   break;
        
        case opcode::sendUnary: {
            ss << "SendUnary ";
            switch(low) {
                case unaryMessage::isNil:  ss << "isNil";    break;
                case unaryMessage::notNil: ss << "isNotNil"; break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;
        case opcode::sendBinary: {
            ss << "SendBinary ";
            switch(low) {
                case binaryMessage::operatorPlus:     ss << "+";  break;
                case binaryMessage::operatorLess:     ss << "<";  break;
                case binaryMessage::operatorLessOrEq: ss << "<="; break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;
        case opcode::sendMessage: ss << "SendMessage "; break;
        
        case opcode::doSpecial: {
            ss << "Special ";
            switch(low) {
                case special::selfReturn:       ss << "selfReturn";     break;
                case special::stackReturn:      ss << "stackReturn";    break;
                case special::blockReturn:      ss << "blockReturn";    break;
                case special::duplicate:        ss << "duplicate";      break;
                case special::popTop:           ss << "popTop";         break;
                case special::branch:           ss << "branch";         break;
                case special::branchIfTrue:     ss << "branchIfTrue";   break;
                case special::branchIfFalse:    ss << "branchIfFalse";  break;
                case special::sendToSuper:      ss << "sendToSuper";    break;
                case special::breakpoint:       ss << "breakpoint";     break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;
        case opcode::doPrimitive: ss << "Primitive"; break;
        
        default: {
            throw std::runtime_error(errSs.str());
        }
    }
    return ss.str();
}
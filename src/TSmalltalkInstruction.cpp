#include <instructions.h>
#include <stdexcept>

bool st::TSmalltalkInstruction::isTerminator() const
{
    if (m_opcode != opcode::doSpecial)
        return false;

    if (isBranch())
        return true;

    switch (m_argument) {
        case special::stackReturn:
        case special::selfReturn:
        case special::blockReturn:
            return true;

        default:
            return false;
    }
}

bool st::TSmalltalkInstruction::isBranch() const
{
    if (m_opcode != opcode::doSpecial)
        return false;

    switch (m_argument) {
        case special::branch:
        case special::branchIfFalse:
        case special::branchIfTrue:
            return true;

        default:
            return false;
    }
}

bool st::TSmalltalkInstruction::isValueProvider() const {
    switch (m_opcode) {
        case opcode::pushInstance:
        case opcode::pushArgument:
        case opcode::pushTemporary:
        case opcode::pushLiteral:
        case opcode::pushBlock:
        case opcode::pushConstant:
        case opcode::markArguments:
        case opcode::sendMessage:
        case opcode::sendUnary:
        case opcode::sendBinary:
            return true;

        case opcode::assignTemporary:
        case opcode::assignInstance:
        case opcode::doPrimitive: // ?
            return false;

        case opcode::doSpecial:
            switch (m_argument) {
                case special::duplicate:
                case special::sendToSuper:
                    return true;

                case special::selfReturn:
                case special::stackReturn:
                case special::blockReturn:
                case special::popTop:
                case special::branch:
                case special::branchIfTrue:
                case special::branchIfFalse:
                    return false;
            }

        case opcode::extended:
            assert(false);
    }

    return false;
}

bool st::TSmalltalkInstruction::isTrivial() const {
    switch (m_opcode) {
        case opcode::pushInstance:
        case opcode::pushArgument:
        case opcode::pushTemporary:
        case opcode::pushLiteral:
        case opcode::pushConstant:
        case opcode::pushBlock:
        case opcode::markArguments:
            return true;

        case opcode::doSpecial:
            switch (m_argument) {
                case special::duplicate:
                    return true;
            }

        default:
            return false;
    }
}

bool st::TSmalltalkInstruction::isValueConsumer() const {
    assert(false); // TODO
    return false;
}

std::string st::TSmalltalkInstruction::toString() const
{
    std::ostringstream ss;

    int argument = m_argument; // They should be ints to be displayed
    int extra = m_extra; // correctly by stringstream

    std::ostringstream errSs;
    errSs << "Unknown instrunction {" << m_opcode << ", " << argument << ", " << extra << "}";

    switch(m_opcode)
    {
        case opcode::pushInstance:    ss << "PushInstance " << argument;      break;
        case opcode::pushArgument:    ss << "PushArgument " << argument;      break;
        case opcode::pushTemporary:   ss << "PushTemporary " << argument;     break;
        case opcode::pushLiteral:     ss << "PushLiteral " << argument;       break;
        case opcode::pushBlock:       ss << "PushBlock " << argument;         break;
        case opcode::assignTemporary: ss << "AssignTemporary " << argument;   break;
        case opcode::assignInstance:  ss << "AssignInstance " << argument;    break;
        case opcode::markArguments:   ss << "MarkArguments " << argument;     break;
        case opcode::sendMessage:     ss << "SendMessage ";                   break;

        case opcode::doPrimitive: {
            ss << "Primitive " << extra << " (" << argument << " arguments)";
        } break;

        case opcode::pushConstant: {
            ss << "PushConstant ";
            switch(argument) {
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
                    ss << argument;
                    break;
                case pushConstants::nil:         ss << "nil";   break;
                case pushConstants::trueObject:  ss << "true";  break;
                case pushConstants::falseObject: ss << "false"; break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;
        case opcode::sendUnary: {
            ss << "SendUnary ";
            switch(argument) {
                case unaryBuiltIns::isNil:  ss << "isNil";    break;
                case unaryBuiltIns::notNil: ss << "isNotNil"; break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;
        case opcode::sendBinary: {
            ss << "SendBinary ";
            switch(argument) {
                case binaryBuiltIns::operatorPlus:     ss << "+";  break;
                case binaryBuiltIns::operatorLess:     ss << "<";  break;
                case binaryBuiltIns::operatorLessOrEq: ss << "<="; break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;
        case opcode::doSpecial: {
            ss << "Special ";
            switch(argument) {
                case special::selfReturn:       ss << "selfReturn";     break;
                case special::stackReturn:      ss << "stackReturn";    break;
                case special::blockReturn:      ss << "blockReturn";    break;
                case special::duplicate:        ss << "duplicate";      break;
                case special::popTop:           ss << "popTop";         break;
                case special::branch:           ss << "branch";         break;
                case special::branchIfTrue:     ss << "branchIfTrue";   break;
                case special::branchIfFalse:    ss << "branchIfFalse";  break;
                case special::sendToSuper:      ss << "sendToSuper";    break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;

        default: {
            throw std::runtime_error(errSs.str());
        }
    }
    return ss.str();
}

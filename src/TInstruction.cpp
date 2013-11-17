/*
 *    TInstruction.cpp
 *
 *    Helper functions for TInstruction class
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.2
 *
 *    LLST is
 *        Copyright (C) 2012-2013 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2013 by Roman Proskuryakov <humbug@deeptown.org>
 *
 *    LLST is based on the LittleSmalltalk which is
 *        Copyright (C) 1987-2005 by Timothy A. Budd
 *        Copyright (C) 2007 by Charles R. Childers
 *        Copyright (C) 2005-2007 by Danny Reinhold
 *
 *    Original license of LittleSmalltalk may be found in the LICENSE file.
 *
 *
 *    This file is part of LLST.
 *    LLST is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    LLST is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with LLST.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <types.h>
#include <opcodes.h>
#include <stdexcept>

std::string TInstruction::toString()
{
    std::ostringstream ss;

    int iHigh = high;
    int iLow  = low;

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
                case unaryBuiltIns::isNil:  ss << "isNil";    break;
                case unaryBuiltIns::notNil: ss << "isNotNil"; break;
                default: {
                    throw std::runtime_error(errSs.str());
                }
            }
        } break;
        case opcode::sendBinary: {
            ss << "SendBinary ";
            switch(low) {
                case binaryBuiltIns::operatorPlus:     ss << "+";  break;
                case binaryBuiltIns::operatorLess:     ss << "<";  break;
                case binaryBuiltIns::operatorLessOrEq: ss << "<="; break;
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

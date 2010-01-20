
// Sixgill: Static assertion checker for C/C++ programs.
// Copyright (C) 2009-2010  Stanford University
// Author: Brian Hackett
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "operand.h"
#include "serial.h"

NAMESPACE_XGILL_BEGIN

/////////////////////////////////////////////////////////////////////
// TOperand static
/////////////////////////////////////////////////////////////////////

void TOperand::Write(Buffer *buf, const TOperand *o)
{
  WriteOpenTag(buf, TAG_TOperand);
  WriteTagUInt32(buf, TAG_Kind, o->Kind());

  switch (o->Kind()) {
  case TO_Variable: {
    const TOperandVariable *no = (const TOperandVariable*)o;
    WriteTagUInt32(buf, TAG_Index, no->GetName());
    break;
  }
  case TO_List: {
    const TOperandList *no = (const TOperandList*)o;
    for (size_t oind = 0; oind < no->GetCount(); oind++)
      TOperand::Write(buf, no->GetOperand(oind));
    break;
  }
  case TO_String: {
    const TOperandString *no = (const TOperandString*)o;
    WriteString(buf, no->GetData(), no->GetDataLength());
    break;
  }
  case TO_TimeStamp: {
    const TOperandTimeStamp *no = (const TOperandTimeStamp*)o;
    WriteTagUInt64(buf, TAG_TimeStamp, no->GetStamp());
    break;
  }
  case TO_Boolean: {
    const TOperandBoolean *no = (const TOperandBoolean*)o;
    WriteTagEmpty(buf, no->IsTrue() ? TAG_True : TAG_False);
    break;
  }
  default:
    Assert(false);
  }

  WriteCloseTag(buf, TAG_TOperand);
}

TOperand* TOperand::Read(Buffer *buf, Transaction *t)
{
  uint32_t kind = 0;
  uint32_t index = 0;
  uint64_t timestamp = 0;
  bool is_true = false;
  bool is_false = false;
  const uint8_t *str_base = NULL;
  size_t str_len = 0;
  Vector<TOperand*> ops;

  Try(ReadOpenTag(buf, TAG_TOperand));
  while (!ReadCloseTag(buf, TAG_TOperand)) {
    switch (PeekOpenTag(buf)) {
    case TAG_Kind: {
      Try(!kind);
      Try(ReadTagUInt32(buf, TAG_Kind, &kind));
      break;
    }
    case TAG_Index: {
      Try(!index);
      Try(kind == TO_Variable);
      Try(ReadTagUInt32(buf, TAG_Index, &index));
      break;
    }
    case TAG_True: {
      Try(!is_true && !is_false);
      Try(kind == TO_Boolean);
      Try(ReadTagEmpty(buf, TAG_True));
      is_true = true;
      break;
    }
    case TAG_False: {
      Try(!is_true && !is_false);
      Try(kind == TO_Boolean);
      Try(ReadTagEmpty(buf, TAG_False));
      is_false = true;
      break;
    }
    case TAG_String: {
      Try(!str_base);
      Try(kind == TO_String);
      Try(ReadString(buf, &str_base, &str_len));
      break;
    }
    case TAG_TimeStamp: {
      Try(kind == TO_TimeStamp);
      Try(ReadTagUInt64(buf, TAG_TimeStamp, &timestamp));
      break;
    }
    case TAG_TOperand: {
      Try(kind == TO_List);
      TOperand *op;
      Try(op = TOperand::Read(buf, t));
      Try(op->Kind() != TO_Variable);
      ops.PushBack(op);
      break;
    }
    default:
      Try(false);
    }
  }

  switch ((TOperandKind)kind) {
  case TO_Variable:
    Try(index);
    return new TOperandVariable(t, index);
  case TO_List: {
    TOperandList *list = new TOperandList(t);
    for (size_t oind = 0; oind < ops.Size(); oind++)
      list->PushOperand(ops[oind]);
    return list;
  }
  case TO_String: {
    Try(str_base);
    Buffer *buf = new Buffer(str_len);
    t->AddBuffer(buf);
    buf->Append(str_base, str_len);
    return new TOperandString(t, buf->base, str_len);
  }
  case TO_TimeStamp:
    return new TOperandTimeStamp(t, timestamp);
  case TO_Boolean:
    Try(is_true || is_false);
    return new TOperandBoolean(t, is_true);
  default:
    Try(false);
    return NULL;
  }
}

/////////////////////////////////////////////////////////////////////
// TOperandVariable
/////////////////////////////////////////////////////////////////////

TOperandVariable::TOperandVariable(Transaction *t, size_t name)
  : TOperand(t, TO_Variable), m_name(name)
{
  Assert(m_name != 0);
}

size_t TOperandVariable::GetName() const
{
  return m_name;
}

void TOperandVariable::Print(OutStream &out) const
{
  out << "$" << (long)m_name;
}

TOperand* TOperandVariable::Instantiate()
{
  return m_transaction->Lookup(m_name, false);
}

/////////////////////////////////////////////////////////////////////
// TOperandList
/////////////////////////////////////////////////////////////////////

TOperandList::TOperandList(Transaction *t)
  : TOperand(t, TO_List)
{}

void TOperandList::PushOperand(TOperand *op)
{
  // can't have variables in an operand list
  Assert(op->Kind() != TO_Variable);

  m_list.PushBack(op);
}

size_t TOperandList::GetCount() const
{
  return m_list.Size();
}

TOperand* TOperandList::GetOperand(size_t ind) const
{
  return m_list[ind];
}

TOperandList* TOperandList::GetOperandList(size_t ind) const
{
  TOperand *res = m_list[ind];
  Assert(res->Kind() == TO_List);
  return (TOperandList*) res;
}

TOperandString* TOperandList::GetOperandString(size_t ind) const
{
  TOperand *res = m_list[ind];
  Assert(res->Kind() == TO_String);
  return (TOperandString*) res;
}

TOperandTimeStamp* TOperandList::GetOperandTimeStamp(size_t ind) const
{
  TOperand *res = m_list[ind];
  Assert(res->Kind() == TO_TimeStamp);
  return (TOperandTimeStamp*) res;
}

TOperandBoolean* TOperandList::GetOperandBoolean(size_t ind) const
{
  TOperand *res = m_list[ind];
  Assert(res->Kind() == TO_Boolean);
  return (TOperandBoolean*) res;
}

void TOperandList::Print(OutStream &out) const
{
  out << "[";
  for (size_t ind = 0; ind < m_list.Size(); ind++) {
    if (ind != 0)
      out << ", ";
    m_list[ind]->Print(out);
  }
  out << "]";
}

/////////////////////////////////////////////////////////////////////
// TOperandString
/////////////////////////////////////////////////////////////////////

TOperandString* TOperandString::Compress(Transaction *t, Buffer *buf)
{
  Buffer *compress_buf = new Buffer();
  t->AddBuffer(compress_buf);

  CompressBufferInUse(buf, compress_buf);
  return new TOperandString(t, compress_buf->base,
                            compress_buf->pos - compress_buf->base);
}

void TOperandString::Uncompress(TOperandString *op, Buffer *buf)
{
  Buffer read_buf(op->GetData(), op->GetDataLength());
  UncompressBuffer(&read_buf, buf);
}

TOperandString::TOperandString(Transaction *t,
                               const uint8_t *data, size_t data_length)
  : TOperand(t, TO_String),
    m_data(data), m_data_length(data_length)
{
  Assert(m_data != NULL || m_data_length == 0);
}

TOperandString::TOperandString(Transaction *t,
                               const char *data)
  : TOperand(t, TO_String),
    m_data((const uint8_t*) data),
    m_data_length(0)
{
  Assert(data);
  m_data_length = strlen(data) + 1;
}

const uint8_t* TOperandString::GetData() const
{
  return m_data;
}

size_t TOperandString::GetDataLength() const
{
  return m_data_length;
}

void TOperandString::Print(OutStream &out) const
{
  if (m_data_length != 0) {
    out << '\"';
    PrintString(out, m_data, m_data_length);
    out << '\"';
  }
  else {
    out << "null";
  }
}

/////////////////////////////////////////////////////////////////////
// TOperandTimeStamp
/////////////////////////////////////////////////////////////////////

TOperandTimeStamp::TOperandTimeStamp(Transaction *t, TimeStamp stamp)
  : TOperand(t, TO_TimeStamp), m_stamp(stamp)
{}

TimeStamp TOperandTimeStamp::GetStamp() const
{
  return m_stamp;
}

void TOperandTimeStamp::Print(OutStream &out) const
{
  out << "timestamp";
}

/////////////////////////////////////////////////////////////////////
// TOperandBoolean
/////////////////////////////////////////////////////////////////////

TOperandBoolean::TOperandBoolean(Transaction *t, bool flag)
  : TOperand(t, TO_Boolean), m_flag(flag)
{}

bool TOperandBoolean::IsTrue() const
{
  return m_flag;
}

void TOperandBoolean::Print(OutStream &out) const
{
  out << (m_flag ? "true" : "false");
}

NAMESPACE_XGILL_END

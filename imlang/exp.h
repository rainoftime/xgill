
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

#pragma once

// common representation for program expressions.

#include "variable.h"
#include "visitor.h"
#include "opcode.h"

NAMESPACE_XGILL_BEGIN

// forward declarations.
class BlockId;
class Exp;
class Bit;

// program points

// program points are an index into some CFG block's list of points.
// 0 is reserved for an invalid/abort point; regular ordering starts at 1.
// for more details see block.h
typedef uint32_t PPoint;

// unique identifier for frames in the solver and checker. see solve/solver.h
// and check/frame.h
typedef uint32_t FrameId;

// callbacks which can be filled in for checking exp/bit simplification.
extern void (*g_callback_ExpSimplify)(Exp*, Exp*);
extern void (*g_callback_CvtSimplify)(Exp*, Bit*);
extern void (*g_callback_BitSimplify)(Bit*, Bit*);

// Exp class.

enum ExpKind {
  EK_Invalid = 0,
  EK_Empty = 1,

  // program lvalues. these can be produced by the frontend.
  EK_Var = 10,
  EK_Drf = 11,
  EK_Fld = 12,
  EK_Rfld = 13,
  EK_Index = 14,
  EK_String = 15,
  EK_VPtr = 16,

  // program rvalues. these can be produced by the frontend.
  EK_Int = 20,
  EK_Float = 21,
  EK_Unop = 22,
  EK_Binop = 23,

  // lvalue modifiers.
  EK_Clobber = 40,
  EK_Exit = 41,
  EK_Initial = 42,

  // rvalue modifiers.
  EK_Val = 30,
  EK_Guard = 31,
  EK_Frame = 32,

  // analysis values. these are internal to later analyses.
  EK_Bound = 50,
  EK_Terminate = 51,
};

class ExpEmpty;

class ExpVar;
class ExpDrf;
class ExpFld;
class ExpRfld;
class ExpIndex;
class ExpString;
class ExpVPtr;

class ExpInt;
class ExpFloat;
class ExpUnop;
class ExpBinop;

class ExpVal;
class ExpGuard;
class ExpFrame;

class ExpClobber;
class ExpExit;
class ExpInitial;

class ExpBound;
class ExpTerminate;

class Exp : public HashObject
{
 public:
  static int Compare(const Exp *exp0, const Exp *exp1);
  static Exp* Copy(const Exp *exp);
  static void Write(Buffer *buf, const Exp *exp);
  static Exp* Read(Buffer *buf);

  // the return type of these is just Exp because the kind can change
  // during simplification.

  static Exp* MakeEmpty();

  // expression constructors. the return type for expressions which might
  // be simplified during construction is left as 'Exp', and these should
  // be treated as arbitrary expressions.

  // lvalue constructors.
  static Exp* MakeVar(Variable *var);
  static Exp* MakeDrf(Exp *target);
  static Exp* MakeFld(Exp *target, Field *field);
  static Exp* MakeRfld(Exp *target, Field *field);
  static Exp* MakeIndex(Exp *target, Type *element_type, Exp *index);
  static Exp* MakeString(TypeArray *type, DataString *str);
  static Exp* MakeString(String *str);
  static Exp* MakeVPtr(Exp *target, uint32_t vtable_index);

  // combine construction of a variable and its expression.
  static Exp* MakeVar(BlockId *id, VarKind kind,
                      String *name, size_t index, String *source_name);

  // various ways of making integers.
  static ExpInt* MakeIntMpz(const mpz_t value,
                            size_t bits = 0, bool sign = true);
  static ExpInt* MakeIntStr(const char *value);
  static ExpInt* MakeInt(long value);

  // other rvalue constructors.
  static Exp* MakeFloat(const char *value);
  static Exp* MakeUnop(UnopKind unop_kind, Exp *op,
                       size_t bits = 0, bool sign = true);
  static Exp* MakeBinop(BinopKind binop_kind,
                        Exp *left_op, Exp *right_op,
                        Type *stride_type = NULL,
                        size_t bits = 0, bool sign = true);

  // lvalue modifier constructors.
  static ExpClobber* MakeClobber(Exp *callee, Exp *value_kind, Exp *overwrite,
                                 PPoint point, Location *location);
  static ExpExit* MakeExit(Exp *target, Exp *value_kind);
  static ExpInitial* MakeInitial(Exp *target, Exp *value_kind);

  // rvalue modifier constructors.
  static ExpVal* MakeVal(Exp *lval, Exp *value_kind, PPoint point,
                         bool relative);
  static ExpGuard* MakeGuard(PPoint point);
  static ExpFrame* MakeFrame(Exp *value, FrameId frame_id);

  // analysis value constructors.
  static Exp* MakeBound(BoundKind bound_kind, Exp *target, Type *stride_type);
  static Exp* MakeTerminate(Exp *target, Type *stride_type,
                            Exp *terminate_test, ExpInt *terminate_int);

  // for an lvalue, fills in all the subexpressions and remainders.
  // if either vector is NULL that vector is not computed. if both vectors
  // are non-NULL then they will have the same size and subexprs and
  // remainders at each index will correlate with one another.
  // gets a reference on each value added to remainders, but *NOT*
  // those added to subexprs. the subexprs will be filled in order
  // from the base variable downwards, i.e. the last entry in subexprs
  // will be value and the last entry in remainders will be empty.
  static void GetSubExprs(Exp *value,
                          Vector<Exp*> *subexprs,
                          Vector<Exp*> *remainders);

  // where subexpr is a subexpression of value, get the remainder.
  static Exp* GetSubExprRemainder(Exp *value, Exp *subexpr);

  // compose an lvalue with a relative offset, returning the result.
  // for GetSubExprs, subvalues[i] composes with remainders[i] into
  // the original value.
  static Exp* Compose(Exp *value, Exp *offset);

  // gets a reference on the bit where value is non-zero. this does NOT
  // consume a reference on value.
  static Bit* MakeNonZeroBit(Exp *value);

  // gets a reference on the bit comparing left_op with right_op.
  // combines MakeBinop and MakeNonZeroBit. if get_references is set then
  // does *not* consume references on left_op and right_op.
  static Bit* MakeCompareBit(BinopKind binop_kind,
                             Exp *left_op, Exp *right_op,
                             Type *stride_type = NULL,
                             bool get_references = false);

  // gets an explicit bound for the specified lvalue and stride type,
  // if possible, NULL otherwise.
  static Exp* GetExplicitBound(BoundKind bound_kind, Exp *target,
                               Type *stride_type);

 public:
  Exp(ExpKind kind, size_t bits = 0, bool sign = true)
    : m_kind(kind), m_bits(bits), m_sign(sign)
  {
    m_hash = Hash32(m_kind, m_bits + (size_t) m_sign);
  }

  // get the kind of this expression.
  ExpKind Kind() const { return m_kind; }

  // get the bit width and sign of this expression, it if is possible for
  // it to overflow. 0/true for expressions which can't overflow. the only
  // expressions which can overflow are a subset of the unops and binops.
  size_t Bits() const { return m_bits; }
  bool Sign() const { return m_sign; }

  DOWNCAST_TYPE(Exp, EK_, Empty)

  DOWNCAST_TYPE(Exp, EK_, Var)
  DOWNCAST_TYPE(Exp, EK_, Drf)
  DOWNCAST_TYPE(Exp, EK_, Fld)
  DOWNCAST_TYPE(Exp, EK_, Rfld)
  DOWNCAST_TYPE(Exp, EK_, Index)
  DOWNCAST_TYPE(Exp, EK_, String)
  DOWNCAST_TYPE(Exp, EK_, VPtr)

  DOWNCAST_TYPE(Exp, EK_, Int)
  DOWNCAST_TYPE(Exp, EK_, Float)
  DOWNCAST_TYPE(Exp, EK_, Unop)
  DOWNCAST_TYPE(Exp, EK_, Binop)

  DOWNCAST_TYPE(Exp, EK_, Clobber)
  DOWNCAST_TYPE(Exp, EK_, Exit)
  DOWNCAST_TYPE(Exp, EK_, Initial)

  DOWNCAST_TYPE(Exp, EK_, Val)
  DOWNCAST_TYPE(Exp, EK_, Guard)
  DOWNCAST_TYPE(Exp, EK_, Frame)

  DOWNCAST_TYPE(Exp, EK_, Bound)
  DOWNCAST_TYPE(Exp, EK_, Terminate)

  // whether this expression is an lvalue which can be assigned to and/or from
  // which other lvalues may be derived.
  bool IsLvalue() const
  {
    switch (m_kind) {
    case EK_Empty:
    case EK_Var:
    case EK_Drf:
    case EK_Fld:
    case EK_Rfld:
    case EK_Index:
    case EK_String:
    case EK_VPtr:
    case EK_Clobber:
    case EK_Exit:
    case EK_Initial:
      return true;
    // analysis expressions which behave like lvalues.
    case EK_Bound:
    case EK_Terminate:
      return true;
    default:
      return false;
    }
  }

  // whether this expression is an rvalue constructed from zero or more
  // lvalue expressions.
  bool IsRvalue() const
  {
    switch (m_kind) {
    case EK_Int:
    case EK_Float:
    case EK_Unop:
    case EK_Binop:
      return true;
    default:
      return false;
    }
  }

  // print this expression for the UI.
  virtual void PrintUI(OutStream &out, bool parens) const { out << this; }

  // as in PrintUI, except prints this as an rvalue, removing any leading
  // Drf or adding an '&' as appropriate.
  void PrintUIRval(OutStream &out, bool parens) const;

  // for an lvalue, get the declared type, if known. has the same restrictions
  // on use as Variable::GetType. note that if the lvalue is cast to some other
  // type this may be unrelated to the type with which the lvalue is used.
  virtual Type* GetType() const { return NULL; }

  // invoke the visitor or mapper on all or a portion of the component values
  // used to compute this value, according to the kind of visitor/mapper.
  virtual void DoVisit(ExpVisitor *visitor);
  virtual Exp* DoMap(ExpMapper *mapper);
  virtual void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);

  // for an lvalue, get the variable it is derived from, NULL if none exists.
  Variable* Root();

  // for an lvalue, get the clobbered value it is derived from, or NULL if
  // none exists.
  ExpClobber* ClobberRoot();

  // for an lvalue, return whether this value is derived from the empty value.
  bool IsRelative();

  // for an lvalue, get the base field, NULL if none exists. if the value
  // is relative, the base field is the field wrapping the empty inner value.
  Field* BaseField();

  // for an lvalue, get the number of contained Drf or Fld expressions.
  size_t DerefCount();
  size_t FieldCount();

  // get the number of non-constant terms appearing in an rvalue. if the same
  // term appears multiple times it will be counted repeatedly, e.g. the size
  // of (x + y) * (x + y) is 4.
  size_t TermCount();

  // whether the number of terms exceeds count. this is robust against
  // gigantic heavily shared expressions, on which TermCount might not
  // quickly terminate.
  bool TermCountExceeds(size_t count);

  // for an lvalue based on another target expression, get that target.
  virtual Exp* GetLvalTarget() const { return NULL; }

  // gets a reference on an exp equal to this one with the lvalue target
  // replaced by new_target. consumes a reference on new_target.
  virtual Exp* ReplaceLvalTarget(Exp *new_target) {
    Assert(false);
    return NULL;
  }

  // for expressions with stride types (index, some binop, bound, terminate),
  // is the specified type compatible with the stride type?
  virtual bool IsCompatibleStrideType(Type *type) const { return false; }

 protected:
  ExpKind m_kind;
  size_t m_bits;
  bool m_sign;

  Exp* BaseMap(Exp *value, ExpMapper *mapper)
  {
    return mapper->Map(value, this);
  }

  void BaseMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res)
  {
    this->IncRef();
    mapper->MultiMap(this, res);
  }

  static HashCons<Exp> g_table;
};

// Exp instance classes.

class ExpEmpty : public Exp
{
 public:
  // inherited methods
  void Print(OutStream &out) const;

 private:
  ExpEmpty();
  friend class Exp;
};

class ExpVar : public Exp
{
 public:
  // get the variable this expression represents.
  Variable* GetVariable() const { return m_var; }

  // inherited methods
  Type* GetType() const;
  void Print(OutStream &out) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Variable *m_var;

  ExpVar(Variable *var);
  friend class Exp;
};

class ExpDrf : public Exp
{
 public:
  // get the value this is dereferencing.
  Exp* GetTarget() const { return m_target; }

  // inherited methods
  Type* GetType() const;
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_target;

  ExpDrf(Exp *target);
  friend class Exp;
};

class ExpFld : public Exp
{
 public:
  // get the value this is accessing a field of.
  Exp* GetTarget() const { return m_target; }

  // get the field this is accessing.
  Field* GetField() const { return m_field; }

  // inherited methods
  Type* GetType() const;
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_target;
  Field *m_field;

  ExpFld(Exp *target, Field *field);
  friend class Exp;
};

class ExpRfld : public Exp
{
 public:
  // get the value this is reversing a field access for.
  Exp* GetTarget() const { return m_target; }

  // get the field this is reversing.
  Field* GetField() const { return m_field; }

  // inherited methods
  Type* GetType() const;
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_target;
  Field *m_field;

  ExpRfld(Exp *target, Field *field);
  friend class Exp;
};

class ExpIndex : public Exp
{
 public:
  // get the value this is accessing an index of.
  Exp* GetTarget() const { return m_target; }

  // get the element type and corresponding stride used for the index.
  Type* GetElementType() const { return m_element_type; }

  // get the array index being accessed.
  Exp* GetIndex() const { return m_index; }

  // inherited methods
  Type* GetType() const;
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  bool IsCompatibleStrideType(Type *type) const;
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_target;
  Type *m_element_type;
  Exp *m_index;

  ExpIndex(Exp *target, Type *element_type, Exp *index);
  friend class Exp;
};

class ExpString : public Exp
{
 public:
  // get the constant string this represents.
  DataString* GetString() const { return m_str; }

  // get the contents for a C-style string (non-unicode), NULL otherwise.
  const char* GetStringCStr() const;

  // inherited methods
  Type* GetType() const;
  void Print(OutStream &out) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  // the element type should be char or ushort.
  TypeArray *m_type;
  DataString *m_str;

  ExpString(TypeArray *type, DataString *str);
  friend class Exp;
};

class ExpVPtr : public Exp
{
 public:
  // get the base object this is a vtable entry for.
  Exp* GetTarget() const { return m_target; }

  // get the index into the vtable of this lvalue.
  uint32_t GetIndex() const { return m_vtable_index; }

  // inherited methods
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_target;
  uint32_t m_vtable_index;

  ExpVPtr(Exp *target, uint32_t vtable_index);
  friend class Exp;
};

class ExpInt : public Exp
{
 public:
  // gets the string representation of this value.
  const char* GetValue() const { return m_value; }

  // get the GMP integer representation of this value.
  // the result must already be initialized.
  void GetMpzValue(mpz_t res) const { mpz_set(res, m_mpz); }

  // fills value with the value of this value, returning true iff the
  // value could be represented in the long.
  bool GetInt(long *value) const {
    if (mpz_fits_slong_p(m_mpz)) {
      *value = mpz_get_si(m_mpz);
      return true;
    }
    return false;
  }

  // inherited methods
  void Print(OutStream &out) const;
  void Persist();
  void UnPersist();

 private:
  const char *m_value;

  // this encodes the same information as m_value.
  mpz_t m_mpz;

  ExpInt(const char *value);
  friend class Exp;
};

class ExpFloat : public Exp
{
 public:
  // get the string representation of this float.
  const char* GetValue() const { return m_value; }

  // inherited methods
  void Print(OutStream &out) const;
  void Persist();
  void UnPersist();

 private:
  const char *m_value;

  ExpFloat(const char *value);
  friend class Exp;
};

class ExpUnop : public Exp
{
 public:
  // get the kind of unop this is.
  UnopKind GetUnopKind() const { return m_unop_kind; }

  // get the operand of this unop.
  Exp *GetOperand() const { return m_op; }

  // inherited methods
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  UnopKind m_unop_kind;
  Exp *m_op;

  ExpUnop(UnopKind unop_kind, Exp *op, size_t bits, bool sign);
  friend class Exp;
};

class ExpBinop : public Exp
{
 public:
  // get the kind of binop this is.
  BinopKind GetBinopKind() const { return m_binop_kind; }

  // get the left/right operands of this binop.
  Exp* GetLeftOperand() const { return m_left_op; }
  Exp* GetRightOperand() const { return m_right_op; }

  // get the stride type, for a pointer arithmetic binop.
  Type* GetStrideType() const { return m_stride_type; }

  // if one of the operands of this binop is constant, set the value
  // parameter and return the other operand, otherwise return NULL.
  Exp* HasConstant(long *value) {
    if (ExpInt *nleft = m_left_op->IfInt()) {
      if (nleft->GetInt(value))
        return m_right_op;
    }
    if (ExpInt *nright = m_right_op->IfInt()) {
      if (nright->GetInt(value))
        return m_left_op;
    }
    return NULL;
  }

  // inherited methods
  bool IsCompatibleStrideType(Type *type) const;
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  BinopKind m_binop_kind;
  Exp *m_left_op;
  Exp *m_right_op;
  Type *m_stride_type;

  ExpBinop(BinopKind binop_kind,
           Exp *left_op, Exp *right_op, Type *stride_type,
           size_t bits, bool sign);
  friend class Exp;
};

class ExpClobber : public Exp
{
 public:
  // get the callee lvalue whose value at callee exit is indicated
  // by this expression. this is equivalent to EExit(target, value_kind)
  // within the callee's scope.
  Exp* GetCallee() const { return m_callee; }

  // get the kind of value being computed for the callee lvalue.
  Exp* GetValueKind() const { return m_value_kind; }

  // get the caller lvalue which was overwritten. this is the lvalue target,
  // which cannot be replaced with ReplaceLvalTarget.
  Exp* GetOverwrite() const { return m_overwrite; }

  // get the point where the callee was invoked.
  PPoint GetPoint() const { return m_point; }

  // get the location of the call/loop point, if known.
  Location* GetLocation() const { return m_location; }

  // inherited methods
  Type* GetType() const;
  Exp* GetLvalTarget() const;
  void Print(OutStream &out) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_callee;
  Exp *m_value_kind;
  Exp *m_overwrite;
  PPoint m_point;

  Location *m_location;

  ExpClobber(Exp *callee, Exp *value_kind, Exp *overwrite,
             PPoint point, Location *location);
  friend class Exp;
};

class ExpExit : public Exp
{
 public:
  // get the lvalue whose value at exit is indicated by this expression.
  Exp* GetTarget() const { return m_target; }

  // get the kind of value being computed at exit.
  Exp* GetValueKind() const { return m_value_kind; }

  // inherited methods
  Type* GetType() const;
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_target;
  Exp *m_value_kind;

  ExpExit(Exp *target, Exp *value_kind);
  friend class Exp;
};

class ExpInitial : public Exp
{
 public:
  // get the lvalue whose value at initial loop entry is indicated
  // by this expression.
  Exp* GetTarget() const { return m_target; }

  // get the kind of value being computed at loop entry.
  Exp* GetValueKind() const { return m_value_kind; }

  // inherited methods
  Type* GetType() const;
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_target;
  Exp *m_value_kind;

  ExpInitial(Exp *target, Exp *value_kind);
  friend class Exp;
};

// uninterpreted value of lval at a particular point. if necessary this
// can be expanded to the exact values it could represent.
class ExpVal : public Exp
{
 public:
  // get the lvalue being evaluated.
  Exp* GetLvalue() const { return m_lval; }

  // get the kind of value being computed.
  Exp* GetValueKind() const { return m_value_kind; }

  // get the point the value is taken at.
  PPoint GetPoint() const { return m_point; }

  // whether the lvalue is relative to this point, vs. at block entry.
  bool IsRelative() const { return m_relative; }

  // inherited methods
  Type* GetType() const;
  void Print(OutStream &out) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_lval;
  Exp *m_value_kind;
  PPoint m_point;
  bool m_relative;

  ExpVal(Exp *lval, Exp *value_kind, PPoint point, bool relative);
  friend class Exp;
};

// uninterpreted condition under which a point is executed.
// if necessary this can be expanded to the exact condition it represents.
class ExpGuard : public Exp
{
 public:
  // get the point whose execution condition is being guarded.
  PPoint GetPoint() const { return m_point; }

  // inherited methods
  void Print(OutStream &out) const;

 private:
  PPoint m_point;

  ExpGuard(PPoint point);
  friend class Exp;
};

// note: this expression cannot be serialized.
class ExpFrame : public Exp
{
 public:
  // get the expression this refers to in the target frame.
  Exp* GetValue() const { return m_value; }

  // get the target frame for this expression.
  FrameId GetFrameId() const { return m_frame_id; }

  // inherited methods
  void Print(OutStream &out) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_value;
  FrameId m_frame_id;

  ExpFrame(Exp *value, FrameId frame_id);
  friend class Exp;
};

class ExpBound : public Exp
{
 public:
  // get the kind of this bound.
  BoundKind GetBoundKind() const { return m_bound_kind; }

  // get the value this is a symbolic bound on. this may be NULL for special
  // bounds generated by the backend solver (the solver is also the only
  // place where Offset bounds are generated), in which case it indicates
  // the upper bound of the currently analyzed buffer.
  Exp* GetTarget() const { return m_target; }

  // get the stride type for this bound.
  Type* GetStrideType() const { return m_stride_type; }

  // inherited methods
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  bool IsCompatibleStrideType(Type *type) const;
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  BoundKind m_bound_kind;
  Exp *m_target;
  Type *m_stride_type;

  ExpBound(BoundKind bound_kind, Exp *target, Type *stride_type);
  friend class Exp;
};

class ExpTerminate : public Exp
{
 public:
  // get the value this indicates the position of a terminator on.
  Exp* GetTarget() const { return m_target; }

  // get the stride type for the terminator.
  Type* GetStrideType() const { return m_stride_type; }

  // get the relative lvalue whose equality with a constant indicates the
  // position of the terminator and value of this expression.
  Exp* GetTerminateTest() const { return m_terminate_test; }

  // get the constant expression the test is compared with.
  ExpInt* GetTerminateInt() const { return m_terminate_int; }

  // return whether this is a NULL terminator test.
  bool IsNullTerminate() const {
    long value;
    if (m_terminate_int->GetInt(&value) && value == 0) {
      if (m_terminate_test->IsEmpty())
        return true;
    }
    return false;
  }

  // inherited methods
  Exp* GetLvalTarget() const;
  Exp* ReplaceLvalTarget(Exp *new_target);
  bool IsCompatibleStrideType(Type *type) const;
  void DoVisit(ExpVisitor *visitor);
  Exp* DoMap(ExpMapper *mapper);
  void DoMultiMap(ExpMultiMapper *mapper, Vector<Exp*> *res);
  void Print(OutStream &out) const;
  void PrintUI(OutStream &out, bool parens) const;
  void DecMoveChildRefs(ORef ov, ORef nv);

 private:
  Exp *m_target;
  Type *m_stride_type;
  Exp *m_terminate_test;
  ExpInt *m_terminate_int;

  ExpTerminate(Exp *target, Type *stride_type,
               Exp *terminate_test, ExpInt *terminate_int);
  friend class Exp;
};

// ExpInfo expression pattern matching.

// quick access to information about a single expression,
// for pattern matching.
struct ExpInfo
{
  // basic expression info.
  Exp *exp;
  size_t bits;
  bool sign;

  // whether the expression is constant.
  bool has_value;
  mpz_t value;

  // whether the expression is a unop/binop.
  UnopKind u_kind;
  BinopKind b_kind;
  Type *b_stride_type;

  // element type for index expressions.
  Type *element_type;

  ExpInfo()
    : exp(NULL), bits(0), sign(false), has_value(false),
      u_kind(U_Invalid), b_kind(B_Invalid), b_stride_type(NULL),
      element_type(NULL)
  {
    mpz_init(value);
  }

  // copy constructor not allowed.
  ExpInfo(const ExpInfo &o) { Assert(false); }

  ~ExpInfo()
  {
    mpz_clear(value);
  }

  ExpInfo& operator = (const ExpInfo &o)
  {
    exp = o.exp;
    bits = o.bits;
    sign = o.sign;

    has_value = o.has_value;
    mpz_set(value, o.value);

    u_kind = o.u_kind;
    b_kind = o.b_kind;
    b_stride_type = o.b_stride_type;
    element_type = o.element_type;

    return *this;
  }

  void Swap(ExpInfo &o)
  {
    ExpInfo tmp;
    tmp = o;
    o = *this;
    *this = tmp;
  }

  void Set(Exp *_exp)
  {
    exp = _exp;
    bits = exp->Bits();
    sign = exp->Sign();

    if (ExpInt *nexp = exp->IfInt()) {
      has_value = true;
      nexp->GetMpzValue(value);
    }

    if (ExpUnop *nexp = exp->IfUnop())
      u_kind = nexp->GetUnopKind();

    if (ExpBinop *nexp = exp->IfBinop()) {
      b_kind = nexp->GetBinopKind();
      b_stride_type = nexp->GetStrideType();
    }

    if (ExpIndex *nexp = exp->IfIndex())
      element_type = nexp->GetElementType();
  }

  void SetChildren(ExpInfo &left_info, ExpInfo &right_info)
  {
    if (u_kind) {
      ExpUnop *nexp = exp->AsUnop();
      left_info.Set(nexp->GetOperand());
    }

    if (b_kind) {
      ExpBinop *nexp = exp->AsBinop();
      left_info.Set(nexp->GetLeftOperand());
      right_info.Set(nexp->GetRightOperand());
    }

    if (element_type) {
      ExpIndex *nexp = exp->AsIndex();
      left_info.Set(nexp->GetTarget());
      right_info.Set(nexp->GetIndex());
    }
  }
};

// information about a bounded tree of unop/binop expressions.
struct FullExpInfo
{
  ExpInfo i;
  ExpInfo li;
  ExpInfo ri;
  ExpInfo lli;
  ExpInfo lri;
  ExpInfo rli;
  ExpInfo rri;

  void Set(Exp *exp)
  {
    i.Set(exp);
    i.SetChildren(li, ri);
    li.SetChildren(lli, lri);
    ri.SetChildren(rli, rri);
  }
};

// fill info_array with all permutations of the commutative binops at exp or
// either of its (unary/binary) children. info_array must contain 8 elements,
// and info_count will be filled with how many are actually in use.
void FillExpInfo(Exp *exp, FullExpInfo *info_array, size_t *info_count);

// Exp utility functions.

// if exp is a Drf, get its target.
inline Exp* ExpDereference(Exp *exp)
{
  if (ExpDrf *nexp = exp->IfDrf())
    return nexp->GetTarget();
  return NULL;
}

// add exp to a multimap result vector.
inline void ExpAddResult(Exp *exp, Vector<Exp*> *res)
{
  if (!res->Contains(exp)) {
    exp->IncRef(res);
    res->PushBack(exp);
  }

  exp->DecRef();
}

// if the size of res exceeds the limit for mapper, clears it except for exp,
// returning true.
inline bool LimitRevertResult(ExpMultiMapper *mapper,
                              Vector<Exp*> *res, Exp *exp)
{
  size_t limit = mapper->ResultLimit();

  if (limit && res->Size() > limit) {
    DecRefVector<Exp>(*res, res);
    res->Clear();

    exp->IncRef();
    ExpAddResult(exp, res);
    return true;
  }

  return false;
}

// replace old_exp with new_exp within exp/bit.
Exp* ExpReplaceExp(Exp *exp, Exp *old_exp, Exp *new_exp);
Bit* BitReplaceExp(Bit *bit, Exp *old_exp, Exp *new_exp);

// if exp is a bound or terminate, get its target and the kind of bound/term.
// accounts for clobbered terminate values. gets references on bound/terminate
// but not on target.
inline void GetExpBoundTerminate(Exp *exp, Exp **target,
                                 ExpBound **bound, ExpTerminate **terminate)
{
  if (exp->IsBound()) {
    if (target)
      *target = exp->GetLvalTarget();
    if (bound)
      *bound = exp->ReplaceLvalTarget(NULL)->AsBound();
  }

  if (exp->IsTerminate()) {
    if (target)
      *target = exp->GetLvalTarget();
    if (terminate)
      *terminate = exp->ReplaceLvalTarget(NULL)->AsTerminate();
  }

  if (ExpClobber *nexp = exp->IfClobber()) {
    if (Exp *kind = nexp->GetValueKind()) {
      if (ExpTerminate *nkind = kind->IfTerminate()) {
        if (target)
          *target = nexp->GetLvalTarget();

        if (terminate) {
          nkind->IncRef();
          *terminate = nkind;
        }
      }
    }
  }
}

// if possible, get new_index such that new_index * stride_type equals
// index * index_type.
Exp* ScaleBoundIndex(Type *stride_type, Type *index_type, Exp *index);

// visitor which adds every lvalue expression to an external vector.
// checks for duplicates and adds a reference for each exp.
class LvalListVisitor : public ExpVisitor
{
 public:
  Vector<Exp*> *lval_list;

  LvalListVisitor(Vector<Exp*> *_lval_list)
    : ExpVisitor(VISK_Lval), lval_list(_lval_list)
  {}

  void Visit(Exp *exp)
  {
    if (!lval_list->Contains(exp)) {
      exp->IncRef(lval_list);
      lval_list->PushBack(exp);
    }
  }
};

NAMESPACE_XGILL_END

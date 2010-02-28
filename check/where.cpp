
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

#include "frame.h"
#include "where.h"
#include <infer/expand.h>
#include <imlang/loopsplit.h>
#include <imlang/storage.h>

NAMESPACE_XGILL_BEGIN

/////////////////////////////////////////////////////////////////////
// Where static
/////////////////////////////////////////////////////////////////////

int Where::PriorityCompare(Where *where0, Where *where1)
{
  WhereKind kind0 = where0->Kind();
  WhereKind kind1 = where1->Kind();

  bool drop0 = (kind0 == WK_None)
            && (where0->AsNone()->GetReportKind() == RK_None);
  bool drop1 = (kind1 == WK_None)
            && (where1->AsNone()->GetReportKind() == RK_None);

  // favor dropping over other propagation.
  if (drop0 && !drop1)
    return -1;
  if (!drop0 && drop1)
    return 1;

  // favor caller/callee/invariant over search termination.
  if (kind0 != WK_None && kind1 == WK_None)
    return -1;
  if (kind0 == WK_None && kind1 != WK_None)
    return 1;

  if (kind0 == WK_None && kind1 == WK_None)
    return 0;

  Bit *bit0 = where0->GetBit();
  Bit *bit1 = where1->GetBit();
  Assert(bit0 && bit1);

  // favor non-callee propagation.
  if (kind0 != WK_Postcondition && kind1 == WK_Postcondition)
    return -1;
  if (kind0 == WK_Postcondition && kind1 != WK_Postcondition)
    return 1;

  // favor callee propagation on later callees.
  if (kind0 == WK_Postcondition && kind1 == WK_Postcondition) {
    WherePostcondition *post0 = where0->AsPostcondition();
    WherePostcondition *post1 = where1->AsPostcondition();
    if (post0->GetPoint() > post1->GetPoint())
      return -1;
    if (post0->GetPoint() < post1->GetPoint())
      return 1;
  }

  // for loops, favor preconditions with loop invariant terms.
  if (kind0 == WK_Precondition) {
    WherePrecondition *pre0 = where0->AsPrecondition();
    if (pre0->IsIgnoreUnroll() &&
        (kind1 != WK_Precondition ||
         !where1->AsPrecondition()->IsIgnoreUnroll()))
      return -1;
  }
  if (kind1 == WK_Precondition) {
    WherePrecondition *pre1 = where1->AsPrecondition();
    if (pre1->IsIgnoreUnroll() &&
        (kind0 != WK_Precondition ||
         !where0->AsPrecondition()->IsIgnoreUnroll()))
      return 1;
  }

  // favor invariant propagation.
  if (kind0 == WK_Invariant && kind1 != WK_Invariant)
    return -1;
  if (kind0 != WK_Invariant && kind1 == WK_Invariant)
    return 1;

  // favor weaker bits over stronger ones. if there are bits b0 and b1
  // where b0 implies b1, use b1. for example:
  // b0: len + 1 <= ubound(buf)
  // b1: len <= ubound(buf)

  // if the two bits are equivalent, pick one canonically to use.
  if (bit0 == bit1)
    return 0;
  if (Solver::BitEquivalent(bit0, bit1))
    return (bit0->Hash() < bit1->Hash()) ? -1 : 1;

  // if one bit implies the other, pick the implied one (the weaker bit).
  if (Solver::BitImplies(bit1, bit0))
    return -1;
  if (Solver::BitImplies(bit0, bit1))
    return 1;

  // out of ideas, the directions are not comparable.
  return 0;
}

// mapper that replaces any ExpFrame expressions for a particular frame
// with the base expression in that ExpFrame.
class RemoveFrameMapper : public ExpMapper
{
public:
  FrameId frame;
  RemoveFrameMapper(FrameId _frame)
    : ExpMapper(VISK_All, WIDK_Drop), frame(_frame)
  {}

  Exp* Map(Exp *exp, Exp*)
  {
    if (ExpFrame *nexp = exp->IfFrame()) {
      if (nexp->GetFrameId() == frame) {
        Exp *new_exp = nexp->GetValue();
        new_exp->IncRef();
        exp->DecRef();
        return new_exp;
      }
    }

    return exp;
  }
};

// remove any uses of ExpVal or ExpFrame (for frame itself) from the
// list of input bits, storing the result in output.
static void RemoveValBits(CheckerFrame *frame, const GuardBitVector &input,
                          GuardBitVector *output)
{
  for (size_t iind = 0; iind < input.Size(); iind++) {
    const GuardBit &igb = input[iind];

    RemoveFrameMapper mapper(frame->Id());
    Bit *nbit = igb.bit->DoMap(&mapper);
    Assert(nbit);

    GuardBitVector remove_res;
    frame->Memory()->TranslateBit(TRK_RemoveVal, 0, nbit, &remove_res);
    nbit->DecRef();

    for (size_t rind = 0; rind < remove_res.Size(); rind++) {
      const GuardBit &rgb = remove_res[rind];
      rgb.IncRef();
      igb.guard->IncRef();

      Bit *new_guard = Bit::MakeAnd(igb.guard, rgb.guard);
      output->PushBack(GuardBit(rgb.bit, new_guard));
    }
  }

  output->SortCombine();
}

void Where::GetAssertBits(CheckerFrame *frame, PPoint point,
                          Bit *assert_cond, GuardBitVector *res)
{
  GuardBitVector base_res;
  frame->Memory()->TranslateBit(TRK_Point, point, assert_cond, &base_res);
  RemoveValBits(frame, base_res, res);
}

/////////////////////////////////////////////////////////////////////
// WhereNone
/////////////////////////////////////////////////////////////////////

WhereNone::WhereNone(ReportKind report_kind)
  : Where(WK_None, NULL), m_report_kind(report_kind)
{}

void WhereNone::Print(OutStream &out) const
{
  out << "Report: " << ReportString(m_report_kind);
}

void WhereNone::PrintUI(OutStream &out) const
{
  out << "Report: ";
  switch (m_report_kind) {
  case RK_Finished:
    out << "Finished exploration, no further dependents"; break;
  case RK_Timeout:
    out << "Timed out during exploration"; break;
  case RK_Recursion:
    out << "Recursion blocked, too many dependents at the same point"; break;
  case RK_Unexpected:
    out << "Unknown lvalue in goal, could not figure out dependent"; break;
  case RK_UnknownCSU:
    out << "Could not find base object for type invariant"; break;
  case RK_NoCallee:
    out << "Depends on a callee with no known implementation"; break;
  default:
    // should not get here with RK_None.
    Assert(false);
  }
}

/////////////////////////////////////////////////////////////////////
// WherePrecondition
/////////////////////////////////////////////////////////////////////

Where* WherePrecondition::Make(BlockMemory *mcfg, Bit *bit)
{
  Assert(bit);
  bool is_function = (mcfg->GetId()->Kind() == B_Function);

  Where *res = NULL;
  if (UseCallerBit(bit, is_function))
    res = new WherePrecondition(mcfg, bit);
  return res;
}

WherePrecondition::WherePrecondition(BlockMemory *mcfg, Bit *bit)
  : Where(WK_Precondition, bit),
    m_mcfg(mcfg), m_ignore_unroll(false)
{
  m_mcfg->IncRef(this);

  if (bit && m_mcfg->GetId()->Kind() == B_Loop) {
    // see if all terms in the bit are loop invariant.
    if (m_mcfg->IsBitPreserved(bit))
      m_ignore_unroll = true;
  }
}

WherePrecondition::~WherePrecondition()
{
  m_mcfg->DecRef(this);
}

void WherePrecondition::Print(OutStream &out) const
{
  out << "Precondition";
  if (m_bit)
    out << ": " << m_bit;
}

void WherePrecondition::PrintUI(OutStream &out) const
{
  if (m_mcfg->GetId()->Kind() == B_Loop)
    out << "LoopInvariant [" << m_mcfg->GetId()->LoopName() << "]";
  else
    out << "Precondition";

  if (m_bit) {
    out << " :: ";
    m_bit->PrintUI(out, true);
  }
}

void WherePrecondition::PrintHook(OutStream &out) const
{
  Variable *func_var = m_mcfg->GetId()->BaseVar();

  if (m_mcfg->GetId()->Kind() == B_Loop)
    out << m_mcfg->GetId()->Loop()->Value() << " ";
  else
    out << "pre ";

  out << func_var->GetName()->Value();
}

void WherePrecondition::GetCallerBits(CheckerFrame *caller_frame, PPoint point,
                                      Bit **base_bit, GuardBitVector *res)
{
  BlockMemory *caller_mcfg = caller_frame->Memory();
  TranslateKind kind = caller_frame->CalleeTranslateKind(point);

  bool unrolling = false;
  if (m_mcfg->GetId()->Kind() == B_Loop && caller_mcfg == m_mcfg)
    unrolling = true;

  ConvertCallsiteMapper mapper(caller_mcfg->GetCFG(), point, unrolling);
  Bit *caller_bit = m_bit->DoMap(&mapper);

  RemoveFrameMapper frame_mapper(caller_frame->Id());
  *base_bit = caller_bit->DoMap(&frame_mapper);

  caller_bit->DecRef();

  GuardBitVector base_res;
  caller_mcfg->TranslateBit(kind, point, m_bit, &base_res);
  RemoveValBits(caller_frame, base_res, res);
}

/////////////////////////////////////////////////////////////////////
// WherePostcondition
/////////////////////////////////////////////////////////////////////

Where* WherePostcondition::Make(CheckerFrame *frame, PPoint point, Bit *bit)
{
  Assert(bit);
  Bit *new_bit = TranslateCalleeBit(frame->Memory(), point, bit, frame->Id());

  Where *res = NULL;
  if (new_bit) {
    res = new WherePostcondition(frame, point, new_bit);
    new_bit->DecRef();
  }

  return res;
}

WherePostcondition::WherePostcondition(CheckerFrame *frame,
                                       PPoint point, Bit *bit)
  : Where(WK_Postcondition, bit), m_frame(frame), m_point(point)
{}

// mapper to replace all exit and clobber expressions with
// the corresponding Drf or other expression.
class ConvertExitClobberMapper : public ExpMapper
{
public:
  ConvertExitClobberMapper()
    : ExpMapper(VISK_All, WIDK_Drop)
  {}

  Exp* Map(Exp *value, Exp *old)
  {
    Exp *target = NULL;
    Exp *value_kind = NULL;

    if (ExpExit *nvalue = value->IfExit()) {
      target = nvalue->GetTarget();
      value_kind = nvalue->GetValueKind();
    }

    if (ExpClobber *nvalue = value->IfClobber()) {
      target = nvalue->GetOverwrite();
      value_kind = nvalue->GetValueKind();
    }

    if (!target)
      return value;

    // feed the targeted expression back into the mapper,
    // as this outer exp was treated as a leaf.
    Exp *new_target = target->DoMap(this);

    if (value_kind) {
      Exp *res = value_kind->ReplaceLvalTarget(new_target);
      value->DecRef();
      return res;
    }
    else {
      value->DecRef();
      return Exp::MakeDrf(new_target);
    }
  }
};

void WherePostcondition::Print(OutStream &out) const
{
  out << "Postcondition [" << m_point << "]";
  if (m_bit)
    out << ": " << m_bit;
}

void WherePostcondition::PrintUI(OutStream &out) const
{
  PEdge *edge = m_frame->CFG()->GetSingleOutgoingEdge(m_point);
  Location *loc = m_frame->CFG()->GetPointLocation(m_point);

  if (edge->IsLoop()) {
    const char *loop_name = edge->AsLoop()->GetLoopId()->LoopName();
    out << "LoopInvariant [" << loop_name << "]";
  }
  else {
    out << "Postcondition [";

    PEdgeCall *nedge = edge->AsCall();
    Exp *function = nedge->GetFunction();
    function->PrintUI(out, true);

    out << ":" << loc->Line() << "]";
  }

  if (m_bit) {
    ConvertExitClobberMapper mapper;
    Bit *new_bit = m_bit->DoMap(&mapper);

    out << " :: ";
    new_bit->PrintUI(out, true);
    new_bit->DecRef();
  }
}

void WherePostcondition::PrintHook(OutStream &out) const
{
  BlockId *id = m_frame->CFG()->GetId();
  Variable *func_var = id->BaseVar();

  PEdge *edge = m_frame->CFG()->GetSingleOutgoingEdge(m_point);

  if (PEdgeLoop *nedge = edge->IfLoop()) {
    out << nedge->GetLoopId()->Loop()->Value() << " "
        << func_var->GetName()->Value();
  }
  else {
    PEdgeCall *nedge = edge->AsCall();

    if (Variable *callee = nedge->GetDirectFunction()) {
      // direct call, just one hook function.
      out << "post " << callee->GetName()->Value();
    }
    else {
      // indirect call, one hook function for each callee.
      CallEdgeSet *callees = CalleeCache.Lookup(func_var);
      bool found_callee = false;

      if (callees) {
        for (size_t eind = 0; eind < callees->GetEdgeCount(); eind++) {
          const CallEdge &edge = callees->GetEdge(eind);
          if (edge.where.id == id && edge.where.point == m_point) {
            if (found_callee)
              out << "$";  // add the separator
            found_callee = true;

            out << "post " << edge.callee->GetName()->Value();
          }
        }
      }

      CalleeCache.Release(func_var);
    }
  }
}

void WherePostcondition::GetSkipLoopBits(Bit **base_bit, GuardBitVector *res)
{
  BlockMemory *mcfg = m_frame->Memory();

  ConvertExitClobberMapper mapper;
  *base_bit = m_bit->DoMap(&mapper);

  // TODO: is SkipClobber the best translation to do here?
  // there can't be any clobbers in m_bit, just exit expressions
  // which will be handled correctly by TranslateBit. needs cleanup.

  GuardBitVector base_res;
  mcfg->TranslateBit(TRK_SkipClobber, m_point, m_bit, &base_res);
  RemoveValBits(m_frame, base_res, res);
}

void WherePostcondition::GetCalleeBits(CheckerFrame *callee_frame,
                                       Bit **base_bit, GuardBitVector *res)
{
  BlockMemory *callee_mcfg = callee_frame->Memory();
  PPoint exit_point = callee_mcfg->GetCFG()->GetExitPoint();

  ConvertExitClobberMapper mapper;
  *base_bit = m_bit->DoMap(&mapper);

  GuardBitVector base_res;
  callee_mcfg->TranslateBit(TRK_Exit, exit_point, m_bit, &base_res);
  RemoveValBits(callee_frame, base_res, res);
}

/////////////////////////////////////////////////////////////////////
// WhereInvariant
/////////////////////////////////////////////////////////////////////

// visitor to check that an invariant contains only lvalues we can find
// the possible updates for.
class CheckInvariantVisitor : public ExpVisitor
{
public:
  bool exclude;
  CheckInvariantVisitor() : ExpVisitor(VISK_Lval), exclude(false) {}

  void Visit(Exp *exp)
  {
    if (exp->DerefCount() > 1)
      exclude = true;
  }
};

Where* WhereInvariant::Make(TypeCSU *csu, Exp *lval, Bit *bit)
{
  Assert(bit);
  Bit *new_bit;

  cout << "HUH " << csu << " " << lval << " " << bit << endl;

  if (csu) {
    Variable *this_var = Variable::Make(NULL, VK_This, NULL, 0, NULL);
    Exp *this_exp = Exp::MakeVar(this_var);
    Exp *this_drf = Exp::MakeDrf(this_exp);
    new_bit = TranslateHeapBit(lval, this_drf, false, bit);
    this_drf->DecRef();
  }
  else {
    new_bit = TranslateHeapBit(NULL, NULL, false, bit);
  }

  Where *res = NULL;
  if (new_bit) {
    // additionally visit it to make sure we can find all lvalues.
    CheckInvariantVisitor visitor;
    new_bit->DoVisit(&visitor);

    if (!visitor.exclude)
      res = new WhereInvariant(csu, new_bit);
    new_bit->DecRef();
  }

  return res;
}

WhereInvariant::WhereInvariant(TypeCSU *csu, Bit *bit)
  : Where(WK_Invariant, bit), m_csu(csu)
{
  if (m_csu)
    m_csu->IncRef(this);
}

WhereInvariant::~WhereInvariant()
{
  if (m_csu)
    m_csu->DecRef(this);
}

void WhereInvariant::Print(OutStream &out) const
{
  if (m_csu)
    out << "TypeInvariant [" << m_csu << "]: " << m_bit;
  else
    out << "GlobalInvariant: " << m_bit;
}

void WhereInvariant::PrintUI(OutStream &out) const
{
  if (m_csu)
    out << "TypeInvariant [" << m_csu << "]";
  else
    out << "GlobalInvariant";

  if (m_bit) {
    out << " :: ";
    m_bit->PrintUI(out, true);
  }
}

void WhereInvariant::PrintHook(OutStream &out) const
{
  // TODO: implement
  Assert(false);
}

void WhereInvariant::GetHeapBits(CheckerFrame *write_frame,
                                 Exp *write_csu, Exp *base_csu,
                                 Bit **base_bit, GuardBitVector *res)
{
  BlockMemory *mcfg = write_frame->Memory();

  Exp *old_lval = NULL;
  if (m_csu) {
    Variable *this_var = Variable::Make(NULL, VK_This, NULL, 0, NULL);
    Exp *old_this = Exp::MakeVar(this_var);
    old_lval = Exp::MakeDrf(old_this);
  }

  Bit *exit_bit = TranslateHeapBit(old_lval, write_csu, true, m_bit);
  Assert(exit_bit);

  if (old_lval)
    old_lval->DecRef();

  // TODO: using this to get the base bit for an invariant is fairly
  // hacked up, but for now we can't do this correctly as the base bit
  // needs to be relative to the CFG exit point, not the point where
  // any writes occur at. for now just get the displayable point for
  // the base CSU, and hope that means the same thing at exit as at
  // the point of the write.

  ConvertExitClobberMapper mapper;
  Bit *new_bit = m_bit->DoMap(&mapper);

  if (base_csu) {
    *base_bit = BitReplaceExp(new_bit, old_lval, base_csu);
    new_bit->DecRef();
  }
  else {
    *base_bit = new_bit;
  }

  GuardBitVector base_res;
  PPoint exit_point = mcfg->GetCFG()->GetExitPoint();
  mcfg->TranslateBit(TRK_Exit, exit_point, exit_bit, &base_res);

  exit_bit->DecRef();
  RemoveValBits(write_frame, base_res, res);
}

void WhereInvariant::AssertRecursive(CheckerFrame *frame, Exp *exp)
{
  if (m_csu == NULL)
    return;

  Exp *read_csu = GetWriteCSU(exp);

  if (read_csu == NULL)
    return;

  Variable *this_var = Variable::Make(NULL, VK_This, NULL, 0, NULL);
  Exp *this_exp = Exp::MakeVar(this_var);
  Exp *this_drf = Exp::MakeDrf(this_exp);

  Bit *entry_bit = TranslateHeapBit(this_drf, read_csu, false, m_bit);
  Assert(entry_bit);
  this_drf->DecRef();

  frame->AddAssert(entry_bit);
  entry_bit->DecRef();
}

Exp* WhereInvariant::GetWriteCSU(Exp *lval)
{
  // for now we can only handle invariants where the lvalues are field
  // offsets from the base CSU.
  while (ExpFld *nlval = lval->IfFld()) {
    if (nlval->GetField()->GetCSUType() == m_csu)
      return nlval->GetTarget();
    lval = nlval->GetTarget();
  }
  return NULL;
}

NAMESPACE_XGILL_END

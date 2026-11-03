// SPDX-License-Identifier: MIT
// Strict straight-line vertex/fragment AIR frontend. Unsupported constructs fail.
#include "AirNir.h"
#include "Library.h"
#include <llvm/Support/SHA256.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <map>
#include <vector>
using namespace llvm;
[[noreturn]] static void fail(const Twine &why) {
    errs() << "AIR refused: " << why << '\n';
    std::exit(4);
}
static StringRef str(Metadata *m) {
    auto *s = dyn_cast_or_null<MDString>(m);
    if (!s)
        fail("metadata string required");
    return s->getString();
}
static uint64_t integer(Metadata *m) {
    auto *v = mdconst::dyn_extract_or_null<ConstantInt>(m);
    if (!v || v->getBitWidth() > 64)
        fail("metadata integer required");
    return v->getZExtValue();
}
static MDNode *node(Metadata *m) {
    auto *n = dyn_cast_or_null<MDNode>(m);
    if (!n)
        fail("metadata node required");
    return n;
}
static Metadata *at(MDNode *n, unsigned index) {
    if (index >= n->getNumOperands())
        fail("metadata operand missing");
    return n->getOperand(index);
}
static int key(MDNode *n, StringRef name) {
    for (unsigned i = 0; i < n->getNumOperands(); i++)
        if (auto *s = dyn_cast_or_null<MDString>(n->getOperand(i)); s && s->getString() == name)
            return i;
    return -1;
}
static uint64_t after(MDNode *n, StringRef name) {
    int i = key(n, name);
    if (i < 0 || unsigned(i + 1) >= n->getNumOperands())
        fail("missing metadata " + name);
    return integer(n->getOperand(i + 1));
}
static unsigned width(Type *t) {
    if (t->isFloatTy())
        return 32;
    if (t->isIntegerTy(1))
        return 1;
    if (t->isIntegerTy(16) || t->isIntegerTy(32))
        return 32;
    fail("unsupported scalar type");
}
static unsigned lanes(Type *t) {
    if (auto *v = dyn_cast<FixedVectorType>(t)) {
        if (v->getNumElements() > 4)
            fail("vector wider than four");
        width(v->getElementType());
        return v->getNumElements();
    }
    if (auto *s = dyn_cast<StructType>(t)) {
        unsigned result = 0;
        for (Type *e : s->elements())
            result += lanes(e);
        if (result > 64)
            fail("aggregate too wide");
        return result;
    }
    width(t);
    return 1;
}
struct Pointer {
    unsigned binding, size;
    uint64_t offset;
};
using ValueLanes = std::vector<nir_def *>;
class Lower {
    air_nir *s;
    const DataLayout &dl;
    std::map<Value *, ValueLanes> values;
    std::map<Value *, Pointer> pointers;

  public:
    Lower(air_nir *shader, const DataLayout &layout) : s(shader), dl(layout) {}
    ValueLanes get(Value *v) {
        auto found = values.find(v);
        if (found != values.end())
            return found->second;
        unsigned n = lanes(v->getType());
        ValueLanes out(n, nullptr);
        if (isa<UndefValue>(v) || isa<PoisonValue>(v))
            return out;
        if (auto *f = dyn_cast<ConstantFP>(v); f && v->getType()->isFloatTy())
            out[0] = air_nir_bits(s, f->getValueAPF().bitcastToAPInt().getZExtValue(), 32);
        else if (auto *i = dyn_cast<ConstantInt>(v); i && v->getType()->isIntegerTy())
            out[0] = air_nir_bits(s, i->getZExtValue(), width(i->getType()));
        else if (auto *c = dyn_cast<Constant>(v); c && v->getType()->isVectorTy()) {
            for (unsigned j = 0; j < n; j++)
                out[j] = get(c->getAggregateElement(j))[0];
        } else if (auto *c = dyn_cast<Constant>(v); c && v->getType()->isStructTy()) {
            out.clear();
            for (unsigned j = 0; j < cast<StructType>(v->getType())->getNumElements(); j++) {
                auto data = get(c->getAggregateElement(j));
                out.insert(out.end(), data.begin(), data.end());
            }
        } else
            fail("unlowered value or unsupported constant");
        values[v] = out;
        return out;
    }
    nir_def *defined(nir_def *v) {
        if (!v)
            fail("undefined lane reaches arithmetic or output");
        return v;
    }
    void buffer(Argument *a, unsigned binding, unsigned size) { pointers[a] = {binding, size, 0}; }
    void vertex(Argument *a) {
        values[a] = {air_nir_vertex(s, a->getType()->getIntegerBitWidth())};
    }
    void attribute(Argument *a, unsigned location, unsigned count) {
        ValueLanes data(count);
        if (air_nir_attribute(s, location, count, data.data()))
            fail("invalid vertex attribute");
        values[a] = data;
    }
    void fragment_input(Argument *a, unsigned location, unsigned count) {
        ValueLanes data(count);
        if (air_nir_fragment_input(s, location, count, data.data())) fail("invalid fragment input");
        values[a] = data;
    }
    void lower(Instruction &i) {
        if (auto *g = dyn_cast<GetElementPtrInst>(&i)) {
            auto base = pointers.find(g->getPointerOperand());
            if (base == pointers.end())
                fail("GEP is not based on a declared buffer");
            APInt offset(64, 0);
            if (!g->accumulateConstantOffset(dl, offset) || offset.isNegative() ||
                offset.getZExtValue() > base->second.size)
                fail("nonconstant, negative or out-of-range GEP");
            auto p = base->second;
            p.offset += offset.getZExtValue();
            if (p.offset > p.size)
                fail("GEP outside buffer");
            pointers[&i] = p;
            return;
        }
        if (auto *l = dyn_cast<LoadInst>(&i)) {
            auto p = pointers.find(l->getPointerOperand());
            Type *t = l->getType();
            auto *vt = dyn_cast<FixedVectorType>(t);
            bool floatType = t->isFloatTy() || (vt && vt->getElementType()->isFloatTy());
            unsigned count = vt ? vt->getNumElements() : 1;
            unsigned bytes = count * 4;
            if (p == pointers.end() || !floatType || count < 1 || count > 4 || l->isVolatile() ||
                l->isAtomic() || p->second.size < bytes ||
                p->second.offset > p->second.size - bytes || p->second.offset % 4 ||
                l->getAlign().value() < 4)
                fail("unsupported or out-of-range buffer load");
            ValueLanes data;
            for (unsigned k = 0; k < count; k++)
                data.push_back(air_nir_load(s, p->second.binding, p->second.offset + k * 4));
            values[&i] = data;
            return;
        }
        if (auto *e = dyn_cast<InsertElementInst>(&i)) {
            auto v = get(e->getOperand(0));
            auto *index = dyn_cast<ConstantInt>(e->getOperand(2));
            if (!index || index->getZExtValue() >= v.size())
                fail("dynamic or out-of-range insert");
            v[index->getZExtValue()] = get(e->getOperand(1))[0];
            values[&i] = v;
            return;
        }
        if (auto *v = dyn_cast<ShuffleVectorInst>(&i)) {
            auto a = get(v->getOperand(0)), b = get(v->getOperand(1));
            a.insert(a.end(), b.begin(), b.end());
            ValueLanes out;
            for (int index : v->getShuffleMask())
                out.push_back(index < 0 ? nullptr : a.at(index));
            values[&i] = out;
            return;
        }
        if (auto *v = dyn_cast<InsertValueInst>(&i)) {
            auto *type = dyn_cast<StructType>(v->getType());
            if (!type || v->getNumIndices() != 1)
                fail("nested aggregate insertion");
            unsigned index = *v->idx_begin(), offset = 0;
            for (unsigned j = 0; j < index; j++)
                offset += lanes(type->getElementType(j));
            auto out = get(v->getAggregateOperand()), in = get(v->getInsertedValueOperand());
            std::copy(in.begin(), in.end(), out.begin() + offset);
            values[&i] = out;
            return;
        }
        if (auto *z = dyn_cast<ZExtInst>(&i)) {
            if (!z->getSrcTy()->isIntegerTy(16) || !z->getDestTy()->isIntegerTy(32))
                fail("unsupported zero extension");
            values[&i] = get(z->getOperand(0));
            return;
        }
        enum air_op op;
        unsigned count = 2;
        if (auto *cmp = dyn_cast<ICmpInst>(&i)) {
            if (cmp->getPredicate() == CmpInst::ICMP_EQ)
                op = AIR_EQ;
            else if (cmp->getPredicate() == CmpInst::ICMP_UGT)
                op = AIR_UGT;
            else
                fail("unsupported integer comparison");
        } else if (isa<SelectInst>(i)) {
            op = AIR_SELECT;
            count = 3;
        } else if (auto *c = dyn_cast<CallInst>(&i)) {
            auto *f = c->getCalledFunction();
            if (f && f->getName() == "air.fast_saturate.f32" && c->arg_size() == 1 &&
                c->getType()->isFloatTy() && c->getArgOperand(0)->getType()->isFloatTy() &&
                c->hasNoNaNs() && c->hasNoInfs() && c->hasNoSignedZeros()) {
                op = AIR_FSAT;
                count = 1;
            } else {
                if (!f || f->getName() != "air.fma.v2f32" || c->arg_size() != 3 ||
                    !c->getType()->isVectorTy() ||
                    cast<FixedVectorType>(c->getType())->getNumElements() != 2 ||
                    !cast<FixedVectorType>(c->getType())->getElementType()->isFloatTy())
                    fail("unsupported AIR call");
                op = c->hasNoNaNs() && c->hasNoInfs() && c->hasNoSignedZeros() ? AIR_FMA_FINITE_NSZ : AIR_FMA;
                count = 3;
            }
        } else if (i.getOpcode() == Instruction::Or && i.getType()->isIntegerTy(1))
            op = AIR_OR;
        else if (i.getOpcode() == Instruction::And &&
                 (i.getType()->isIntegerTy(16) || i.getType()->isIntegerTy(32)))
            op = AIR_AND;
        else if (i.getOpcode() == Instruction::FAdd)
            op = AIR_FADD;
        else if (i.getOpcode() == Instruction::FSub)
            op = AIR_FSUB;
        else if (i.getOpcode() == Instruction::FMul)
            op = AIR_FMUL;
        else if (i.getOpcode() == Instruction::FDiv) {
            if (!i.hasAllowReciprocal())
                fail("strict division lowering is not implemented");
            op = AIR_FDIV;
        } else
            fail(Twine("unsupported instruction ") + i.getOpcodeName());
        unsigned flags = 0;
        if (auto *fp = dyn_cast<FPMathOperator>(&i)) {
            if (fp->hasNoNaNs())
                flags |= AIR_NNAN;
            if (fp->hasNoInfs())
                flags |= AIR_NINF;
            if (fp->hasNoSignedZeros())
                flags |= AIR_NSZ;
            if (fp->hasAllowReciprocal())
                flags |= AIR_ARCP;
        }
        unsigned n = lanes(i.getType());
        ValueLanes args[3];
        for (unsigned j = 0; j < count; j++)
            args[j] = get(i.getOperand(j));
        ValueLanes out;
        for (unsigned j = 0; j < n; j++) {
            nir_def *in[3] = {nullptr, nullptr, nullptr};
            for (unsigned k = 0; k < count; k++) {
                if (args[k].size() != n && !(op == AIR_SELECT && k == 0 && args[k].size() == 1))
                    fail("operand shape mismatch");
                in[k] = defined(args[k][args[k].size() == 1 ? 0 : j]);
            }
            out.push_back(air_nir_op(s, op, flags, in[0], in[1], in[2]));
        }
        values[&i] = out;
    }
    void outputs(Value *v, Type *type, MDNode *metadata, bool fragment) {
        auto data = get(v);
        if (data.size() != lanes(type))
            fail("return aggregate shape");
        unsigned offset = 0, slot = 0;
        std::vector<Type *> elements;
        if (auto *st = dyn_cast<StructType>(type))
            elements.assign(st->element_begin(), st->element_end());
        else
            elements.push_back(type);
        for (Type *e : elements) {
            unsigned n = lanes(e);
            for (unsigned j = 0; j < n; j++)
                defined(data[offset + j]);
            unsigned target = fragment ? integer(at(node(at(metadata, slot)), 1)) : slot;
            if (fragment ? air_nir_color(s, slot, target, n, data.data() + offset)
                         : air_nir_store(s, slot, n, data.data() + offset))
                fail("invalid output");
            slot++;
            offset += n;
        }
    }
};
static Function *validate(Module &m, MDNode *&outputs, MDNode *&inputs, bool &fragment) {
    if (m.getTargetTriple().str() != "air64-apple-ios17.7.0" ||
        !m.getDataLayout().isLittleEndian() || m.getDataLayout().getPointerSizeInBits(2) != 64)
        fail("unsupported triple or data layout");
    if (verifyModule(m, &errs()))
        fail("LLVM verification failed");
    auto *version = m.getNamedMetadata("air.version");
    if (!version || version->getNumOperands() != 1)
        fail("AIR version missing");
    auto *v = version->getOperand(0);
    if (v->getNumOperands() != 3 || integer(at(v, 0)) != 2 || integer(at(v, 1)) != 6 ||
        integer(at(v, 2)) != 0)
        fail("unsupported AIR version");
    auto *options = m.getNamedMetadata("air.compile_options");
    bool denormDisable = false, fastMath = false;
    if (options) {
        for (MDNode *option : options->operands()) {
            if (option->getNumOperands() != 1)
                fail("compile option shape");
            StringRef name = str(at(option, 0));
            denormDisable |= name == "air.compile.denorms_disable";
            fastMath |= name == "air.compile.fast_math_enable";
        }
    }
    if (!denormDisable || !fastMath)
        fail("only explicit fast-math and denorm-disabled AIR is qualified");
    for (GlobalVariable &g : m.globals()) {
        auto *t = dyn_cast<ArrayType>(g.getValueType());
        if (g.getName() != "llvm.global_ctors" || !t || t->getNumElements() != 0 ||
            !g.hasInitializer() || !g.getInitializer()->isNullValue())
            fail("globals or function-constant initialization unsupported");
    }
    for (const char *kind : {"air.kernel", "air.visible"})
        if (m.getNamedMetadata(kind))
            fail("unsupported shader stage");
    auto *vertex = m.getNamedMetadata("air.vertex");
    auto *frag = m.getNamedMetadata("air.fragment");
    if (bool(vertex) == bool(frag))
        fail("exactly one vertex or fragment stage required");
    fragment = frag != nullptr;
    auto *stage = fragment ? frag : vertex;
    if (stage->getNumOperands() != 1)
        fail("exactly one entry required");
    auto *entry = stage->getOperand(0);
    if (entry->getNumOperands() != 3)
        fail("entry metadata shape");
    auto *vm = dyn_cast<ValueAsMetadata>(at(entry, 0));
    auto *f = vm ? dyn_cast<Function>(vm->getValue()) : nullptr;
    if (!f || f->isDeclaration() || f->size() != 1 || f->isVarArg())
        fail("single-block function required");
    for (Function &other : m)
        if (!other.isDeclaration() && &other != f)
            fail("multiple function bodies");
    outputs = node(at(entry, 1));
    inputs = node(at(entry, 2));
    std::vector<Type *> elements;
    if (auto *ret = dyn_cast<StructType>(f->getReturnType()))
        elements.assign(ret->element_begin(), ret->element_end());
    else
        elements.push_back(f->getReturnType());
    if (elements.empty() || elements.size() > 16 || elements.size() != outputs->getNumOperands())
        fail("return metadata mismatch");
    std::map<unsigned, bool> targets;
    for (unsigned j = 0; j < elements.size(); j++) {
        if (fragment) {
            auto *meta = node(at(outputs, j));
            Type *type = elements[j];
            auto *vector = dyn_cast<FixedVectorType>(type);
            if (elements.size() > 8 || (!type->isFloatTy() && (!vector ||
                !vector->getElementType()->isFloatTy() || vector->getNumElements() > 4)) ||
                str(at(meta, 0)) != "air.render_target" || meta->getNumOperands() < 3)
                fail("unsupported fragment output");
            uint64_t target = integer(at(meta, 1));
            if (target >= 8 || integer(at(meta, 2)) != 0 || targets.count(target))
                fail("fragment target range, dual source or duplicate");
            targets[target] = true;
            continue;
        }
        auto *v = dyn_cast<FixedVectorType>(elements[j]);
        auto *meta = node(at(outputs, j));
        if (!v || !v->getElementType()->isFloatTy() || v->getNumElements() > 4 ||
            str(at(meta, 0)) != (j ? "air.vertex_output" : "air.position") ||
            (!j && v->getNumElements() != 4))
            fail("unsupported vertex output");
        if (j && (meta->getNumOperands() < 2 || !str(at(meta, 1)).starts_with("generated(")))
            fail("varying identity missing");
    }
    if (inputs->getNumOperands() != f->arg_size())
        fail("input metadata mismatch");
    if (!isa<ReturnInst>(f->front().getTerminator()))
        fail("return required");
    return f;
}
struct Input {
    unsigned argument, index, components, size;
    std::string kind, bound_source;
    bool unused;
    std::string identity = "";
    unsigned alignment = 0;
};
static bool argument_offset(Value *v, Argument *a, const DataLayout &dl, uint64_t &offset) {
    if (v == a) {
        offset = 0;
        return true;
    }
    auto *g = dyn_cast<GEPOperator>(v);
    if (!g || !argument_offset(g->getPointerOperand(), a, dl, offset))
        return false;
    APInt delta(64, 0);
    if (!g->accumulateConstantOffset(dl, delta) || delta.isNegative() ||
        delta.getZExtValue() > 65536 || offset > 65536 - delta.getZExtValue())
        fail("unqualified static buffer extent");
    offset += delta.getZExtValue();
    return true;
}
static uint64_t static_extent(Argument *a) {
    uint64_t extent = 0;
    const DataLayout &dl = a->getParent()->getParent()->getDataLayout();
    for (Instruction &i : instructions(a->getParent()))
        if (auto *l = dyn_cast<LoadInst>(&i)) {
            uint64_t offset;
            if (!argument_offset(l->getPointerOperand(), a, dl, offset))
                continue;
            TypeSize bytes = dl.getTypeStoreSize(l->getType());
            if (bytes.isScalable() || bytes.getFixedValue() > 65536 ||
                offset > 65536 - bytes.getFixedValue())
                fail("unqualified static buffer extent");
            extent = std::max(extent, offset + bytes.getFixedValue());
        }
    return extent;
}
static uint64_t static_alignment(Argument *a) {
    uint64_t alignment = a->getParamAlign().valueOrOne().value();
    const DataLayout &dl = a->getParent()->getParent()->getDataLayout();
    for (Instruction &i : instructions(a->getParent()))
        if (auto *l = dyn_cast<LoadInst>(&i)) {
            uint64_t offset;
            if (!argument_offset(l->getPointerOperand(), a, dl, offset))
                continue;
            uint64_t required = l->getAlign().value();
            if (offset % required)
                fail("unsupported buffer base alignment residue");
            alignment = std::max(alignment, required);
        }
    return alignment;
}
static std::vector<Input> parse_inputs(Function *f, MDNode *inputs, bool fragment) {
    std::vector<Input> schema;
    std::map<unsigned, bool> bindings, locations;
    unsigned vertices = 0, attributes = 0, varyings = 0;
    std::map<std::string, bool> identities;
    for (unsigned j = 0; j < f->arg_size(); j++) {
        auto *a = f->getArg(j);
        auto *meta = node(at(inputs, j));
        if (meta->getNumOperands() < 2 || integer(at(meta, 0)) != j)
            fail("input index");
        StringRef kind = str(at(meta, 1));
        Input in = {j, 0, 0, 0, kind.str(), "", key(meta, "air.arg_unused") >= 0};
        if (in.unused && !a->use_empty())
            fail("unused argument metadata contradicts body");
        if (!fragment && kind == "air.vertex_id") {
            if (!(a->getType()->isIntegerTy(16) || a->getType()->isIntegerTy(32)) || ++vertices > 1)
                fail("vertex ID type/count");
            in.components = 1;
        } else if (!fragment && kind == "air.vertex_input") {
            auto *type = dyn_cast<FixedVectorType>(a->getType());
            if (!type || !type->getElementType()->isFloatTy() || type->getNumElements() > 4)
                fail("unsupported vertex attribute type");
            uint64_t location = after(meta, "air.location_index");
            int loc = key(meta, "air.location_index");
            if (location >= 16 || integer(at(meta, loc + 2)) != 1 || locations.count(location))
                fail("vertex attribute location/range overlap");
            locations[location] = true;
            in.index = location;
            in.components = type->getNumElements();
            attributes++;
        } else if (fragment && kind == "air.position") {
            auto *type = dyn_cast<FixedVectorType>(a->getType());
            if (!in.unused || !type || !type->getElementType()->isFloatTy() || type->getNumElements() != 4 ||
                key(meta, "air.center") < 0 || key(meta, "air.no_perspective") < 0 ||
                key(meta, "air.perspective") >= 0 || key(meta, "air.centroid") >= 0 || key(meta, "air.sample") >= 0)
                fail("only unused center fragment position is qualified");
            in.components = 4;
        } else if (fragment && kind == "air.fragment_input") {
            Type *type = a->getType();
            if (!(type->isFloatTy() || isa<FixedVectorType>(type)) || !type->getScalarType()->isFloatTy() ||
                lanes(type) > 4 || varyings >= 16 || meta->getNumOperands() < 3 ||
                key(meta, "air.center") < 0 || key(meta, "air.perspective") < 0 ||
                key(meta, "air.no_perspective") >= 0 || key(meta, "air.centroid") >= 0 ||
                key(meta, "air.sample") >= 0 || key(meta, "air.flat") >= 0)
                fail("unsupported fragment interpolation/type");
            in.identity = str(at(meta, 2)).str();
            if (!StringRef(in.identity).starts_with("generated(") || !StringRef(in.identity).ends_with(")") || in.identity.size() <= 11 || identities.count(in.identity))
                fail("fragment varying identity missing or duplicate");
            identities[in.identity] = true;
            in.index = varyings++;
            in.components = lanes(type);
        } else if (kind == "air.buffer") {
            uint64_t size, binding = after(meta, "air.location_index");
            if (key(meta, "air.buffer_size") >= 0) {
                size = after(meta, "air.buffer_size");
                in.bound_source = "air.buffer_size";
            } else {
                size = std::max(after(meta, "air.arg_type_size"), static_extent(a));
                size = std::max(size, a->getDereferenceableBytes());
                in.bound_source = "type-and-static-access-span";
            }
            int loc = key(meta, "air.location_index");
            uint64_t alignment = static_alignment(a);
            if (key(meta, "air.arg_type_align_size") >= 0)
                alignment = std::max(alignment, after(meta, "air.arg_type_align_size"));
            if (!alignment || (alignment & (alignment - 1)) || alignment > 4096)
                fail("unsupported constant buffer alignment");
            in.alignment = alignment;
            if (!a->getType()->isPointerTy() || a->getType()->getPointerAddressSpace() != 2 ||
                after(meta, "air.address_space") != 2 || key(meta, "air.read") < 0 ||
                key(meta, "air.write") >= 0 || (!in.unused && !a->onlyReadsMemory()) || !size ||
                size > 65536 || binding >= 16 || bindings.count(binding) ||
                integer(at(meta, loc + 2)) != 1)
                fail("constant buffer contract");
            bindings[binding] = true;
            in.index = binding;
            in.size = size;
        } else
            fail("unsupported input kind");
        schema.push_back(in);
    }
    if (!fragment && !vertices && !attributes)
        fail("vertex ID or attributes required");
    return schema;
}
static void interface_file(Function *f, MDNode *outputs, const std::vector<Input> &schema,
                           StringRef prefix, bool fragment) {
    json::Array ins, outs;
    for (const Input &in : schema) {
        json::Object item{{"argument", in.argument}, {"kind", in.kind}, {"index", in.index},
            {"components", in.components}, {"minimum_bytes", in.size}, {"minimum_alignment", in.alignment},
            {"bound_source", in.bound_source}, {"unused", in.unused}};
        if (in.kind == "air.fragment_input") {
            item["identity"] = in.identity;
            item["interpolation"] = "perspective";
            item["sampling"] = "center";
        }
        ins.push_back(std::move(item));
    }
    Type *r = f->getReturnType();
    auto *s = dyn_cast<StructType>(r);
    for (unsigned i = 0; i < outputs->getNumOperands(); i++) {
        Type *type = s ? s->getElementType(i) : r;
        auto *meta = node(at(outputs, i));
        json::Object output{{"slot", i}, {"components", lanes(type)},
                            {"identity", fragment ? "air.render_target" : i ? str(at(meta, 1)).str() : "air.position"}};
        if (fragment) {
            output["target"] = integer(at(meta, 1));
            output["blend_index"] = integer(at(meta, 2));
        }
        outs.push_back(std::move(output));
    }
    std::error_code ec;
    raw_fd_ostream out(prefix.str() + ".interface.json", ec);
    if (ec)
        fail("interface output open");
    out << formatv("{0:2}", json::Value(json::Object{{"function", f->getName().str()},
                                                     {"stage", fragment ? "fragment" : "vertex"},
                                                     {"inputs", std::move(ins)},
                                                     {"outputs", std::move(outs)},
                                                     {"runtime_resource_layout_qualified", false}}))
        << "\n";
}
static void oracle(Module &m, Function *f, StringRef path) {
    // Test oracle only: retarget a clone of the original IR and replace the
    // explicitly supported AIR FMA with LLVM's fused operation.
    for (Instruction &i : instructions(f))
        if (auto *c = dyn_cast<CallInst>(&i)) {
            auto *callee = c->getCalledFunction();
            if (!callee || callee->getName() != "air.fma.v2f32")
                fail("oracle unknown call");
            c->setCalledFunction(
                Intrinsic::getOrInsertDeclaration(&m, Intrinsic::fma, {c->getType()}));
        }
    auto &ctx = m.getContext();
    IRBuilder<> b(ctx);
    Type *ptr = PointerType::getUnqual(ctx), *i16 = Type::getInt16Ty(ctx);
    bool vertexOnly = f->arg_size() == 1 && f->getArg(0)->getType() == i16;
    if (!vertexOnly && (f->arg_size() != 2 || !f->getArg(0)->getType()->isPointerTy() ||
                        f->getArg(1)->getType() != i16))
        fail("oracle ABI requires optional buffer0 and ushort vertex");
    auto *wrapper = Function::Create(FunctionType::get(b.getVoidTy(), {ptr, i16, ptr}, false),
                                     GlobalValue::ExternalLinkage, "air_test_oracle", m);
    b.SetInsertPoint(BasicBlock::Create(ctx, "entry", wrapper));
    Value *r =
        vertexOnly
            ? b.CreateCall(f, {wrapper->getArg(1)})
            : b.CreateCall(f, {b.CreateAddrSpaceCast(wrapper->getArg(0), f->getArg(0)->getType()),
                               wrapper->getArg(1)});
    auto *rt = dyn_cast<StructType>(f->getReturnType());
    unsigned slots = rt ? rt->getNumElements() : 1;
    for (unsigned j = 0; j < slots; j++) {
        Value *vector = rt ? b.CreateExtractValue(r, j) : r;
        unsigned n = cast<FixedVectorType>(vector->getType())->getNumElements();
        for (unsigned k = 0; k < n; k++)
            b.CreateStore(b.CreateExtractElement(vector, k),
                          b.CreateGEP(b.getFloatTy(), wrapper->getArg(2), b.getInt32(j * 4 + k)));
    }
    b.CreateRetVoid();
    m.setTargetTriple(Triple(sys::getDefaultTargetTriple()));
    if (verifyModule(m, &errs()))
        fail("oracle LLVM verification");
    std::error_code ec;
    raw_fd_ostream stream(path, ec);
    if (ec)
        fail("oracle output open");
    WriteBitcodeToFile(m, stream);
}
static void resource_oracle(Module &m, Function *f, const std::vector<Input> &schema,
                            StringRef path) {
    // Host numerical reference only. Clamp finite values through LLVM's
    // min/max intrinsics; interpolation itself is supplied, never rasterized.
    std::vector<CallInst *> calls;
    for (Instruction &i : instructions(f))
        if (auto *c = dyn_cast<CallInst>(&i)) calls.push_back(c);
    for (CallInst *c : calls) {
        auto *callee = c->getCalledFunction();
        if (callee && callee->getName() == "air.fma.v2f32") {
            c->setCalledFunction(Intrinsic::getOrInsertDeclaration(&m, Intrinsic::fma, {c->getType()}));
        } else if (callee && callee->getName() == "air.fast_saturate.f32" && c->arg_size() == 1 &&
            c->getType()->isFloatTy() && c->hasNoNaNs() && c->hasNoInfs() && c->hasNoSignedZeros()) {
            IRBuilder<> builder(c);
            Value *low = builder.CreateBinaryIntrinsic(Intrinsic::maxnum, c->getArgOperand(0), ConstantFP::get(c->getType(), 0.0));
            Value *high = builder.CreateBinaryIntrinsic(Intrinsic::minnum, low, ConstantFP::get(c->getType(), 1.0));
            c->replaceAllUsesWith(high);
            c->eraseFromParent();
        } else fail("resource oracle unknown call");
    }
    // Preserve the frontend's separate-rounding policy in the host reference.
    for (Instruction &i : instructions(f))
        if (isa<FPMathOperator>(i)) i.copyFastMathFlags(FastMathFlags());
    for (const char *name : {"unsafe-fp-math", "approx-func-fp-math", "no-infs-fp-math",
                             "no-nans-fp-math", "no-signed-zeros-fp-math"})
        f->removeFnAttr(name);
    auto &ctx = m.getContext();
    IRBuilder<> b(ctx);
    Type *ptr = PointerType::getUnqual(ctx);
    auto *wrapper =
        Function::Create(FunctionType::get(b.getVoidTy(), {ptr, ptr, b.getInt32Ty(), ptr}, false),
                         GlobalValue::ExternalLinkage, "air_test_resource_oracle", m);
    b.SetInsertPoint(BasicBlock::Create(ctx, "entry", wrapper));
    std::vector<Value *> args;
    for (const Input &in : schema) {
        Type *type = f->getArg(in.argument)->getType();
        Value *value;
        if (in.kind == "air.vertex_id")
            value = b.CreateIntCast(wrapper->getArg(2), type, false);
        else if (in.kind == "air.position")
            value = Constant::getNullValue(type); // Validated unused float4 only.
        else if (in.kind == "air.buffer") {
            Value *address = b.CreateGEP(ptr, wrapper->getArg(1), b.getInt32(in.index));
            value = b.CreateAddrSpaceCast(b.CreateLoad(ptr, address), type);
        } else {
            value = PoisonValue::get(type);
            for (unsigned k = 0; k < in.components; k++) {
                Value *address =
                    b.CreateGEP(b.getFloatTy(), wrapper->getArg(0), b.getInt32(in.index * 4 + k));
                Value *component = b.CreateLoad(b.getFloatTy(), address);
                value = type->isFloatTy() ? component : b.CreateInsertElement(value, component, k);
            }
        }
        args.push_back(value);
    }
    Value *r = b.CreateCall(f, args);
    auto *rt = dyn_cast<StructType>(f->getReturnType());
    unsigned slots = rt ? rt->getNumElements() : 1;
    for (unsigned j = 0; j < slots; j++) {
        Value *vector = rt ? b.CreateExtractValue(r, j) : r;
        unsigned count = lanes(vector->getType());
        for (unsigned k = 0; k < count; k++)
            b.CreateStore(vector->getType()->isVectorTy() ? b.CreateExtractElement(vector, k) : vector,
                          b.CreateGEP(b.getFloatTy(), wrapper->getArg(3), b.getInt32(j * 4 + k)));
    }
    b.CreateRetVoid();
    m.setTargetTriple(Triple(sys::getDefaultTargetTriple()));
    if (verifyModule(m, &errs()))
        fail("resource oracle LLVM verification");
    std::error_code ec;
    raw_fd_ostream out(path, ec);
    if (ec)
        fail("resource oracle output open");
    WriteBitcodeToFile(m, out);
}
static int library_sha256(const uint8_t *data, size_t bytes, uint8_t out[32], void *) {
    auto digest = SHA256::hash(ArrayRef<uint8_t>(data, bytes));
    for (unsigned i = 0; i < 32; i++) out[i] = digest[i];
    return 0;
}
int main(int argc, char **argv) {
    bool library_input = argc == 6 && !strcmp(argv[4], "--compile-library");
    if (argc != 5 && argc != 7 && !library_input) {
        errs() << "usage: air_nir INPUT.bc CHIP_ID OUTPUT_PREFIX --compile [--test-evaluate "
                  "CASES.bin]\n"
                  "       air_nir INPUT.metallib CHIP_ID OUTPUT_PREFIX --compile-library INDEX\n"
                  "       air_nir INPUT.bc CHIP_ID OUTPUT.bc --test-oracle\n"
                  "       air_nir INPUT.bc CHIP_ID OUTPUT_PREFIX --compile --test-resources "
                  "CASES.bin\n"
                  "       air_nir INPUT.bc CHIP_ID OUTPUT.bc --test-resource-oracle\n";
        return 2;
    }
    LLVMContext ctx;
    auto buffer = MemoryBuffer::getFile(argv[1]);
    if (!buffer)
        fail("input read");
    if ((*buffer)->getBufferSize() > (library_input ? AIR_LIBRARY_MAX_BYTES : 16u * 1024u * 1024u))
        fail("input too large");
    MemoryBufferRef bitcode = (*buffer)->getMemBufferRef();
    struct air_library_function library_function = {};
    if (library_input) {
        if (!argv[5][0]) fail("library function index");
        unsigned index = 0;
        for (const char *p = argv[5]; *p; ++p) {
            if (*p < '0' || *p > '9' || index > AIR_LIBRARY_MAX_FUNCTIONS / 10)
                fail("library function index");
            index = index * 10 + unsigned(*p - '0');
            if (index >= AIR_LIBRARY_MAX_FUNCTIONS) fail("library function index");
        }
        struct air_library_view view;
        uint32_t error = air_library_open(reinterpret_cast<const uint8_t *>(bitcode.getBufferStart()),
            bitcode.getBufferSize(), library_sha256, nullptr, &view);
        if (!error) error = air_library_get(&view, index, &library_function);
        if (error) fail("library container status " + Twine(error));
        if (library_function.bitcode_bytes > 16u * 1024u * 1024u) fail("module too large");
        bitcode = MemoryBufferRef(StringRef(reinterpret_cast<const char *>(library_function.bitcode),
            library_function.bitcode_bytes), bitcode.getBufferIdentifier());
    }
    auto parsed = parseBitcodeFile(bitcode, ctx);
    if (!parsed) {
        consumeError(parsed.takeError());
        fail("bitcode parse");
    }
    auto m = std::move(*parsed);
    MDNode *outputs, *inputs;
    bool fragment;
    Function *f = validate(*m, outputs, inputs, fragment);
    if (library_input && (f->getName() != StringRef(reinterpret_cast<const char *>(library_function.name),
        library_function.name_bytes) || library_function.stage != unsigned(fragment)))
        fail("library record contradicts actual AIR entry");
    auto schema = parse_inputs(f, inputs, fragment);
    if (!strcmp(argv[4], "--test-oracle") && argc == 5) {
        oracle(*m, f, argv[3]);
        return 0;
    }
    if (!strcmp(argv[4], "--test-resource-oracle") && argc == 5) {
        resource_oracle(*m, f, schema, argv[3]);
        return 0;
    }
    bool resources = argc == 7 && !strcmp(argv[5], "--test-resources");
    bool testInt32 = argc == 7 && (!strcmp(argv[5], "--test-evaluate-int32") || resources);
    if ((!library_input && strcmp(argv[4], "--compile")) ||
        (argc == 7 && strcmp(argv[5], "--test-evaluate") && !testInt32))
        return 2;
    char *end = nullptr;
    uint64_t chip = strtoull(argv[2], &end, 0);
    if (!end || *end || !chip)
        fail("chip ID");
    air_nir *s = air_nir_create_stage(chip, f->getName().str().c_str(), fragment ? AIR_FRAGMENT : AIR_VERTEX);
    if (!s)
        fail("target or mandatory asm roundtrip");
    Lower lower(s, m->getDataLayout());
    for (const Input &in : schema) {
        auto *a = f->getArg(in.argument);
        if (in.unused)
            continue;
        if (in.kind == "air.position")
            continue; // Validated unused builtin: no fabricated position input.
        if (in.kind == "air.vertex_id")
            lower.vertex(a);
        else if (in.kind == "air.fragment_input")
            lower.fragment_input(a, in.index, in.components);
        else if (in.kind == "air.vertex_input")
            lower.attribute(a, in.index, in.components);
        else
            lower.buffer(a, in.index, in.size);
    }
    for (Instruction &i : f->front()) {
        if (auto *r = dyn_cast<ReturnInst>(&i))
            lower.outputs(r->getReturnValue(), f->getReturnType(), outputs, fragment);
        else
            lower.lower(i);
    }
    interface_file(f, outputs, schema, argv[3], fragment);
    if (testInt32 && air_nir_lower_integer_ops(s))
        fail("int64 arithmetic did not lower completely");
    if (resources) {
        std::ifstream input(argv[6], std::ios::binary);
        std::ofstream output(std::string(argv[3]) + ".eval", std::ios::binary);
        if (!input || !output)
            fail("resource test IO");
        unsigned count = 0;
        for (;;) {
            uint32_t vertex;
            input.read(reinterpret_cast<char *>(&vertex), 4);
            if (!input.gcount() && input.eof())
                break;
            if (input.gcount() != 4 || ++count > 65536)
                fail("resource case framing/bound");
            uint32_t attributes[16][4];
            input.read(reinterpret_cast<char *>(attributes), sizeof(attributes));
            if (input.gcount() != sizeof(attributes))
                fail("resource attribute framing");
            std::vector<uint8_t> data[16];
            struct air_nir_test_buffer buffers[16] = {};
            for (const Input &in : schema)
                if (in.kind == "air.buffer") {
                    data[in.index].resize(in.size);
                    input.read(reinterpret_cast<char *>(data[in.index].data()), in.size);
                    if (unsigned(input.gcount()) != in.size)
                        fail("resource buffer framing");
                    buffers[in.index] = {data[in.index].data(), in.size};
                }
            float out[64] = {};
            if (air_nir_test_eval_resources(s, vertex, attributes, buffers, out))
                fail("resource NIR evaluation");
            output.write(reinterpret_cast<char *>(out),
                         outputs->getNumOperands() * 4 * sizeof(float));
        }
        if (!count || !output)
            fail("resource evaluation output");
    } else if (argc == 7) {
        unsigned size = 0, buffers = 0;
        for (const Input &in : schema) {
            if (in.kind == "air.vertex_input")
                fail("old test ABI has no attributes");
            if (in.kind == "air.buffer") {
                if (++buffers > 1 || in.index != 0)
                    fail("old test ABI requires optional binding0");
                size = in.size;
            }
        }
        std::ifstream input(argv[6], std::ios::binary);
        std::ofstream output(std::string(argv[3]) + ".eval", std::ios::binary);
        if (!input || !output)
            fail("test IO");
        unsigned count = 0;
        for (;;) {
            uint32_t vertex;
            input.read(reinterpret_cast<char *>(&vertex), 4);
            if (input.gcount() == 0 && input.eof())
                break;
            if (input.gcount() != 4 || ++count > 65536)
                fail("test case framing/bound");
            std::vector<uint8_t> data(size);
            if (size)
                input.read(reinterpret_cast<char *>(data.data()), size);
            float out[64] = {};
            if ((size && unsigned(input.gcount()) != size) ||
                air_nir_test_eval(s, vertex, data.data(), size, out))
                fail("NIR constant evaluation");
            output.write(reinterpret_cast<char *>(out),
                         outputs->getNumOperands() * 4 * sizeof(float));
        }
        if (!count || !output)
            fail("test evaluation output");
    }
    int result = air_nir_compile(s, argv[3]);
    air_nir_destroy(s);
    return result ? 5 : 0;
}

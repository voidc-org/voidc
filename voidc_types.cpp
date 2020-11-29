//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_types.h"


#include <cassert>

#include <llvm/IR/DerivedTypes.h>

#include <llvm-c/Core.h>


//---------------------------------------------------------------------
v_type_t::~v_type_t() {}


//---------------------------------------------------------------------
LLVMTypeRef
v_type_void_t::obtain_llvm_type(void) const
{
    return  LLVMVoidTypeInContext(context.llvm_ctx);
}

LLVMTypeRef
v_type_f16_t::obtain_llvm_type(void) const
{
    return  LLVMHalfTypeInContext(context.llvm_ctx);
}

LLVMTypeRef
v_type_f32_t::obtain_llvm_type(void) const
{
    return  LLVMFloatTypeInContext(context.llvm_ctx);
}

LLVMTypeRef
v_type_f64_t::obtain_llvm_type(void) const
{
    return  LLVMDoubleTypeInContext(context.llvm_ctx);
}

LLVMTypeRef
v_type_f128_t::obtain_llvm_type(void) const
{
    return  LLVMFP128TypeInContext(context.llvm_ctx);
}


//---------------------------------------------------------------------
template<v_type_t::kind_t tag>
LLVMTypeRef
v_type_integer_base_t<tag>::obtain_llvm_type(void) const
{
    return  LLVMIntTypeInContext(this->context.llvm_ctx, bits);
}

template LLVMTypeRef v_type_sint_t::obtain_llvm_type(void) const;
template LLVMTypeRef v_type_uint_t::obtain_llvm_type(void) const;


//---------------------------------------------------------------------
LLVMTypeRef
v_type_function_t::obtain_llvm_type(void) const
{
    const std::size_t N = key.first.size();

    LLVMTypeRef ft[N];

    for (unsigned i=0; i<N; ++i)
    {
        ft[i] = key.first[i]->llvm_type();
    }

    return  LLVMFunctionType(ft[0], ft+1, N-1, key.second);
}


//---------------------------------------------------------------------
LLVMTypeRef
v_type_pointer_t::obtain_llvm_type(void) const
{
    LLVMTypeRef et = nullptr;

    if (key.first->kind() == v_type_t::k_void)
    {
        et = context.opaque_void_type;
    }
    else
    {
        et = key.first->llvm_type();
    }

    return  LLVMPointerType(et, key.second);
}


//---------------------------------------------------------------------
LLVMTypeRef
v_type_struct_t::obtain_llvm_type(void) const
{
    LLVMTypeRef t = nullptr;

    if (name)
    {
        auto *c_name = name->c_str();

        t = LLVMGetTypeByName(context.llvm_mod, c_name);

        if (!t) t = LLVMStructCreateNamed(context.llvm_ctx, c_name);
    }

    if (body)
    {
        if (t  &&  !LLVMIsOpaqueStruct(t))  return t;

        const std::size_t N = body->first.size();

        LLVMTypeRef elts[N];

        for (unsigned i=0; i<N; ++i)
        {
            elts[i] = body->first[i]->llvm_type();
        }

        if (t)
        {
            LLVMStructSetBody(t, elts, N, body->second);
        }
        else
        {
            t = LLVMStructTypeInContext(context.llvm_ctx, elts, N, body->second);
        }
    }

    assert(t && "Unnamed struct without body");

    return t;
}


//---------------------------------------------------------------------
LLVMTypeRef
v_type_array_t::obtain_llvm_type(void) const
{
    auto et = key.first->llvm_type();

    using namespace llvm;

    return  wrap(ArrayType::get(unwrap(et), key.second));
}


//---------------------------------------------------------------------
template<>
LLVMTypeRef
v_type_fvector_t::obtain_llvm_type(void) const
{
    auto et = key.first->llvm_type();

    using namespace llvm;

    return  wrap(FixedVectorType::get(unwrap(et), key.second));
}

template<>
LLVMTypeRef
v_type_svector_t::obtain_llvm_type(void) const
{
    auto et = key.first->llvm_type();

    using namespace llvm;

    return  wrap(ScalableVectorType::get(unwrap(et), key.second));
}


//---------------------------------------------------------------------
//- ..
//---------------------------------------------------------------------
voidc_types_ctx_t::voidc_types_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size)
  : llvm_ctx(ctx),
    llvm_mod(LLVMModuleCreateWithNameInContext("empty_mod", ctx)),
    opaque_void_type(LLVMStructCreateNamed(ctx, "struct.v_target_opaque_void")),

    void_type(new v_type_void_t(*this)),
    f16_type (new v_type_f16_t (*this)),
    f32_type (new v_type_f32_t (*this)),
    f64_type (new v_type_f64_t (*this)),
    f128_type(new v_type_f128_t(*this)),

    bool_type     (get_uint_type(1)),
    char_type     (get_sint_type(8)),
    short_type    (get_sint_type(16)),
    int_type      (get_sint_type(8*int_size)),
    long_type     (get_sint_type(8*long_size)),
    long_long_type(get_sint_type(64)),
    intptr_t_type (get_sint_type(8*ptr_size)),
    size_t_type   (get_uint_type(8*ptr_size)),
    char32_t_type (get_uint_type(32))
{}


//---------------------------------------------------------------------
v_type_sint_t *
voidc_types_ctx_t::get_sint_type(unsigned bits)
{
    auto [it, ex] = sint_types.try_emplace(bits, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_sint_t(*this, it->first));
    }

    return it->second.get();
}

v_type_uint_t *
voidc_types_ctx_t::get_uint_type(unsigned bits)
{
    auto [it, ex] = uint_types.try_emplace(bits, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_uint_t(*this, it->first));
    }

    return it->second.get();
}


//---------------------------------------------------------------------
v_type_function_t *
voidc_types_ctx_t::get_function_type(v_type_t *ret, v_type_t **args, unsigned count, bool var_args)
{
    std::vector<v_type_t *> ft_data(count + 1);

    ft_data[0] = ret;

    for (unsigned i=0; i<count; ++i)
    {
        ft_data[i+1] = args[i];
    }

    v_type_function_t::key_t key = { ft_data, var_args };

    auto [it, ex] = function_types.try_emplace(key, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_function_t(*this, it->first));
    }

    return it->second.get();
}


//---------------------------------------------------------------------
v_type_pointer_t *
voidc_types_ctx_t::get_pointer_type(v_type_t *et, unsigned addr_space)
{
    v_type_pointer_t::key_t key = { et, addr_space };

    auto [it, ex] = pointer_types.try_emplace(key, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_pointer_t(*this, it->first));
    }

    return it->second.get();
}


//---------------------------------------------------------------------
v_type_struct_t *
voidc_types_ctx_t::get_struct_type(const std::string &name)
{
    auto [it, ex] = named_struct_types.try_emplace(name, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_struct_t(*this, it->first));
    }

    return it->second.get();
}

v_type_struct_t *
voidc_types_ctx_t::get_struct_type(v_type_t **elts, unsigned count, bool packed)
{
    std::vector<v_type_t *> et_data(count);

    for (unsigned i=0; i<count; ++i)
    {
        et_data[i] = elts[i];
    }

    v_type_struct_t::body_key_t key = { et_data, packed };

    auto [it, ex] = anon_struct_types.try_emplace(key, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_struct_t(*this, it->first));
    }

    return it->second.get();
}


//---------------------------------------------------------------------
v_type_array_t *
voidc_types_ctx_t::get_array_type(v_type_t *et, uint64_t count)
{
    v_type_array_t::key_t key = { et, count };

    auto [it, ex] = array_types.try_emplace(key, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_array_t(*this, it->first));
    }

    return it->second.get();
}


//---------------------------------------------------------------------
v_type_fvector_t *
voidc_types_ctx_t::get_fvector_type(v_type_t *et, unsigned count)
{
    v_type_fvector_t::key_t key = { et, count };

    auto [it, ex] = fvector_types.try_emplace(key, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_fvector_t(*this, it->first));
    }

    return it->second.get();
}

v_type_svector_t *
voidc_types_ctx_t::get_svector_type(v_type_t *et, unsigned count)
{
    v_type_svector_t::key_t key = { et, count };

    auto [it, ex] = svector_types.try_emplace(key, nullptr);

    if (!ex)
    {
        it->second.reset(new v_type_svector_t(*this, it->first));
    }

    return it->second.get();
}



//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_visitor.h"

#include "voidc_dllexport.h"
#include "voidc_util.h"
#include "voidc_llvm.h"

#include <cstdio>
#include <cassert>


//-----------------------------------------------------------------
//- ...
//-----------------------------------------------------------------
void voidc_visitor_t::static_initialize(void)
{
    static_assert((sizeof(visitor_ptr_t) % sizeof(intptr_t)) == 0);

    auto content_type = LLVMArrayType(compile_ctx_t::intptr_t_type, sizeof(visitor_ptr_t)/sizeof(intptr_t));

    auto gctx = LLVMGetGlobalContext();

    auto visitor_ptr_type = LLVMStructCreateNamed(gctx, "struct.voidc_opaque_visitor_ptr");

    LLVMStructSetBody(visitor_ptr_type, &content_type, 1, false);
    v_add_symbol("voidc_opaque_visitor_ptr", compile_ctx_t::LLVMOpaqueType_type, (void *)visitor_ptr_type);
}

//-----------------------------------------------------------------
void voidc_visitor_t::static_terminate(void)
{
}


//---------------------------------------------------------------------
//- !!!
//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

//---------------------------------------------------------------------
VOIDC_DEFINE_INITIALIZE_IMPL(visitor_ptr_t, voidc_initialize_visitor_impl)
VOIDC_DEFINE_RESET_IMPL(visitor_ptr_t, voidc_reset_visitor_impl)
VOIDC_DEFINE_COPY_IMPL(visitor_ptr_t, voidc_copy_visitor_impl)
VOIDC_DEFINE_MOVE_IMPL(visitor_ptr_t, voidc_move_visitor_impl)
VOIDC_DEFINE_STD_ANY_GET_POINTER_IMPL(visitor_ptr_t, voidc_std_any_get_pointer_visitor_impl)
VOIDC_DEFINE_STD_ANY_SET_POINTER_IMPL(visitor_ptr_t, voidc_std_any_set_pointer_visitor_impl)


//---------------------------------------------------------------------
void *voidc_visitor_get_void_method(const visitor_ptr_t *ptr, v_quark_t quark)
{
    return  (*ptr)->void_methods[quark];
}

void voidc_visitor_set_void_method(visitor_ptr_t *dst, const visitor_ptr_t *src, v_quark_t quark, void *void_method)
{
    auto visitor = (*src)->set_void_method(quark, void_method);

    *dst = std::make_shared<const voidc_visitor_t>(visitor);
}


//---------------------------------------------------------------------
VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"



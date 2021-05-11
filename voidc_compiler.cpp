//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_compiler.h"

#include "voidc_types.h"
#include "voidc_target.h"


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
visitor_sptr_t voidc_compiler;
visitor_sptr_t voidc_type_calc;


//=====================================================================
//- AST Visitor - Compiler (level 0) ...
//=====================================================================
static void compile_ast_stmt_list_t(const visitor_sptr_t *vis, void *aux, size_t count, bool start) {}
static void compile_ast_expr_list_t(const visitor_sptr_t *vis, void *aux, size_t count, bool start) {}


//---------------------------------------------------------------------
//- unit
//---------------------------------------------------------------------
static
void compile_ast_unit_t(const visitor_sptr_t *vis, void *aux,
                        const ast_stmt_list_sptr_t *stmt_list, int line, int column)
{
    if (!*stmt_list)  return;

    auto saved_target = voidc_global_ctx_t::target;

    voidc_global_ctx_t::target = voidc_global_ctx_t::voidc;

    auto &gctx = *voidc_global_ctx_t::target;                           //- target == voidc (!)
    auto &lctx = static_cast<voidc_local_ctx_t &>(*gctx.local_ctx);     //- Sic!

    auto saved_module = lctx.module;

    lctx.prepare_unit_action(line, column);


    auto entry = LLVMGetInsertBlock(gctx.builder);
    auto cur_f = LLVMGetBasicBlockParent(entry);

    auto unit_leave_b  = LLVMAppendBasicBlock(cur_f, "unit_leave_b");
    auto unit_leave_bv = LLVMBasicBlockAsValue(unit_leave_b);

    lctx.vars = lctx.vars.set("voidc.internal_branch_target_leave", {nullptr, unit_leave_bv});      //- Sic!


    (*stmt_list)->accept(*vis, aux);


    auto cur_b = LLVMGetInsertBlock(gctx.builder);

    if (!LLVMGetBasicBlockTerminator(cur_b))
    {
        auto leave_bv = lctx.vars["voidc.internal_branch_target_leave"].second;
        auto leave_b  = LLVMValueAsBasicBlock(leave_bv);

        LLVMBuildBr(gctx.builder, leave_b);
    }


    LLVMMoveBasicBlockAfter(unit_leave_b, cur_b);

    LLVMPositionBuilderAtEnd(gctx.builder, unit_leave_b);


    lctx.finish_unit_action();

    lctx.module = saved_module;

    voidc_global_ctx_t::target = saved_target;
}


//---------------------------------------------------------------------
//- stmt
//---------------------------------------------------------------------
static
void compile_ast_stmt_t(const visitor_sptr_t *vis, void *aux,
                        const std::string *vname, const ast_expr_sptr_t *expr)
{
    if (!*expr) return;

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.result_type = INVIOLABLE_TAG;

    lctx.push_temporaries();

    (*expr)->accept(*vis, aux);

    lctx.pop_temporaries();

    auto const &ret_name = vname->c_str();

    if (ret_name[0])
    {
        size_t len = 0;

        LLVMGetValueName2(lctx.result_value, &len);

        if (len == 0)
        {
            LLVMSetValueName2(lctx.result_value, ret_name, vname->size());
        }

        lctx.vars = lctx.vars.set(*vname, {lctx.result_type, lctx.result_value});
    }
}


//---------------------------------------------------------------------
//- expr_call
//---------------------------------------------------------------------
static
void compile_ast_expr_call_t(const visitor_sptr_t *vis, void *aux,
                             const ast_expr_sptr_t      *fexpr,
                             const ast_expr_list_sptr_t *args)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto fname = std::dynamic_pointer_cast<const ast_expr_identifier_t>(*fexpr))
    {
        auto &fun_name = fname->name;

        if (gctx.intrinsics.count(fun_name))
        {
            gctx.intrinsics[fun_name](vis, aux, args);

            return;
        }
    }

    auto tt = lctx.result_type;

    lctx.result_type = UNREFERENCE_TAG;

    (*fexpr)->accept(*vis, aux);

    v_type_t    *t = lctx.result_type;
    LLVMValueRef f = lctx.result_value;


    if (auto *ft = dynamic_cast<v_type_pointer_t *>(t))
    {
        t = ft->element_type();
    }

    auto ft = static_cast<v_type_function_t *>(t);

    auto par_count = ft->param_count();
    auto par_types = ft->param_types();

    auto arg_count = (*args)->data.size();


    auto values = std::make_unique<LLVMValueRef[]>(arg_count);

    for (int i=0; i<arg_count; ++i)
    {
        if (i < par_count)  lctx.result_type = par_types[i];
        else                lctx.result_type = UNREFERENCE_TAG;

        (*args)->data[i]->accept(*vis, aux);

        values[i] = lctx.result_value;
    }

    auto v = LLVMBuildCall(gctx.builder, f, values.get(), arg_count, "");


    lctx.result_type = tt;

    lctx.adopt_result(ft->return_type(), v);
}


//---------------------------------------------------------------------
//- expr_identifier
//---------------------------------------------------------------------
static
void compile_ast_expr_identifier_t(const visitor_sptr_t *vis, void *aux,
                                   const std::string *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    LLVMValueRef v = nullptr;
    v_type_t    *t = nullptr;

    if (!lctx.obtain_identifier(*name, t, v))
    {
        throw std::runtime_error("Identifier not found: " + *name);
    }

    lctx.adopt_result(t, v);
}


//---------------------------------------------------------------------
//- expr_integer
//---------------------------------------------------------------------
static
void compile_ast_expr_integer_t(const visitor_sptr_t *vis, void *aux,
                                intptr_t num)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    v_type_t *t = gctx.int_type;

    auto tt = lctx.result_type;

    if (tt  &&  tt != UNREFERENCE_TAG)   t = tt;

    lctx.result_type = t;


    bool is_reference = (t->kind() == v_type_t::k_reference);

    if (is_reference) t = static_cast<v_type_reference_t *>(t)->element_type();

    LLVMValueRef v;

    if (t->kind() == v_type_t::k_pointer  &&  num == 0)
    {
        v = LLVMConstPointerNull(t->llvm_type());
    }
    else
    {
        v = LLVMConstInt(t->llvm_type(), num, false);       //- ?
    }

    if (is_reference) v = lctx.make_temporary(t, v);

    lctx.result_value = v;
}


//---------------------------------------------------------------------
//- expr_string
//---------------------------------------------------------------------
static
void compile_ast_expr_string_t(const visitor_sptr_t *vis, void *aux,
                               const std::string *str)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto v = LLVMBuildGlobalStringPtr(gctx.builder, str->c_str(), "str");

    lctx.adopt_result(gctx.char_ptr_type, v);
}


//---------------------------------------------------------------------
//- expr_char
//---------------------------------------------------------------------
static
void compile_ast_expr_char_t(const visitor_sptr_t *vis, void *aux,
                             char32_t c)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    auto t = gctx.char32_t_type;

    auto v = LLVMConstInt(t->llvm_type(), c, false);

    lctx.adopt_result(t, v);
}


//=====================================================================
//- Type calculator - just expr_identifier...
//=====================================================================
static
void typecalc_ast_expr_identifier_t(const visitor_sptr_t *vis, void *aux,
                                    const std::string *name)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    if (auto type = lctx.find_type(name->c_str()))
    {
        assert(aux);

        auto ret = reinterpret_cast<v_type_t **>(aux);

        *ret = type;
    }
    else
    {
        throw std::runtime_error("Type not found: " + *name);
    }
}


//=====================================================================
//- Compiler visitor(s)
//=====================================================================
static
visitor_sptr_t compile_visitor_level_zero;

visitor_sptr_t
make_voidc_compiler(void)
{
    if (!compile_visitor_level_zero)
    {
        voidc_visitor_t vis;

#define DEF(type) \
        vis = vis.set_void_method(v_##type##_visitor_method_tag, (void *)compile_##type);

        DEFINE_AST_VISITOR_METHOD_TAGS(DEF)

#undef DEF

        compile_visitor_level_zero = std::make_shared<const voidc_visitor_t>(vis);
    }

    assert(compile_visitor_level_zero);

    return  compile_visitor_level_zero;
}


//---------------------------------------------------------------------
static
visitor_sptr_t typecalc_visitor_level_zero;

visitor_sptr_t
make_voidc_type_calc(void)
{
    if (!typecalc_visitor_level_zero)
    {
        voidc_visitor_t vis;

#define DEF(type) \
        vis = vis.set_void_method(v_##type##_visitor_method_tag, (void *)typecalc_##type);

        DEF(ast_expr_identifier_t)

#undef DEF

        typecalc_visitor_level_zero = std::make_shared<const voidc_visitor_t>(vis);
    }

    assert(typecalc_visitor_level_zero);

    return  typecalc_visitor_level_zero;
}



